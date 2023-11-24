// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "print_json.h"

#include "registers.h"

namespace ccc {

struct JsonPrinter {
	FILE* out;
	bool needs_comma = false;
	
	void begin_object();
	void end_object();
	void property(const char* name);
	
	void string(const char* string);
	void number(s64 number);
	void boolean(bool value);
	void begin_array();
	void end_array();
	
	void string_property(const char* name, const char* value);
	void number_property(const char* name, s64 value);
	void boolean_property(const char* name, bool value);
	
	std::string encode_string(const char* string);
};

static void print_json_ast_node(JsonPrinter& json, const ast::Node* ptr);
static void print_json_variable_storage(JsonPrinter& json, const Variable::Storage& storage);
static s64 merge_stabs_type_number_parts(const StabsTypeNumber& number);

void print_json(FILE* out, const SymbolDatabase& database, bool print_per_file_types) {
	JsonPrinter json;
	json.out = out;
	
	json.begin_object();
	
	json.number_property("version", 8);
	
	//json.property("files");
	//json.begin_array();
	//for(const std::unique_ptr<ast::SourceFile>& file : high.source_files) {
	//	print_json_ast_node(json, file.get());
	//}
	//json.end_array();
	//
	//if(!print_per_file_types) {
	//	json.property("deduplicated_types");
	//	json.begin_array();
	//	for(const std::unique_ptr<ast::Node>& node : high.deduplicated_types) {
	//		print_json_ast_node(json, node.get());
	//	}
	//	json.end_array();
	//}
	
	json.end_object();
}

static void print_json_ast_node(JsonPrinter& json, const ast::Node* ptr) {
	CCC_ASSERT(ptr);
	const ast::Node& node = *ptr;
	json.begin_object();
	json.string_property("descriptor", ast::node_type_to_string(node));
	if(!node.name.empty()) {
		json.string_property("name", node.name.c_str());
	}
	if(node.storage_class != ast::SC_NONE) {
		json.string_property("storage_class", storage_class_to_string((ast::StorageClass) node.storage_class));
	}
	if(node.relative_offset_bytes != -1) {
		json.number_property("relative_offset_bytes", node.relative_offset_bytes);
	}
	if(node.absolute_offset_bytes != -1) {
		json.number_property("absolute_offset_bytes", node.absolute_offset_bytes);
	}
	if(node.size_bits != -1) {
		json.number_property("size_bits", node.size_bits);
	}
	if(node.is_const) {
		json.boolean_property("is_const", node.is_const);
	}
	if(node.is_volatile) {
		json.boolean_property("is_volatile", node.is_volatile);
	}
	if(node.access_specifier != ast::AS_PUBLIC) {
		json.string_property("access_specifier", access_specifier_to_string((ast::AccessSpecifier) node.access_specifier));
	}
	if(node.stabs_type_number.type != -1) {
		json.number_property("stabs_type_number", merge_stabs_type_number_parts(node.stabs_type_number));
	}
	//if(!node.files.empty()) {
	//	json.property("files");
	//	json.begin_array();
	//	for(s32 file : node.files) {
	//		json.number(file);
	//	}
	//	json.end_array();
	//}
	switch(node.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = node.as<ast::Array>();
			json.property("element_type");
			print_json_ast_node(json, array.element_type.get());
			json.number_property("element_count", array.element_count);
			break;
		}
		case ast::BITFIELD: {
			const ast::BitField& bitfield = node.as<ast::BitField>();
			json.number_property("bitfield_offset_bits", node.as<ast::BitField>().bitfield_offset_bits);
			json.property("underlying_type");
			print_json_ast_node(json, bitfield.underlying_type.get());
			break;
		}
		case ast::BUILTIN: {
			const ast::BuiltIn& builtin = node.as<ast::BuiltIn>();
			json.string_property("class", builtin_class_to_string(builtin.bclass));
			break;
		}
		case ast::ENUM: {
			const ast::Enum& enumeration = node.as<ast::Enum>();
			json.property("constants");
			json.begin_array();
			for(const auto& [value, name] : enumeration.constants) {
				json.begin_object();
				json.number_property("value", value);
				json.string_property("name", name.c_str());
				json.end_object();
			}
			json.end_array();
			break;
		}
		//case ast::FUNCTION_DEFINITION: {
		//	const ast::FunctionDefinition& function = node.as<ast::FunctionDefinition>();
		//	if(function.address_range.valid()) {
		//		json.property("address_range");
		//		json.begin_object();
		//		json.number_property("low", (function.address_range.low != (u32) -1) ? (s64) function.address_range.low : -1);
		//		json.number_property("high", (function.address_range.high != (u32) -1) ? (s64) function.address_range.high : -1);
		//		json.end_object();
		//	}
		//	if(!function.relative_path.empty()) {
		//		json.string_property("relative_path", function.relative_path.c_str());
		//	}
		//	json.property("type");
		//	print_json_ast_node(json, function.type.get());
		//	json.property("locals");
		//	json.begin_array();
		//	for(const std::unique_ptr<ast::Variable>& local : function.locals) {
		//		print_json_ast_node(json, local.get());
		//	}
		//	json.end_array();
		//	json.property("line_numbers");
		//	json.begin_array();
		//	for(const ast::LineNumberPair& pair : function.line_numbers) {
		//		json.begin_array();
		//		json.number((pair.address != (u32) -1) ? (s64) pair.address : -1);
		//		json.number(pair.line_number);
		//		json.end_array();
		//	}
		//	json.end_array();
		//	json.property("sub_source_files");
		//	json.begin_array();
		//	for(const ast::SubSourceFile& sub : function.sub_source_files) {
		//		json.begin_object();
		//		json.number_property("address", (sub.address != (u32) -1) ? (s64) sub.address : -1);
		//		json.string_property("path", sub.relative_path.c_str());
		//		json.end_object();
		//	}
		//	json.end_array();
		//	break;
		//}
		case ast::FUNCTION_TYPE: {
			const ast::FunctionType& function = node.as<ast::FunctionType>();
			if(function.return_type.has_value()) {
				json.property("return_type");
				print_json_ast_node(json, function.return_type->get());
			}
			if(function.parameters.has_value()) {
				json.property("parameters");
				json.begin_array();
				for(const std::unique_ptr<ast::Node>& node : *function.parameters) {
					print_json_ast_node(json, node.get());
				}
				json.end_array();
			}
			const char* modifier = "none";
			if(function.modifier == ast::MemberFunctionModifier::STATIC) {
				modifier = "static";
			} else if(function.modifier == ast::MemberFunctionModifier::VIRTUAL) {
				modifier = "virtual";
			}
			json.string_property("modifier", modifier);
			json.number_property("vtable_index", function.vtable_index);
			json.boolean_property("is_constructor", function.is_constructor);
			break;
		}
		case ast::POINTER_OR_REFERENCE: {
			const ast::PointerOrReference& pointer_or_reference = node.as<ast::PointerOrReference>();
			json.property("value_type");
			print_json_ast_node(json, pointer_or_reference.value_type.get());
			break;
		}
		case ast::POINTER_TO_DATA_MEMBER: {
			const ast::PointerToDataMember& member_pointer = node.as<ast::PointerToDataMember>();
			json.property("class_type");
			print_json_ast_node(json, member_pointer.class_type.get());
			json.property("member_type");
			print_json_ast_node(json, member_pointer.member_type.get());
			break;
		}
		//case ast::SOURCE_FILE: {
		//	const ast::SourceFile& source_file = node.as<ast::SourceFile>();
		//	json.string_property("path", source_file.full_path.c_str());
		//	json.string_property("relative_path", source_file.relative_path.c_str());
		//	json.number_property("text_address", (source_file.text_address != (u32) -1) ? (s64) source_file.text_address : -1);
		//	json.property("types");
		//	json.begin_array();
		//	for(const std::unique_ptr<ast::Node>& type : source_file.data_types) {
		//		print_json_ast_node(json, type.get());
		//	}
		//	json.end_array();
		//	json.property("functions");
		//	json.begin_array();
		//	for(const std::unique_ptr<ast::Node>& function : source_file.functions) {
		//		print_json_ast_node(json, function.get());
		//	}
		//	json.end_array();
		//	json.property("globals");
		//	json.begin_array();
		//	for(const std::unique_ptr<ast::Node>& global : source_file.globals) {
		//		print_json_ast_node(json, global.get());
		//	}
		//	json.end_array();
		//	json.property("stabs_type_number_to_deduplicated_type_index");
		//	json.begin_object();
		//	for(const auto [stabs_type_number, deduplicated_type_index] : source_file.stabs_type_number_to_deduplicated_type_index) {
		//		s64 merged_type_number = merge_stabs_type_number_parts(stabs_type_number);
		//		json.number_property(std::to_string(merged_type_number).c_str(), deduplicated_type_index);
		//	}
		//	json.end_object();
		//	break;
		//}
		//
		case ast::STRUCT_OR_UNION: {
			const ast::StructOrUnion& struct_or_union = node.as<ast::StructOrUnion>();
			if(struct_or_union.is_struct) {
				json.property("base_classes");
				json.begin_array();
				for(const std::unique_ptr<ast::Node>& base_class : struct_or_union.base_classes) {
					print_json_ast_node(json, base_class.get());
				}
				json.end_array();
			}
			json.property("fields");
			json.begin_array();
			for(const std::unique_ptr<ast::Node>& node : struct_or_union.fields) {
				print_json_ast_node(json, node.get());
			}
			json.end_array();
			json.property("member_functions");
			json.begin_array();
			for(const std::unique_ptr<ast::Node>& node : struct_or_union.member_functions) {
				print_json_ast_node(json, node.get());
			}
			json.end_array();
			break;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = node.as<ast::TypeName>();
			const char* source = "";
			switch(type_name.source) {
				case ast::TypeNameSource::REFERENCE: source = "reference"; break;
				case ast::TypeNameSource::CROSS_REFERENCE: source = "cross_reference"; break;
				case ast::TypeNameSource::ANONYMOUS_REFERENCE: source = "anonymous_reference"; break;
				case ast::TypeNameSource::ERROR: source = "error"; break;
			}
			json.string_property("source", source);
			json.string_property("type_name", type_name.type_name.c_str());
			if(type_name.referenced_file_handle != (u32) -1) {
				json.number_property("referenced_file_handle", type_name.referenced_file_handle);
			}
			if(type_name.referenced_stabs_type_number.type > -1) {
				json.number_property("referenced_stabs_type_number", merge_stabs_type_number_parts(type_name.referenced_stabs_type_number));
			}
			break;
		}
		//case ast::VARIABLE: {
		//	const ast::Variable& variable = node.as<ast::Variable>();
		//	const char* class_string = "";
		//	switch(variable.variable_class) {
		//		case ast::VariableClass::GLOBAL: class_string = "global"; break;
		//		case ast::VariableClass::LOCAL: class_string = "local"; break;
		//		case ast::VariableClass::PARAMETER: class_string = "parameter"; break;
		//	}
		//	json.string_property("class", class_string);
		//	print_json_variable_storage(json, variable.storage);
		//	json.number_property("block_low", (variable.block.low != (u32) -1) ? (s64) variable.block.low : -1);
		//	json.number_property("block_high", (variable.block.high != (u32) -1) ? (s64) variable.block.high : -1);
		//	json.property("type");
		//	print_json_ast_node(json, variable.type.get());
		//	break;
		//}
	}
	json.end_object();
}

static void print_json_variable_storage(JsonPrinter& json, const Variable::Storage& storage) {
	json.property("storage");
	json.begin_object();
	if(const Variable::GlobalStorage* global_storage = std::get_if<Variable::GlobalStorage>(&storage)) {
		json.string_property("type", "global");
		json.string_property("global_location", Variable::GlobalStorage::location_to_string(global_storage->location));
		json.number_property("global_address", global_storage->address.value);
	}
	if(const Variable::RegisterStorage* register_storage = std::get_if<Variable::RegisterStorage>(&storage)) {
		auto [register_class, register_index_relative] =
				mips::map_dbx_register_index(register_storage->dbx_register_number);
			json.string_property("type", "register");
			json.string_property("register", mips::REGISTER_STRING_TABLES[(s32) register_class][register_index_relative]);
			json.string_property("register_class", mips::REGISTER_CLASSES[(s32) register_class]);
			json.number_property("dbx_register_number", register_storage->dbx_register_number);
			json.number_property("register_index", register_index_relative);
			json.boolean_property("is_by_reference", register_storage->is_by_reference);
	}
	
	if(const Variable::StackStorage* stack_storage = std::get_if<Variable::StackStorage>(&storage)) {
		json.string_property("type", "stack");
		json.number_property("stack_offset", stack_storage->stack_pointer_offset);
	}
	json.end_object();
}

static s64 merge_stabs_type_number_parts(const StabsTypeNumber& number) {
	if(number.file > -1) {
		return number.type | (s64) number.file << 32;
	} else {
		return number.type;
	}
}

void JsonPrinter::begin_object() {
	if(needs_comma) {
		fprintf(out, ",");
	}
	needs_comma = false;
	fprintf(out, "{");
}

void JsonPrinter::end_object() {
	needs_comma = true;
	fprintf(out, "}");
}

void JsonPrinter::property(const char* name) {
	if(needs_comma) {
		fprintf(out, ",");
	}
	needs_comma = false;
	fprintf(out, "\"%s\":", name);
}

void JsonPrinter::string(const char* value) {
	if(needs_comma) {
		fprintf(out, ",");
	}
	needs_comma = true;
	std::string encoded = encode_string(value);
	fprintf(out, "\"%s\"", encoded.c_str());
}

void JsonPrinter::number(s64 value) {
	if(needs_comma) {
		fprintf(out, ",");
	}
	needs_comma = true;
	fprintf(out, "%" PRId64, value);
}

void JsonPrinter::boolean(bool value) {
	if(needs_comma) {
		fprintf(out, ",");
	}
	needs_comma = true;
	fprintf(out, "%s", value ? "true" : "false");
}

void JsonPrinter::begin_array() {
	if(needs_comma) {
		fprintf(out, ",");
	}
	needs_comma = false;
	fprintf(out, "[");
}

void JsonPrinter::end_array() {
	needs_comma = true;
	fprintf(out, "]");
}

void JsonPrinter::string_property(const char* name, const char* value) {
	property(name);
	string(value);
}

void JsonPrinter::number_property(const char* name, s64 value) {
	property(name);
	number(value);
}

void JsonPrinter::boolean_property(const char* name, bool value) {
	property(name);
	boolean(value);
}

std::string JsonPrinter::encode_string(const char* string) {
	static const char* HEX_DIGITS = "0123456789abcdef";
	
	std::string encoded;
	for(const char* ptr = string; *ptr != 0; ptr++) {
		if(*ptr >= 0 && *ptr <= 0x7f && isprint(*ptr)) {
			if(*ptr == '"' || *ptr == '\\') {
				encoded += '\\';
			}
			encoded += *ptr;
		} else {
			encoded += '%';
			encoded += HEX_DIGITS[*ptr >> 4];
			encoded += HEX_DIGITS[*ptr & 0xf];
		}
	}
	
	return encoded;
}

}
