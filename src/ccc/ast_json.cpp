// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "ast_json.h"

#include "symbol_database.h"

namespace ccc::ast {

void write_json(JsonWriter& json, const Node* ptr, const SymbolDatabase& database)
{
	CCC_ASSERT(ptr);
	const Node& node = *ptr;
	
	json.StartObject();
	
	json.Key("descriptor");
	json.String(node_type_to_string(node));
	
	if(!node.name.empty()) {
		json.Key("name");
		json.String(node.name.c_str());
	}
	if(node.storage_class != SC_NONE) {
		json.Key("storage_class");
		json.String(storage_class_to_string((StorageClass) node.storage_class));
	}
	if(node.relative_offset_bytes != -1) {
		json.Key("relative_offset_bytes");
		json.Int(node.relative_offset_bytes);
	}
	if(node.absolute_offset_bytes != -1) {
		json.Key("absolute_offset_bytes");
		json.Int(node.absolute_offset_bytes);
	}
	if(node.size_bits != -1) {
		json.Key("size_bits");
		json.Int(node.size_bits);
	}
	if(node.is_const) {
		json.Key("is_const");
		json.Bool(node.is_const);
	}
	if(node.is_volatile) {
		json.Key("is_volatile");
		json.Bool(node.is_volatile);
	}
	if(node.access_specifier != AS_PUBLIC) {
		json.Key("access_specifier");
		json.String(access_specifier_to_string((AccessSpecifier) node.access_specifier));
	}
	
	switch(node.descriptor) {
		case ARRAY: {
			const Array& array = node.as<Array>();
			json.Key("element_type");
			write_json(json, array.element_type.get(), database);
			json.Key("element_count");
			json.Int(array.element_count);
			break;
		}
		case BITFIELD: {
			const BitField& bitfield = node.as<BitField>();
			json.Key("bitfield_offset_bits");
			json.Int(node.as<BitField>().bitfield_offset_bits);
			json.Key("underlying_type");
			write_json(json, bitfield.underlying_type.get(), database);
			break;
		}
		case BUILTIN: {
			const BuiltIn& builtin = node.as<BuiltIn>();
			json.Key("class");
			json.String(builtin_class_to_string(builtin.bclass));
			break;
		}
		case ENUM: {
			const Enum& enumeration = node.as<Enum>();
			json.Key("constants");
			json.StartArray();
			for(const auto& [value, name] : enumeration.constants) {
				json.StartObject();
				json.Key("value");
				json.Int(value);
				json.Key("name");
				json.String(name);
				json.EndObject();
			}
			json.EndArray();
			break;
		}
		case ERROR: {
			const Error& error = node.as<Error>();
			json.Key("message");
			json.String(error.message);
			break;
		}
		case FORWARD_DECLARED: {
			// TODO
			break;
		}
		case FUNCTION: {
			const Function& function = node.as<Function>();
			if(function.return_type.has_value()) {
				json.Key("return_type");
				write_json(json, function.return_type->get(), database);
			}
			if(function.parameters.has_value()) {
				json.Key("parameters");
				json.StartArray();
				for(const std::unique_ptr<Node>& node : *function.parameters) {
					write_json(json, node.get(), database);
				}
				json.EndArray();
			}
			const char* modifier = "none";
			if(function.modifier == MemberFunctionModifier::STATIC) {
				modifier = "static";
			} else if(function.modifier == MemberFunctionModifier::VIRTUAL) {
				modifier = "virtual";
			}
			json.Key("modifier");
			json.String(modifier);
			json.Key("vtable_index");
			json.Int(function.vtable_index);
			json.Key("is_constructor");
			json.Bool(function.is_constructor);
			break;
		}
		case POINTER_OR_REFERENCE: {
			const PointerOrReference& pointer_or_reference = node.as<PointerOrReference>();
			json.Key("value_type");
			write_json(json, pointer_or_reference.value_type.get(), database);
			break;
		}
		case POINTER_TO_DATA_MEMBER: {
			const PointerToDataMember& member_pointer = node.as<PointerToDataMember>();
			json.Key("class_type");
			write_json(json, member_pointer.class_type.get(), database);
			json.Key("member_type");
			write_json(json, member_pointer.member_type.get(), database);
			break;
		}
		case STRUCT_OR_UNION: {
			const StructOrUnion& struct_or_union = node.as<StructOrUnion>();
			if(struct_or_union.is_struct) {
				json.Key("base_classes");
				json.StartArray();
				for(const std::unique_ptr<Node>& base_class : struct_or_union.base_classes) {
					write_json(json, base_class.get(), database);
				}
				json.EndArray();
			}
			json.Key("fields");
			json.StartArray();
			for(const std::unique_ptr<Node>& node : struct_or_union.fields) {
				write_json(json, node.get(), database);
			}
			json.EndArray();
			json.Key("member_functions");
			json.StartArray();
			for(const std::unique_ptr<Node>& node : struct_or_union.member_functions) {
				write_json(json, node.get(), database);
			}
			json.EndArray();
			break;
		}
		case TYPE_NAME: {
			const TypeName& type_name = node.as<TypeName>();
			json.Key("source");
			json.String(type_name_source_to_string(type_name.source));
			json.Key("data_type_handle");
			json.Int(database.data_types.index_from_handle(type_name.data_type_handle()));
			break;
		}
	}
	
	json.EndObject();
}

}
