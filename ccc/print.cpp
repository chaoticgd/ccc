#include "print.h"

#include <cmath>

namespace ccc {
	
struct VariableName {
	const std::string* identifier;
	std::vector<s8> pointer_chars;
};

enum VariableNamePrintFlags {
	NO_PRINT_FLAGS = 0,
	INSERT_SPACE_TO_LEFT = (1 << 0),
	INSERT_SPACE_TO_RIGHT = (1 << 1),
	BRACKETS_IF_POINTER = (1 << 2)
};

static void print_cpp_ast_node(FILE* dest, const ast::Node& node, VariableName& parent_name, s32 indentation_level, s32 digits_for_offset);
static void print_cpp_storage_class(FILE* dest, ast::StorageClass storage_class);
static void print_cpp_variable_name(FILE* dest, VariableName& name, u32 flags);
static void print_cpp_offset(FILE* dest, const ast::Node& node, s32 digits_for_offset);
static void indent(FILE* dest, s32 level);

void print_ast_nodes(FILE* dest, const std::vector<std::unique_ptr<ast::Node>>& nodes, OutputLanguage language, bool verbose) {
	switch(language) {
		case OutputLanguage::CPP: {
			bool last_was_multiline = true;
			for(size_t i = 0; i < nodes.size(); i++) {
				const std::unique_ptr<ast::Node>& node = nodes[i];
				assert(node.get());
				bool is_struct_or_union =
					node->descriptor == ast::INLINE_STRUCT ||
					node->descriptor == ast::INLINE_UNION;
				bool multiline =
					node->descriptor == ast::INLINE_ENUM ||
					is_struct_or_union;
				if(!last_was_multiline && multiline) {
					fprintf(dest, "\n");
				}
				if(node->conflicting_types) {
					fprintf(dest, "// warning: multiple differing types with the same name, only one recovered\n");
				}
				if(verbose) {
					if(node->symbol != nullptr) {
						fprintf(dest, "// symbol: %s\n", node->symbol->raw.c_str());
					}
				}
				VariableName name{nullptr};
				s32 digits_for_offset = 0;
				if(is_struct_or_union && node->size_bits > 0) {
					digits_for_offset = (s32) ceilf(log2(node->size_bits / 8.f) / 4.f);
				}
				print_cpp_ast_node(stdout, *node.get(), name, 0, digits_for_offset);
				fprintf(dest, ";\n");
				if(multiline && i != nodes.size() - 1) {
					fprintf(dest, "\n");
				}
				last_was_multiline = multiline;
			}
			break;
		}
		case OutputLanguage::JSON: {
			break;
		}
	}
}

static void print_cpp_ast_node(FILE* dest, const ast::Node& node, VariableName& parent_name, s32 indentation_level, s32 digits_for_offset) {
	VariableName this_name{&node.name};
	VariableName& name = node.name.empty() ? parent_name : this_name;
	
	print_cpp_storage_class(dest, node.storage_class);
	
	switch(node.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = node.as<ast::Array>();
			assert(array.element_type.get());
			print_cpp_ast_node(dest, *array.element_type.get(), name, indentation_level, digits_for_offset);
			fprintf(dest, "[%d]", array.element_count);
			break;
		}
		case ast::BITFIELD: {
			const ast::BitField& bit_field = node.as<ast::BitField>();
			assert(bit_field.underlying_type.get());
			print_cpp_ast_node(dest, *bit_field.underlying_type.get(), name, indentation_level, digits_for_offset);
			printf(" : %d", bit_field.size_bits);
			break;
		}
		case ast::FUNCTION: {
			const ast::Function& function = node.as<ast::Function>();
			assert(function.return_type.get());
			VariableName dummy{nullptr};
			print_cpp_ast_node(dest, *function.return_type.get(), dummy, indentation_level, digits_for_offset);
			fprintf(dest, " ");
			print_cpp_variable_name(dest, name, NO_PRINT_FLAGS | BRACKETS_IF_POINTER);
			fprintf(dest, "(");
			if(function.parameters.has_value()) {
				for(size_t i = 0; i < function.parameters->size(); i++) {
					assert((*function.parameters)[i].get());
					VariableName dummy{nullptr};
					print_cpp_ast_node(dest, *(*function.parameters)[i].get(), dummy, indentation_level, digits_for_offset);
					if(i != function.parameters->size() - 1) {
						fprintf(dest, ", ");
					}
				}
			} else {
				fprintf(dest, "/* parameters unknown */");
			}
			fprintf(dest, ")");
			break;
		}
		case ast::INLINE_ENUM: {
			const ast::InlineEnum& inline_enum = node.as<ast::InlineEnum>();
			fprintf(dest, "enum");
			bool name_on_top = (indentation_level == 0) && (inline_enum.storage_class != ast::StorageClass::TYPEDEF);
			if(name_on_top) {
				print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			fprintf(dest, " {");
			if(inline_enum.size_bits > -1) {
				printf(" // 0x%x", inline_enum.size_bits / 8);
			}
			fprintf(dest, "\n");
			for(size_t i = 0; i < inline_enum.constants.size(); i++) {
				s32 number = inline_enum.constants[i].first;
				const std::string& name = inline_enum.constants[i].second;
				bool is_last = i == inline_enum.constants.size() - 1;
				indent(dest, indentation_level + 1);
				fprintf(dest, "%s = %d%s\n", name.c_str(), number, is_last ? "" : ",");
			}
			indent(dest, indentation_level);
			fprintf(dest, "}");
			if(!name_on_top) {
				print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			break;
		}
		case ast::INLINE_STRUCT: {
			const ast::InlineStruct& inline_struct = node.as<ast::InlineStruct>();
			fprintf(dest, "struct");
			bool name_on_top = (indentation_level == 0) && (inline_struct.storage_class != ast::StorageClass::TYPEDEF);
			if(name_on_top) {
				print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			if(!inline_struct.base_classes.empty()) {
				fprintf(dest, " :");
				for(const ast::BaseClass& base_class : inline_struct.base_classes) {
					if(base_class.offset > -1) {
						fprintf(dest, " /* 0x%03x */", base_class.offset);
					}
					fprintf(dest, " %s", base_class.type_name.c_str());
				}
			}
			fprintf(dest, " { // 0x%x\n", inline_struct.size_bits / 8);
			for(const std::unique_ptr<ast::Node>& field : inline_struct.fields) {
				assert(field.get());
				indent(dest, indentation_level + 1);
				print_cpp_offset(dest, *field.get(), digits_for_offset);
				print_cpp_ast_node(dest, *field.get(), name, indentation_level + 1, digits_for_offset);
				fprintf(dest, ";\n");
			}
			if(!inline_struct.member_functions.empty()) {
				if(!inline_struct.fields.empty()) {
					indent(dest, indentation_level + 1);
					fprintf(dest, "\n");
				}
				for(size_t i = 0; i < inline_struct.member_functions.size(); i++) {
					assert(inline_struct.member_functions[i].get());
					indent(dest, indentation_level + 1);
					print_cpp_ast_node(dest, *inline_struct.member_functions[i].get(), name, indentation_level + 1, digits_for_offset);
					fprintf(dest, ";\n");
				}
			}
			indent(dest, indentation_level);
			fprintf(dest, "}");
			if(!name_on_top) {
				print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			break;
		}
		case ast::INLINE_UNION: {
			const ast::InlineUnion& inline_union = node.as<ast::InlineUnion>();
			fprintf(dest, "union");
			bool name_on_top = indentation_level == 0 && inline_union.storage_class != ast::StorageClass::TYPEDEF;
			if(name_on_top) {
				print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			fprintf(dest, " {\n");
			for(const std::unique_ptr<ast::Node>& field : inline_union.fields) {
				assert(field.get());
				indent(dest, indentation_level + 1);
				print_cpp_offset(dest, *field.get(), digits_for_offset);
				print_cpp_ast_node(dest, *field.get(), name, indentation_level + 1, digits_for_offset);
				fprintf(dest, ";\n");
			}
			if(!inline_union.member_functions.empty()) {
				if(!inline_union.fields.empty()) {
					indent(dest, indentation_level + 1);
					fprintf(dest, "\n");
				}
				for(size_t i = 0; i < inline_union.member_functions.size(); i++) {
					assert(inline_union.member_functions[i].get());
					indent(dest, indentation_level + 1);
					print_cpp_ast_node(dest, *inline_union.member_functions[i].get(), name, indentation_level + 1, digits_for_offset);
					fprintf(dest, ";\n");
				}
			}
			indent(dest, indentation_level);
			fprintf(dest, "}");
			if(!name_on_top) {
				print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			break;
		}
		case ast::POINTER: {
			const ast::Pointer& pointer = node.as<ast::Pointer>();
			assert(pointer.value_type.get());
			name.pointer_chars.emplace_back('*');
			print_cpp_ast_node(dest, *pointer.value_type.get(), name, indentation_level, digits_for_offset);
			print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			break;
		}
		case ast::REFERENCE: {
			const ast::Reference& reference = node.as<ast::Reference>();
			assert(reference.value_type.get());
			name.pointer_chars.emplace_back('&');
			print_cpp_ast_node(dest, *reference.value_type.get(), name, indentation_level, digits_for_offset);
			print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			break;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = node.as<ast::TypeName>();
			fprintf(dest, "%s", type_name.type_name.c_str());
			print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			break;
		}
	}
}

static void print_cpp_storage_class(FILE* dest, ast::StorageClass storage_class) {
	switch(storage_class) {
		case ast::StorageClass::NONE: break;
		case ast::StorageClass::TYPEDEF: fprintf(dest, "typedef "); break;
		case ast::StorageClass::EXTERN: fprintf(dest, "extern "); break;
		case ast::StorageClass::STATIC: fprintf(dest, "static "); break;
		case ast::StorageClass::AUTO: fprintf(dest, "auto "); break;
		case ast::StorageClass::REGISTER: fprintf(dest, "register "); break;
	}
}

static void print_cpp_variable_name(FILE* dest, VariableName& name, u32 flags) {
	bool has_name = name.identifier != nullptr && !name.identifier->empty();
	bool has_brackets = (flags & BRACKETS_IF_POINTER) && !name.pointer_chars.empty();
	if(has_name && (flags & INSERT_SPACE_TO_LEFT)) {
		fprintf(dest, " ");
	}
	if(has_brackets) {
		fprintf(dest, "(");
	}
	for(s32 i = (s32) name.pointer_chars.size() - 1; i >= 0; i--) {
		fprintf(dest, "%c", name.pointer_chars[i]);
	}
	name.pointer_chars.clear();
	if(has_name) {
		fprintf(dest, "%s", name.identifier->c_str());
		name.identifier = nullptr;
		if((flags & INSERT_SPACE_TO_RIGHT) && name.identifier->empty()) {
			fprintf(dest, " ");
		}
	}
	if(has_brackets) {
		fprintf(dest, ")");
	}
}

static void print_cpp_offset(FILE* dest, const ast::Node& node, s32 digits_for_offset) {
	if(node.storage_class != ast::StorageClass::STATIC && node.absolute_offset_bytes > -1) {
		assert(digits_for_offset > -1 && digits_for_offset < 100);
		fprintf(dest, "/* 0x%0*x", digits_for_offset, node.absolute_offset_bytes);
		if(node.bitfield_offset_bits > -1) {
			fprintf(dest, ":%d", node.bitfield_offset_bits);
		}
		fprintf(dest, " */ ");
	}
}

static void indent(FILE* dest, s32 level) {
	for(s32 i = 0; i < level; i++) {
		fputc('\t', dest);
	}
}

}
