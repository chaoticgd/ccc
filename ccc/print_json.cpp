#include "print_json.h"

namespace ccc {

struct JsonWriter {
	FILE* dest;
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
};

static void print_json_ast_node(JsonWriter& json, const ast::Node& node);
static void print_json_variable_storage(JsonWriter& json, const ast::VariableStorage& storage);

void print_json(FILE* dest, const AnalysisResults& src, bool print_per_file_types) {
	JsonWriter json;
	json.dest = dest;
	
	json.begin_object();
	
	json.property("files");
	json.begin_array();
	for(const std::unique_ptr<ast::SourceFile>& file : src.source_files) {
		print_json_ast_node(json, *file);
	}
	json.end_array();
	
	if(!print_per_file_types) {
		json.property("deduplicated_types");
		json.begin_array();
		for(const std::unique_ptr<ast::Node>& node : src.deduplicated_types) {
			print_json_ast_node(json, *node.get());
		}
		json.end_array();
	}
	
	json.end_object();
}

static void print_json_ast_node(JsonWriter& json, const ast::Node& node) {
	json.begin_object();
	json.string_property("descriptor", ast::node_type_to_string(node));
	if(!node.name.empty()) {
		json.string_property("name", node.name.c_str());
	}
	if(node.storage_class != ast::StorageClass::NONE) {
		json.string_property("storage_class", storage_class_to_string(node.storage_class));
	}
	if(node.relative_offset_bytes != -1) {
		json.number_property("relative_offset_bytes", node.relative_offset_bytes);
	}
	if(node.absolute_offset_bytes != -1) {
		json.number_property("absolute_offset_bytes", node.absolute_offset_bytes);
	}
	if(node.bitfield_offset_bits != -1) {
		json.number_property("bitfield_offset_bits", node.bitfield_offset_bits);
	}
	if(node.size_bits != -1) {
		json.number_property("size_bits", node.size_bits);
	}
	if(node.order != -1) {
		json.number_property("order", node.order);
	}
	switch(node.descriptor) {
		case ast::NodeDescriptor::ARRAY: {
			const ast::Array& array = node.as<ast::Array>();
			json.property("element_type");
			print_json_ast_node(json, *array.element_type.get());
			json.number_property("element_count", array.element_count);
			break;
		}
		case ast::NodeDescriptor::BITFIELD: {
			const ast::BitField& bitfield = node.as<ast::BitField>();
			json.property("underlying_type");
			print_json_ast_node(json, *bitfield.underlying_type.get());
			break;
		}
		case ast::NodeDescriptor::BUILTIN: {
			const ast::BuiltIn& builtin = node.as<ast::BuiltIn>();
			json.string_property("class", builtin_class_to_string(builtin.bclass));
			break;
		}
		case ast::NodeDescriptor::COMPOUND_STATEMENT: {
			const ast::CompoundStatement& compound_statement = node.as<ast::CompoundStatement>();
			json.property("children");
			json.begin_array();
			for(const std::unique_ptr<ast::Node>& child : compound_statement.children) {
				print_json_ast_node(json, *child.get());
			}
			json.end_array();
			break;
		}
		case ast::NodeDescriptor::FUNCTION_DEFINITION: {
			const ast::FunctionDefinition& function_definition = node.as<ast::FunctionDefinition>();
			json.property("type");
			print_json_ast_node(json, *function_definition.type.get());
			json.property("body");
			print_json_ast_node(json, *function_definition.body.get());
			break;
		}
		case ast::NodeDescriptor::FUNCTION_TYPE: {
			const ast::FunctionType& function = node.as<ast::FunctionType>();
			json.property("return_type");
			print_json_ast_node(json, *function.return_type.get());
			if(function.parameters.has_value()) {
				json.property("parameters");
				json.begin_array();
				for(const std::unique_ptr<ast::Node>& node : *function.parameters) {
					print_json_ast_node(json, *node.get());
				}
				json.end_array();
			}
			const char* modifier = "none";
			if(function.modifier == MemberFunctionModifier::STATIC) {
				modifier = "static";
			} else if(function.modifier == MemberFunctionModifier::VIRTUAL) {
				modifier = "virtual";
			}
			json.string_property("modifier", modifier);
			json.boolean_property("is_constructor", function.is_constructor);
			break;
		}
		case ast::NodeDescriptor::INLINE_ENUM: {
			const ast::InlineEnum& inline_enum = node.as<ast::InlineEnum>();
			json.property("constants");
			json.begin_array();
			for(const auto& [number, name] : inline_enum.constants) {
				json.begin_object();
				json.number_property("number", number);
				json.string_property("name", name.c_str());
				json.end_object();
			}
			json.end_array();
			break;
		}
		case ast::NodeDescriptor::INLINE_STRUCT_OR_UNION: {
			const ast::InlineStructOrUnion& struct_or_union = node.as<ast::InlineStructOrUnion>();
			if(struct_or_union.is_struct) {
				json.property("base_classes");
				json.begin_array();
				for(const ast::BaseClass& base_class : struct_or_union.base_classes) {
					json.begin_object();
					json.string_property("visibility", stabs_field_visibility_to_string(base_class.visibility));
					json.number_property("offset", base_class.offset);
					json.string_property("type_name", base_class.type_name.c_str());
					json.end_object();
				}
				json.end_array();
			}
			json.property("fields");
			json.begin_array();
			for(const std::unique_ptr<ast::Node>& node : struct_or_union.fields) {
				print_json_ast_node(json, *node.get());
			}
			json.end_array();
			json.property("member_functions");
			json.begin_array();
			for(const std::unique_ptr<ast::Node>& node : struct_or_union.member_functions) {
				print_json_ast_node(json, *node.get());
			}
			json.end_array();
			break;
		}
		case ast::NodeDescriptor::POINTER: {
			const ast::Pointer& pointer = node.as<ast::Pointer>();
			json.property("value_type");
			print_json_ast_node(json, *pointer.value_type.get());
			break;
		}
		case ast::NodeDescriptor::REFERENCE: {
			const ast::Reference& reference = node.as<ast::Reference>();
			json.property("value_type");
			print_json_ast_node(json, *reference.value_type.get());
			break;
		}
		case ast::NodeDescriptor::SOURCE_FILE: {
			const ast::SourceFile& source_file = node.as<ast::SourceFile>();
			json.string_property("path", source_file.full_path.c_str());
			json.number_property("text_address", source_file.text_address);
			json.property("types");
			json.begin_array();
			for(const std::unique_ptr<ast::Node>& type : source_file.types) {
				print_json_ast_node(json, *type.get());
			}
			json.end_array();
			json.property("functions");
			json.begin_array();
			for(const std::unique_ptr<ast::Node>& function : source_file.functions) {
				print_json_ast_node(json, *function.get());
			}
			json.end_array();
			json.property("globals");
			json.begin_array();
			for(const std::unique_ptr<ast::Node>& global : source_file.globals) {
				print_json_ast_node(json, *global.get());
			}
			json.end_array();
			break;
		}
		case ast::NodeDescriptor::TYPE_NAME: {
			const ast::TypeName& type_name = node.as<ast::TypeName>();
			json.string_property("type_name", type_name.type_name.c_str());
			break;
		}
		case ast::NodeDescriptor::VARIABLE: {
			const ast::Variable& variable = node.as<ast::Variable>();
			const char* class_string = "";
			switch(variable.variable_class) {
				case ast::VariableClass::GLOBAL: class_string = "global";
				case ast::VariableClass::LOCAL: class_string = "local";
				case ast::VariableClass::PARAMETER: class_string = "parameter";
			}
			json.string_property("class", class_string);
			print_json_variable_storage(json, variable.storage);
			if(variable.block.low != 0 || variable.block.high != 0) {
				json.number_property("block_low", variable.block.low);
				json.number_property("block_high", variable.block.high);
			}
			json.property("type");
			print_json_ast_node(json, *variable.type.get());
			break;
		}
	}
	json.end_object();
}

static void print_json_variable_storage(JsonWriter& json, const ast::VariableStorage& storage) {
	json.property("storage");
	json.begin_object();
	switch(storage.location) {
		case ast::VariableStorageLocation::BSS: {
			json.string_property("location", "bss");
			json.number_property("address", storage.bss_or_data_address);
			break;
		}
		case ast::VariableStorageLocation::DATA: {
			json.string_property("location", "data");
			json.number_property("address", storage.bss_or_data_address);
			break;
		}
		case ast::VariableStorageLocation::REGISTER: {
			json.string_property("location", "register");
			json.string_property("register", mips::REGISTER_STRING_TABLES[(s32) storage.register_class][storage.register_index_relative]);
			json.string_property("register_class", mips::REGISTER_CLASSES[(s32) storage.register_class]);
			json.number_property("dbx_register_number", storage.dbx_register_number);
			json.number_property("register_index", storage.register_index_relative);
			break;
		}
		case ast::VariableStorageLocation::STACK: {
			json.string_property("location", "stack");
			json.number_property("stack_offset", storage.stack_pointer_offset);
			break;
		}
	}
	json.end_object();
}

void JsonWriter::begin_object() {
	if(needs_comma) {
		fprintf(dest, ",");
	}
	needs_comma = false;
	fprintf(dest, "{");
}

void JsonWriter::end_object() {
	needs_comma = true;
	fprintf(dest, "}");
}

void JsonWriter::property(const char* name) {
	if(needs_comma) {
		fprintf(dest, ",");
	}
	needs_comma = false;
	fprintf(dest, "\"%s\":", name);
}

void JsonWriter::string(const char* value) {
	if(needs_comma) {
		fprintf(dest, ",");
	}
	needs_comma = true;
	fprintf(dest, "\"%s\"", value);
}

void JsonWriter::number(s64 value) {
	if(needs_comma) {
		fprintf(dest, ",");
	}
	needs_comma = true;
	fprintf(dest, "%" PRId64, value);
}

void JsonWriter::boolean(bool value) {
	if(needs_comma) {
		fprintf(dest, ",");
	}
	needs_comma = true;
	fprintf(dest, "%s", value ? "true" : "false");
}

void JsonWriter::begin_array() {
	if(needs_comma) {
		fprintf(dest, ",");
	}
	needs_comma = false;
	fprintf(dest, "[");
}

void JsonWriter::end_array() {
	needs_comma = true;
	fprintf(dest, "]");
}

void JsonWriter::string_property(const char* name, const char* value) {
	property(name);
	string(value);
}

void JsonWriter::number_property(const char* name, s64 value) {
	property(name);
	number(value);
}

void JsonWriter::boolean_property(const char* name, bool value) {
	property(name);
	boolean(value);
}

}
