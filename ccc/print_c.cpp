#include "print_c.h"

namespace ccc::print {

enum VariableNamePrintFlags {
	NO_PRINT_FLAGS = 0,
	INSERT_SPACE_TO_LEFT = (1 << 0),
	INSERT_SPACE_TO_RIGHT = (1 << 1)
};

static void print_storage_class(FILE* dest, ast::StorageClass storage_class);
static void print_variable_name(FILE* dest, VariableName& name, u32 flags);
static void print_offset(FILE* dest, const ast::Node& node);
static void indent(FILE* dest, s32 level);

void print_ast_node_as_c(FILE* dest, const ast::Node& node, VariableName& parent_name, s32 indentation_level) {
	VariableName this_name{&node.name};
	VariableName& name = node.name.empty() ? parent_name : this_name;
	
	switch(node.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = node.as<ast::Array>();
			print_storage_class(dest, array.storage_class);
			assert(array.element_type.get());
			print_ast_node_as_c(dest, *array.element_type.get(), name, indentation_level);
			fprintf(dest, "[%d]", array.element_count);
			break;
		}
		case ast::BITFIELD: {
			const ast::BitField& bit_field = node.as<ast::BitField>();
			assert(bit_field.underlying_type.get());
			print_ast_node_as_c(dest, *bit_field.underlying_type.get(), name, indentation_level);
			printf(" : %d", bit_field.size_bits);
			break;
		}
		case ast::FUNCTION: {
			const ast::Function& function = node.as<ast::Function>();
			assert(function.return_type.get());
			VariableName dummy{nullptr};
			print_ast_node_as_c(dest, *function.return_type.get(), dummy, indentation_level);
			fprintf(dest, " (");
			print_variable_name(dest, name, NO_PRINT_FLAGS);
			fprintf(dest, ")(");
			if(function.parameter_types.has_value()) {
				for(size_t i = 0; i < function.parameter_types->size(); i++) {
					assert((*function.parameter_types)[i].get());
					print_ast_node_as_c(dest, *(*function.parameter_types)[i].get(), dummy, indentation_level);
					if(i != function.parameter_types->size() - 1) {
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
			bool name_on_top = indentation_level == 0 && inline_enum.storage_class != ast::StorageClass::TYPEDEF;
			if(name_on_top) {
				print_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			printf(" {\n");
			for(size_t i = 0; i < inline_enum.constants.size(); i++) {
				s32 number = inline_enum.constants[i].first;
				const std::string& name = inline_enum.constants[i].second;
				bool is_last = i == inline_enum.constants.size() - 1;
				indent(dest, indentation_level + 1);
				fprintf(dest, "%s = %d%s\n", name.c_str(), number, is_last ? "" : ",");
			}
			fprintf(dest, "}");
			if(!name_on_top) {
				print_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			break;
		}
		case ast::INLINE_STRUCT: {
			const ast::InlineStruct& inline_struct = node.as<ast::InlineStruct>();
			print_storage_class(dest, inline_struct.storage_class);
			fprintf(dest, "struct");
			bool name_on_top = indentation_level == 0 && inline_struct.storage_class != ast::StorageClass::TYPEDEF;
			if(name_on_top) {
				print_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
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
			fprintf(dest, " {\n");
			for(const std::unique_ptr<ast::Node>& field : inline_struct.fields) {
				assert(field.get());
				indent(dest, indentation_level + 1);
				print_offset(dest, *field.get());
				print_ast_node_as_c(dest, *field.get(), name, indentation_level + 1);
				fprintf(dest, ";\n");
			}
			indent(dest, indentation_level);
			fprintf(dest, "}");
			if(!name_on_top) {
				print_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			break;
		}
		case ast::INLINE_UNION: {
			const ast::InlineUnion& inline_union = node.as<ast::InlineUnion>();
			print_storage_class(dest, inline_union.storage_class);
			fprintf(dest, "union");
			bool name_on_top = indentation_level == 0 && inline_union.storage_class != ast::StorageClass::TYPEDEF;
			if(name_on_top) {
				print_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			fprintf(dest, " {\n");
			for(const std::unique_ptr<ast::Node>& field : inline_union.fields) {
				assert(field.get());
				indent(dest, indentation_level + 1);
				print_offset(dest, *field.get());
				print_ast_node_as_c(dest, *field.get(), name, indentation_level + 1);
				fprintf(dest, ";\n");
			}
			indent(dest, indentation_level);
			fprintf(dest, "}");
			if(!name_on_top) {
				print_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			}
			break;
		}
		case ast::POINTER: {
			const ast::Pointer& pointer = node.as<ast::Pointer>();
			assert(pointer.value_type.get());
			name.pointer_count++;
			print_ast_node_as_c(dest, *pointer.value_type.get(), name, indentation_level);
			print_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			break;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = node.as<ast::TypeName>();
			fprintf(dest, "%s", type_name.type_name.c_str());
			print_variable_name(dest, name, INSERT_SPACE_TO_LEFT);
			break;
		}
	}
}

static void print_storage_class(FILE* dest, ast::StorageClass storage_class) {
	switch(storage_class) {
		case ast::StorageClass::NONE: break;
		case ast::StorageClass::TYPEDEF: fprintf(dest, "typedef "); break;
		case ast::StorageClass::EXTERN: fprintf(dest, "extern "); break;
		case ast::StorageClass::STATIC: fprintf(dest, "static "); break;
		case ast::StorageClass::AUTO: fprintf(dest, "auto "); break;
		case ast::StorageClass::REGISTER: fprintf(dest, "register "); break;
	}
}

static void print_variable_name(FILE* dest, VariableName& name, u32 flags) {
	if(name.identifier != nullptr && (flags & INSERT_SPACE_TO_LEFT) && !name.identifier->empty()) {
		fprintf(dest, " ");
	}
	while(name.pointer_count > 0) {
		fprintf(dest, "*");
		name.pointer_count--;
	}
	if(name.identifier != nullptr) {
		fprintf(dest, "%s", name.identifier->c_str());
		name.identifier = nullptr;
		if((flags & INSERT_SPACE_TO_RIGHT) && name.identifier->empty()) {
			fprintf(dest, " ");
		}
	}
}

static void print_offset(FILE* dest, const ast::Node& node) {
	if(node.absolute_offset_bytes > -1) {
		fprintf(dest, "/* 0x%03x", node.absolute_offset_bytes);
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
