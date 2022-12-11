#include "print.h"

#include <cmath>

namespace ccc {
	
struct VariableName {
	const std::string* identifier;
	std::vector<s8> pointer_chars;
};

enum VariableNamePrintFlags {
	NO_VAR_PRINT_FLAGS = 0,
	INSERT_SPACE_TO_LEFT = (1 << 0),
	INSERT_SPACE_TO_RIGHT = (1 << 1),
	BRACKETS_IF_POINTER = (1 << 2)
};

static void print_cpp_ast_node(FILE* dest, const ast::Node& node, VariableName& parent_name, s32 indentation_level, s32 digits_for_offset, u32 flags);
static void print_cpp_storage_class(FILE* dest, ast::StorageClass storage_class);
static void print_cpp_variable_name(FILE* dest, VariableName& name, u32 flags);
static void print_cpp_offset(FILE* dest, const ast::Node& node, s32 digits_for_offset);
static void indent(FILE* dest, s32 level);

void print_cpp_abi_information(FILE* dest, const std::set<std::pair<std::string, RangeClass>>& builtins) {
	fprintf(dest, "// ABI information:\n");
	for(const auto& [type, range_class] : builtins) {
		const char* range_string;
		switch(range_class) {
			case RangeClass::UNSIGNED_8: range_string = "8-bit unsigned integer"; break;
			case RangeClass::SIGNED_8: range_string = "8-bit signed integer"; break;
			case RangeClass::UNSIGNED_16: range_string = "16-bit unsigned integer"; break;
			case RangeClass::SIGNED_16: range_string = "16-bit signed integer"; break;
			case RangeClass::UNSIGNED_32: range_string = "32-bit unsigned integer"; break;
			case RangeClass::SIGNED_32: range_string = "32-bit signed integer"; break;
			case RangeClass::FLOAT_32: range_string = "32-bit floating point"; break;
			case RangeClass::UNSIGNED_64: range_string = "64-bit unsigned integer"; break;
			case RangeClass::SIGNED_64: range_string = "64-bit signed integer"; break;
			case RangeClass::FLOAT_64: range_string = "64-bit floating point"; break;
			case RangeClass::UNSIGNED_128: range_string = "128-bit unsigned integer"; break;
			case RangeClass::SIGNED_128: range_string = "128-bit signed integer"; break;
			case RangeClass::UNKNOWN_PROBABLY_ARRAY: range_string = ""; break;
		}
		fprintf(dest, "//   %-25s%s\n", type.c_str(), range_string);
	}
}

void print_cpp_ast_nodes(FILE* dest, const std::vector<std::unique_ptr<ast::Node>>& nodes, u32 flags) {
	bool last_was_multiline = true;
	for(size_t i = 0; i < nodes.size(); i++) {
		const std::unique_ptr<ast::Node>& node = nodes[i];
		assert(node.get());
		bool multiline =
			node->descriptor == ast::INLINE_ENUM ||
			node->descriptor == ast::INLINE_STRUCT_OR_UNION;
		if(!last_was_multiline && multiline) {
			fprintf(dest, "\n");
		}
		if(node->conflicting_types) {
			fprintf(dest, "// warning: multiple differing types with the same name, only one recovered\n");
		}
		if(flags & PRINT_VERBOSE) {
			if(node->symbol != nullptr) {
				fprintf(dest, "// symbol: %s\n", node->symbol->raw.c_str());
			}
		}
		VariableName name{nullptr};
		s32 digits_for_offset = 0;
		if(node->descriptor == ast::INLINE_STRUCT_OR_UNION && node->size_bits > 0) {
			digits_for_offset = (s32) ceilf(log2(node->size_bits / 8.f) / 4.f);
		}
		print_cpp_ast_node(stdout, *node.get(), name, 0, digits_for_offset, flags);
		fprintf(dest, ";\n");
		if(multiline && i != nodes.size() - 1) {
			fprintf(dest, "\n");
		}
		last_was_multiline = multiline;
	}
}

static void print_cpp_ast_node(FILE* dest, const ast::Node& node, VariableName& parent_name, s32 indentation_level, s32 digits_for_offset, u32 flags) {
	VariableName this_name{&node.name};
	VariableName& name = node.name.empty() ? parent_name : this_name;
	
	print_cpp_storage_class(dest, node.storage_class);
	
	switch(node.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = node.as<ast::Array>();
			assert(array.element_type.get());
			print_cpp_ast_node(dest, *array.element_type.get(), name, indentation_level, digits_for_offset, flags);
			fprintf(dest, "[%d]", array.element_count);
			break;
		}
		case ast::BITFIELD: {
			const ast::BitField& bit_field = node.as<ast::BitField>();
			assert(bit_field.underlying_type.get());
			print_cpp_ast_node(dest, *bit_field.underlying_type.get(), name, indentation_level, digits_for_offset, flags);
			printf(" : %d", bit_field.size_bits);
			break;
		}
		case ast::FUNCTION: {
			const ast::Function& function = node.as<ast::Function>();
			assert(function.return_type.get());
			VariableName dummy{nullptr};
			print_cpp_ast_node(dest, *function.return_type.get(), dummy, indentation_level, digits_for_offset, flags);
			fprintf(dest, " ");
			print_cpp_variable_name(dest, name, BRACKETS_IF_POINTER);
			fprintf(dest, "(");
			if(function.parameters.has_value()) {
				for(size_t i = 0; i < function.parameters->size(); i++) {
					assert((*function.parameters)[i].get());
					VariableName dummy{nullptr};
					print_cpp_ast_node(dest, *(*function.parameters)[i].get(), dummy, indentation_level, digits_for_offset, flags);
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
		case ast::INLINE_STRUCT_OR_UNION: {
			const ast::InlineStructOrUnion& struct_or_union = node.as<ast::InlineStructOrUnion>();
			if(struct_or_union.is_union) {
				fprintf(dest, "union");
			} else {
				fprintf(dest, "struct");
			}
			bool name_on_top = (indentation_level == 0) && (struct_or_union.storage_class != ast::StorageClass::TYPEDEF);
			if(name_on_top) {
				print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			// Print base classes.
			if(!struct_or_union.base_classes.empty()) {
				fprintf(dest, " :");
				for(const ast::BaseClass& base_class : struct_or_union.base_classes) {
					if(base_class.offset > -1) {
						fprintf(dest, " /* 0x%03x */", base_class.offset);
					}
					fprintf(dest, " %s", base_class.type_name.c_str());
				}
			}
			fprintf(dest, " { // 0x%x\n", struct_or_union.size_bits / 8);
			// Print fields.
			for(const std::unique_ptr<ast::Node>& field : struct_or_union.fields) {
				assert(field.get());
				indent(dest, indentation_level + 1);
				print_cpp_offset(dest, *field.get(), digits_for_offset);
				print_cpp_ast_node(dest, *field.get(), name, indentation_level + 1, digits_for_offset, flags);
				fprintf(dest, ";\n");
			}
			// Print member functions.
			if(!(flags & PRINT_OMIT_MEMBER_FUNCTIONS) && !struct_or_union.member_functions.empty()) {
				bool first = true;
				for(size_t i = 0; i < struct_or_union.member_functions.size(); i++) {
					ast::Function& member_func = struct_or_union.member_functions[i]->as<ast::Function>();
					bool is_special = member_func.name == "__as"
							|| (member_func.name == struct_or_union.name
								&& (member_func.parameters.has_value()
									|| member_func.parameters->size() == 0));
					if((flags & PRINT_INCLUDE_SPECIAL_FUNCTIONS) || !is_special) {
						if(first && !struct_or_union.fields.empty()) {
							indent(dest, indentation_level + 1);
							fprintf(dest, "\n");
							first = false;
						}
						assert(struct_or_union.member_functions[i].get());
						indent(dest, indentation_level + 1);
						print_cpp_ast_node(dest, *struct_or_union.member_functions[i].get(), name, indentation_level + 1, digits_for_offset, flags);
						fprintf(dest, ";\n");
					}
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
			print_cpp_ast_node(dest, *pointer.value_type.get(), name, indentation_level, digits_for_offset, flags);
			print_cpp_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			break;
		}
		case ast::REFERENCE: {
			const ast::Reference& reference = node.as<ast::Reference>();
			assert(reference.value_type.get());
			name.pointer_chars.emplace_back('&');
			print_cpp_ast_node(dest, *reference.value_type.get(), name, indentation_level, digits_for_offset, flags);
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
