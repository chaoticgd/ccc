#include "data_refinement.h"

namespace ccc {

struct DataRefinementContext {
	const HighSymbolTable& high;
	const std::vector<Module*>& modules;
	const std::map<s32, const ast::Node*>& address_to_node;
};

static void refine_variable(ast::Variable& variable, const DataRefinementContext& context);
static std::unique_ptr<ast::Node> refine_node(s32 virtual_address, const ast::Node& type, const DataRefinementContext& context);
static std::unique_ptr<ast::Node> refine_builtin(s32 virtual_address, BuiltInClass bclass, const DataRefinementContext& context);
static std::unique_ptr<ast::Node> refine_pointer_or_reference(s32 virtual_address, const ast::Node& type, const DataRefinementContext& context);
static const char* generate_format_string(s32 size, bool is_signed);

void refine_variables(HighSymbolTable& high, const std::vector<Module*>& modules) {
	// Build a map of where all functions and globals are in memory, so that we
	// can lookup where pointers point to.
	std::map<s32, const ast::Node*> address_to_node;
	for(std::unique_ptr<ast::SourceFile>& source_file : high.source_files) {
		for(std::unique_ptr<ast::Node>& node : source_file->functions) {
			ast::FunctionDefinition& function = node->as<ast::FunctionDefinition>();
			if(function.address_range.low > -1) {
				address_to_node[function.address_range.low] = node.get();
			}
		}
		for(std::unique_ptr<ast::Node>& node : source_file->globals) {
			ast::Variable& variable = node->as<ast::Variable>();
			if(variable.storage.type == ast::VariableStorageType::GLOBAL && variable.storage.global_address > -1) {
				address_to_node[variable.storage.global_address] = node.get();
			}
		}
	}
	
	DataRefinementContext context{high, modules, address_to_node};
	
	// Refine all global variables.
	for(std::unique_ptr<ast::SourceFile>& source_file : high.source_files) {
		for(std::unique_ptr<ast::Node>& node : source_file->globals) {
			refine_variable(node->as<ast::Variable>(), context);
		}
	}
	
	// Refine all static local variables.
	for(std::unique_ptr<ast::SourceFile>& source_file : high.source_files) {
		for(std::unique_ptr<ast::Node>& node : source_file->functions) {
			ast::FunctionDefinition& function = node->as<ast::FunctionDefinition>();
			for(std::unique_ptr<ast::Variable>& local : function.locals) {
				refine_variable(*local.get(), context);
			}
		}
	}
}

static void refine_variable(ast::Variable& variable, const DataRefinementContext& context) {
	bool valid_type = variable.storage.type == ast::VariableStorageType::GLOBAL;
	bool valid_address = variable.storage.global_address > -1;
	bool valid_location = variable.storage.global_location != ast::GlobalVariableLocation::BSS
		&& variable.storage.global_location != ast::GlobalVariableLocation::SBSS;
	if(valid_type && valid_address && valid_location) {
		variable.data = refine_node(variable.storage.global_address, *variable.type.get(), context);
	}
}

static std::unique_ptr<ast::Node> refine_node(s32 virtual_address, const ast::Node& type, const DataRefinementContext& context) {
	switch(type.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = type.as<ast::Array>();
			if(array.element_type->computed_size_bytes < 0) {
				std::unique_ptr<ast::Data> error = std::make_unique<ast::Data>();
				error->string = "CCC_CANNOT_COMPUTE_ELEMENT_SIZE";
				return error;
			}
			std::unique_ptr<ast::InitializerList> list = std::make_unique<ast::InitializerList>();
			for(s32 i = 0; i < array.element_count; i++) {
				s32 offset = i * array.element_type->computed_size_bytes;
				type.is_currently_processing = true;
				std::unique_ptr<ast::Node> element = refine_node(virtual_address + offset, *array.element_type.get(), context);
				type.is_currently_processing = false;
				if(element->descriptor == ast::DATA) element->as<ast::Data>().field_name = stringf("[%d]", i);
				if(element->descriptor == ast::INITIALIZER_LIST) element->as<ast::InitializerList>().field_name = stringf("[%d]", i);
				list->children.emplace_back(std::move(element));
			}
			return list;
		}
		case ast::BITFIELD: {
			std::unique_ptr<ast::Data> data = std::make_unique<ast::Data>();
			data->string = "CCC_BITFIELD";
			return data;
		}
		case ast::BUILTIN: {
			const ast::BuiltIn& builtin = type.as<ast::BuiltIn>();
			return refine_builtin(virtual_address, builtin.bclass, context);
		}
		case ast::DATA: {
			break;
		}
		case ast::FUNCTION_DEFINITION: {
			break;
		}
		case ast::FUNCTION_TYPE: {
			break;
		}
		case ast::INITIALIZER_LIST: {
			break;
		}
		case ast::INLINE_ENUM: {
			const ast::InlineEnum& inline_enum = type.as<ast::InlineEnum>();
			std::unique_ptr<ast::Data> data = std::make_unique<ast::Data>();
			s32 value = 0;
			read_virtual((u8*) &value, virtual_address, 4, context.modules);
			for(const auto& [number, name] : inline_enum.constants) {
				if(number == value) {
					data->string = name;
					return data;
				}
			}
			data->string = stringf("%d", value);
			return data;
		}
		case ast::INLINE_STRUCT_OR_UNION: {
			const ast::InlineStructOrUnion& struct_or_union = type.as<ast::InlineStructOrUnion>();
			std::unique_ptr<ast::InitializerList> list = std::make_unique<ast::InitializerList>();
			for(const std::unique_ptr<ast::Node>& field : struct_or_union.fields) {
				if(field->storage_class == ast::SC_STATIC) {
					continue;
				}
				type.is_currently_processing = true;
				std::unique_ptr<ast::Node> child = refine_node(virtual_address + field->relative_offset_bytes, *field.get(), context);
				type.is_currently_processing = false;
				if(child->descriptor == ast::DATA) child->as<ast::Data>().field_name = stringf(".%s", field->name.c_str());
				if(child->descriptor == ast::INITIALIZER_LIST) child->as<ast::InitializerList>().field_name = stringf(".%s", field->name.c_str());
				list->children.emplace_back(std::move(child));
			}
			return list;
		}
		case ast::POINTER:
		case ast::REFERENCE: {
			return refine_pointer_or_reference(virtual_address, type, context);
		}
		case ast::POINTER_TO_DATA_MEMBER: {
			return refine_builtin(virtual_address, BuiltInClass::UNSIGNED_32, context);
		}
		case ast::SOURCE_FILE: {
			break;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = type.as<ast::TypeName>();
			if(type_name.referenced_file_index > -1 && type_name.referenced_stabs_type_number > -1) {
				const ast::SourceFile& source_file = *context.high.source_files[type_name.referenced_file_index].get();
				auto type_index = source_file.stabs_type_number_to_deduplicated_type_index.find(type_name.referenced_stabs_type_number);
				if(type_index != source_file.stabs_type_number_to_deduplicated_type_index.end()) {
					ast::Node& resolved_type = *context.high.deduplicated_types.at(type_index->second).get();
					if(!resolved_type.is_currently_processing) {
						type.is_currently_processing = true;
						std::unique_ptr<ast::Node> result = refine_node(virtual_address, resolved_type, context);
						type.is_currently_processing = false;
						return std::move(result);
					}
				}
			}
			std::unique_ptr<ast::Data> error = std::make_unique<ast::Data>();
			error->string = "CCC_TYPE_LOOKUP_FAILED";
			return error;
		}
		case ast::VARIABLE: {
			break;
		}
	}
	
	verify_not_reached("Failed to refine global variable (%s).", ast::node_type_to_string(type));
}

static std::unique_ptr<ast::Node> refine_builtin(s32 virtual_address, BuiltInClass bclass, const DataRefinementContext& context) {
	std::unique_ptr<ast::Data> data = nullptr;
	
	switch(bclass) {
		case BuiltInClass::VOID: {
			break;
		}
		case BuiltInClass::UNSIGNED_8:
		case BuiltInClass::UNQUALIFIED_8:
		case BuiltInClass::UNSIGNED_16:
		case BuiltInClass::UNSIGNED_32:
		case BuiltInClass::UNSIGNED_64: {
			data = std::make_unique<ast::Data>();
			u64 value = 0;
			s32 size = builtin_class_size(bclass);
			read_virtual((u8*) &value, virtual_address, size, context.modules);
			const char* format = generate_format_string(size, false);
			data->string = stringf(format, value);
			break;
		}
		case BuiltInClass::SIGNED_8:
		case BuiltInClass::SIGNED_16:
		case BuiltInClass::SIGNED_32:
		case BuiltInClass::SIGNED_64: {
			data = std::make_unique<ast::Data>();
			s64 value = 0;
			s32 size = builtin_class_size(bclass);
			read_virtual((u8*) &value, virtual_address, size, context.modules);
			const char* format = generate_format_string(size, true);
			data->string = stringf(format, value);
			break;
		}
		case BuiltInClass::BOOL_8: {
			data = std::make_unique<ast::Data>();
			bool value = false;
			read_virtual((u8*) &value, virtual_address, 1, context.modules);
			data->string = value ? "true" : "false";
			break;
		}
		case BuiltInClass::FLOAT_32: {
			data = std::make_unique<ast::Data>();
			float value = 0.f;
			static_assert(sizeof(value) == 4);
			read_virtual((u8*) &value, virtual_address, 4, context.modules);
			data->string = stringf("%.9g", value);
			break;
		}
		case BuiltInClass::FLOAT_64: {
			data = std::make_unique<ast::Data>();
			double value = 0.f;
			static_assert(sizeof(value) == 8);
			read_virtual((u8*) &value, virtual_address, 8, context.modules);
			data->string = stringf("%.17g", value);
			break;
		}
		case BuiltInClass::UNSIGNED_128:
		case BuiltInClass::SIGNED_128:
		case BuiltInClass::UNQUALIFIED_128:
		case BuiltInClass::FLOAT_128: {
			data = std::make_unique<ast::Data>();
			float value[4];
			read_virtual((u8*) value, virtual_address, 16, context.modules);
			data->string = stringf("VECTOR(%.9g, %.9g, %.9g, %.9g)",
				value[0], value[1], value[2], value[3]);
			break;
		}
		case BuiltInClass::UNKNOWN_PROBABLY_ARRAY: {
			break;
		}
	}
	
	return data;
}

static std::unique_ptr<ast::Node> refine_pointer_or_reference(s32 virtual_address, const ast::Node& type, const DataRefinementContext& context) {
	std::unique_ptr<ast::Data> data = std::make_unique<ast::Data>();
	s32 address = 0;
	read_virtual((u8*) &address, virtual_address, 4, context.modules);
	if(address != 0) {
		auto node = context.address_to_node.find(address);
		if(node != context.address_to_node.end()) {
			if(node->second->descriptor == ast::VARIABLE) {
				const ast::Variable& variable = node->second->as<ast::Variable>();
				if(type.descriptor == ast::POINTER && variable.type->descriptor != ast::ARRAY) {
					data->string += "&";
				}
			}
			data->string += node->second->name;
		} else {
			data->string = stringf("0x%x", address);
		}
	} else {
		data->string = "NULL";
	}
	return data;
}

static const char* generate_format_string(s32 size, bool is_signed) {
	switch(size) {
		case 1: return is_signed ? "%hhd" : "%hhu";
		case 2: return is_signed ? "%hd" : "%hu";
		case 4: return is_signed ? "%d" : "%hu";
	}
	return is_signed ? ("%" PRId64) : ("%" PRIu64);
}

}
