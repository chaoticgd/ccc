// This file is part of the Chaos Compiler Collection.
//
// SPDX-License-Identifier: MIT

#include "data_refinement.h"

namespace ccc {

struct DataRefinementContext {
	const SymbolTable& symbol_table;
	const std::vector<ElfFile*>& elves;
};

static void refine_variable(Variable& variable, const DataRefinementContext& context);
static std::unique_ptr<ast::Node> refine_node(u32 virtual_address, const ast::Node& type, const DataRefinementContext& context);
static std::unique_ptr<ast::Node> refine_builtin(u32 virtual_address, ast::BuiltInClass bclass, const DataRefinementContext& context);
static std::unique_ptr<ast::Node> refine_pointer_or_reference(u32 virtual_address, const ast::Node& type, const DataRefinementContext& context);
static const char* generate_format_string(s32 size, bool is_signed);
static std::string single_precision_float_to_string(float value);
static std::string string_format(const char* format, va_list args);
static std::string stringf(const char* format, ...);

void refine_variables(SymbolTable& symbol_table, const std::vector<ElfFile*>& elves) {
	DataRefinementContext context{symbol_table, elves};
	
	// Refine all global variables.
	for(GlobalVariable& global_variable : symbol_table.global_variables) {
		refine_variable(global_variable, context);
	}
	
	// Refine all static local variables.
	for(LocalVariable& local_variable : symbol_table.local_variables) {
		refine_variable(local_variable, context);
	}
}

static void refine_variable(Variable& variable, const DataRefinementContext& context) {
	Variable::GlobalStorage* global_storage = std::get_if<Variable::GlobalStorage>(&variable.storage);
	if(global_storage) {
		bool valid_address = global_storage->address != (u32) -1;
		bool valid_location = global_storage->location != Variable::GlobalStorage::Location::BSS
			&& global_storage->location != Variable::GlobalStorage::Location::SBSS;
		if(valid_address && valid_location) {
			variable.data = refine_node(global_storage->address.value, variable.type(), context);
		}
	}
}

static std::unique_ptr<ast::Node> refine_node(u32 virtual_address, const ast::Node& type, const DataRefinementContext& context) {
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
			data->string = "BITFIELD";
			return data;
		}
		case ast::BUILTIN: {
			const ast::BuiltIn& builtin = type.as<ast::BuiltIn>();
			return refine_builtin(virtual_address, builtin.bclass, context);
		}
		case ast::DATA: {
			break;
		}
		case ast::ENUM: {
			const ast::Enum& enumeration = type.as<ast::Enum>();
			std::unique_ptr<ast::Data> data = std::make_unique<ast::Data>();
			s32 value = 0;
			read_virtual((u8*) &value, virtual_address, 4, context.elves);
			for(const auto& [number, name] : enumeration.constants) {
				if(number == value) {
					data->string = name;
					return data;
				}
			}
			data->string = stringf("%d", value);
			return data;
		}
		case ast::FUNCTION_TYPE: {
			break;
		}
		case ast::INITIALIZER_LIST: {
			break;
		}
		case ast::POINTER_OR_REFERENCE: {
			return refine_pointer_or_reference(virtual_address, type, context);
		}
		case ast::POINTER_TO_DATA_MEMBER: {
			return refine_builtin(virtual_address, ast::BuiltInClass::UNSIGNED_32, context);
		}
		case ast::STRUCT_OR_UNION: {
			const ast::StructOrUnion& struct_or_union = type.as<ast::StructOrUnion>();
			std::unique_ptr<ast::InitializerList> list = std::make_unique<ast::InitializerList>();
			for(s32 i = 0; i < (s32) struct_or_union.base_classes.size(); i++) {
				const std::unique_ptr<ast::Node>& base_class = struct_or_union.base_classes[i];
				std::unique_ptr<ast::Node> child = refine_node(virtual_address + base_class->absolute_offset_bytes, *base_class.get(), context);
				if(child->descriptor == ast::DATA) child->as<ast::Data>().field_name = stringf("base class %d", i);
				if(child->descriptor == ast::INITIALIZER_LIST) child->as<ast::InitializerList>().field_name = stringf("base class %d", i);
				list->children.emplace_back(std::move(child));
			}
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
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = type.as<ast::TypeName>();
			DataTypeHandle resolved_type_handle = context.symbol_table.lookup_type(type_name, false);
			if(resolved_type_handle.valid()) {
				const DataType& resolved_type = context.symbol_table.data_types.at(resolved_type_handle);
				if(!resolved_type.type().is_currently_processing) {
					resolved_type.type().is_currently_processing = true;
					std::unique_ptr<ast::Node> result = refine_node(virtual_address, resolved_type.type(), context);
					resolved_type.type().is_currently_processing = false;
					return result;
				}
			}
			std::unique_ptr<ast::Data> error = std::make_unique<ast::Data>();
			error->string = "CCC_TYPE_LOOKUP_FAILED";
			return error;
		}
	}
	
	CCC_FATAL("Failed to refine global variable (%s).", ast::node_type_to_string(type));
}

static std::unique_ptr<ast::Node> refine_builtin(u32 virtual_address, ast::BuiltInClass bclass, const DataRefinementContext& context) {
	std::unique_ptr<ast::Data> data = nullptr;
	
	switch(bclass) {
		case ast::BuiltInClass::VOID: {
			break;
		}
		case ast::BuiltInClass::UNSIGNED_8:
		case ast::BuiltInClass::UNQUALIFIED_8:
		case ast::BuiltInClass::UNSIGNED_16:
		case ast::BuiltInClass::UNSIGNED_32:
		case ast::BuiltInClass::UNSIGNED_64: {
			data = std::make_unique<ast::Data>();
			u64 value = 0;
			s32 size = builtin_class_size(bclass);
			read_virtual((u8*) &value, virtual_address, size, context.elves);
			const char* format = generate_format_string(size, false);
			data->string = stringf(format, value);
			break;
		}
		case ast::BuiltInClass::SIGNED_8:
		case ast::BuiltInClass::SIGNED_16:
		case ast::BuiltInClass::SIGNED_32:
		case ast::BuiltInClass::SIGNED_64: {
			data = std::make_unique<ast::Data>();
			s64 value = 0;
			s32 size = builtin_class_size(bclass);
			read_virtual((u8*) &value, virtual_address, size, context.elves);
			const char* format = generate_format_string(size, true);
			data->string = stringf(format, value);
			break;
		}
		case ast::BuiltInClass::BOOL_8: {
			data = std::make_unique<ast::Data>();
			bool value = false;
			read_virtual((u8*) &value, virtual_address, 1, context.elves);
			data->string = value ? "true" : "false";
			break;
		}
		case ast::BuiltInClass::FLOAT_32: {
			data = std::make_unique<ast::Data>();
			float value = 0.f;
			static_assert(sizeof(value) == 4);
			read_virtual((u8*) &value, virtual_address, 4, context.elves);
			data->string = single_precision_float_to_string(value);
			break;
		}
		case ast::BuiltInClass::FLOAT_64: {
			data = std::make_unique<ast::Data>();
			double value = 0.f;
			static_assert(sizeof(value) == 8);
			read_virtual((u8*) &value, virtual_address, 8, context.elves);
			data->string = stringf("%g", value);
			if(strtof(data->string.c_str(), nullptr) != value) {
				data->string = stringf("%.17g", value);
			}
			break;
		}
		case ast::BuiltInClass::UNSIGNED_128:
		case ast::BuiltInClass::SIGNED_128:
		case ast::BuiltInClass::UNQUALIFIED_128:
		case ast::BuiltInClass::FLOAT_128: {
			data = std::make_unique<ast::Data>();
			float value[4];
			read_virtual((u8*) value, virtual_address, 16, context.elves);
			data->string = stringf("VECTOR(%s, %s, %s, %s)",
				single_precision_float_to_string(value[0]).c_str(),
				single_precision_float_to_string(value[1]).c_str(),
				single_precision_float_to_string(value[2]).c_str(),
				single_precision_float_to_string(value[3]).c_str());
			break;
		}
		case ast::BuiltInClass::UNKNOWN_PROBABLY_ARRAY: {
			break;
		}
	}
	
	CCC_CHECK_FATAL(data != nullptr, "Failed to refine builtin.");
	return data;
}

static std::unique_ptr<ast::Node> refine_pointer_or_reference(u32 virtual_address, const ast::Node& type, const DataRefinementContext& context) {
	std::unique_ptr<ast::Data> data = std::make_unique<ast::Data>();
	u32 address = 0;
	read_virtual((u8*) &address, virtual_address, 4, context.elves);
	if(address != 0) {
		const Function* function_symbol = context.symbol_table.functions[address];
		if(function_symbol) {
			//if(node->second->descriptor == ast::VARIABLE) {
			//	const ast::Variable& variable = node->second->as<ast::Variable>();
			//	bool is_pointer = type.descriptor == ast::POINTER_OR_REFERENCE
			//		&& type.as<ast::PointerOrReference>().is_pointer;
			//	if(is_pointer && variable.type->descriptor != ast::ARRAY) {
			//		data->string += "&";
			//	}
			//}
			data->string += function_symbol->name();
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

static std::string single_precision_float_to_string(float value) {
	std::string result = stringf("%g", value);
	if(strtof(result.c_str(), nullptr) != value) {
		result = stringf("%.9g", value);
	}
	if(result.find(".") == std::string::npos) {
		result += ".";
	}
	result += "f";
	return result;
}

static std::string string_format(const char* format, va_list args) {
	static char buffer[16 * 1024];
	vsnprintf(buffer, sizeof(buffer), format, args);
	return std::string(buffer);
}

static std::string stringf(const char* format, ...) {
	va_list args;
	va_start(args, format);
	std::string string = string_format(format, args);
	va_end(args);
	return string;
}

}
