#include "data_refinement.h"

namespace ccc {

static std::unique_ptr<ast::Node> refine_node(s32 virtual_address, const ast::Node& type, const HighSymbolTable& high, const std::vector<Module*>& modules);
static std::unique_ptr<ast::Node> refine_builtin(s32 virtual_address, BuiltInClass bclass, const std::vector<Module*>& modules);

void refine_global_variables(HighSymbolTable& high, const std::vector<Module*>& modules) {
	for(std::unique_ptr<ast::SourceFile>& source_file : high.source_files) {
		for(std::unique_ptr<ast::Node>& node : source_file->globals) {
			ast::Variable& global = node->as<ast::Variable>();
			if(global.storage.type == ast::VariableStorageType::GLOBAL && global.storage.global_address > -1) {
				global.data = refine_node(global.storage.global_address, *global.type.get(), high, modules);
			}
		}
	}
}

static std::unique_ptr<ast::Node> refine_node(s32 virtual_address, const ast::Node& type, const HighSymbolTable& high, const std::vector<Module*>& modules) {
	switch(type.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = type.as<ast::Array>();
			if(array.element_type->computed_size_bytes < 0) {
				return nullptr;
			}
			std::unique_ptr<ast::InitializerList> list = std::make_unique<ast::InitializerList>();
			for(s32 i = 0; i < array.element_count; i++) {
				s32 offset = i * array.element_type->computed_size_bytes;
				type.is_currently_processing = true;
				std::unique_ptr<ast::Node> element = refine_node(virtual_address + offset, *array.element_type.get(), high, modules);
				type.is_currently_processing = false;
				if(element == nullptr) {
					return nullptr;
				}
				list->children.emplace_back(std::move(element));
			}
			return list;
		}
		case ast::BITFIELD: {
			break;
		}
		case ast::BUILTIN: {
			const ast::BuiltIn& builtin = type.as<ast::BuiltIn>();
			return refine_builtin(virtual_address, builtin.bclass, modules);
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
			return refine_builtin(virtual_address, BuiltInClass::SIGNED_32, modules);
		}
		case ast::INLINE_STRUCT_OR_UNION: {
			const ast::InlineStructOrUnion& struct_or_union = type.as<ast::InlineStructOrUnion>();
			std::unique_ptr<ast::InitializerList> list = std::make_unique<ast::InitializerList>();
			for(const std::unique_ptr<ast::Node>& field : struct_or_union.fields) {
				type.is_currently_processing = true;
				std::unique_ptr<ast::Node> child = refine_node(virtual_address + field->relative_offset_bytes, *field.get(), high, modules);
				type.is_currently_processing = false;
				if(child == nullptr) {
					return nullptr;
				}
				list->children.emplace_back(std::move(child));
			}
			return list;
		}
		case ast::LITERAL: {
			break;
		}
		case ast::POINTER: {
			return refine_builtin(virtual_address, BuiltInClass::UNSIGNED_32, modules);
		}
		case ast::POINTER_TO_DATA_MEMBER: {
			return refine_builtin(virtual_address, BuiltInClass::UNSIGNED_32, modules);
		}
		case ast::REFERENCE: {
			return refine_builtin(virtual_address, BuiltInClass::UNSIGNED_32, modules);
		}
		case ast::SOURCE_FILE: {
			break;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = type.as<ast::TypeName>();
			if(type_name.referenced_file_index > -1 && type_name.referenced_stabs_type_number > -1) {
				const ast::SourceFile& source_file = *high.source_files[type_name.referenced_file_index].get();
				auto type_index = source_file.stabs_type_number_to_deduplicated_type_index.find(type_name.referenced_stabs_type_number);
				if(type_index != source_file.stabs_type_number_to_deduplicated_type_index.end()) {
					ast::Node& resolved_type = *high.deduplicated_types.at(type_index->second).get();
					if(!resolved_type.is_currently_processing) {
						type.is_currently_processing = true;
						std::unique_ptr<ast::Node> result = refine_node(virtual_address, resolved_type, high, modules);;
						type.is_currently_processing = false;
						return std::move(result);
					}
				}
			}
			return nullptr;
		}
		case ast::VARIABLE: {
			break;
		}
	}
	return nullptr;
	verify_not_reached("Failed to refine global variable (%s).", ast::node_type_to_string(type));
}

static std::unique_ptr<ast::Node> refine_builtin(s32 virtual_address, BuiltInClass bclass, const std::vector<Module*>& modules) {
	std::unique_ptr<ast::Literal> literal = nullptr;
	
	switch(bclass) {
		case BuiltInClass::VOID: {
			break;
		}
		case BuiltInClass::UNSIGNED_8:
		case BuiltInClass::UNQUALIFIED_8:
		case BuiltInClass::UNSIGNED_16:
		case BuiltInClass::UNSIGNED_32:
		case BuiltInClass::UNSIGNED_64: {
			literal = std::make_unique<ast::Literal>();
			literal->literal_type = ast::LiteralType::INTEGER_UNSIGNED;
			literal->value.unsigned_integer = 0;
			read_virtual((u8*) &literal->value.unsigned_integer, virtual_address, builtin_class_size(bclass), modules);
			break;
		}
		case BuiltInClass::SIGNED_8:
		case BuiltInClass::SIGNED_16:
		case BuiltInClass::SIGNED_32:
		case BuiltInClass::SIGNED_64: {
			literal = std::make_unique<ast::Literal>();
			literal->literal_type = ast::LiteralType::INTEGER_SIGNED;
			literal->value.integer = 0;
			read_virtual((u8*) &literal->value.unsigned_integer, virtual_address, builtin_class_size(bclass), modules);
			break;
		}
		case BuiltInClass::BOOL_8: {
			literal = std::make_unique<ast::Literal>();
			literal->literal_type = ast::LiteralType::BOOLEAN;
			literal->value.boolean = false;
			read_virtual((u8*) &literal->value.boolean, virtual_address, 1, modules);
			break;
		}
		case BuiltInClass::FLOAT_32: {
			literal = std::make_unique<ast::Literal>();
			literal->literal_type = ast::LiteralType::FLOAT_SINGLE;
			literal->value.float_single = 0.f;
			read_virtual((u8*) &literal->value.float_single, virtual_address, 4, modules);
			break;
		}
		case BuiltInClass::FLOAT_64: {
			literal = std::make_unique<ast::Literal>();
			literal->literal_type = ast::LiteralType::FLOAT_DOUBLE;
			literal->value.float_double = 0.0;
			read_virtual((u8*) &literal->value.float_double, virtual_address, 8, modules);
			break;
		}
		case BuiltInClass::UNSIGNED_128:
		case BuiltInClass::SIGNED_128:
		case BuiltInClass::UNQUALIFIED_128:
		case BuiltInClass::FLOAT_128: {
			literal = std::make_unique<ast::Literal>();
			literal->literal_type = ast::LiteralType::VECTOR;
			literal->value.vector[0] = 0.f;
			literal->value.vector[1] = 0.f;
			literal->value.vector[2] = 0.f;
			literal->value.vector[3] = 0.f;
			read_virtual((u8*) literal->value.vector, virtual_address, 16, modules);
			break;
		}
		case BuiltInClass::UNKNOWN_PROBABLY_ARRAY: {
			break;
		}
	}
	
	return literal;
}

}
