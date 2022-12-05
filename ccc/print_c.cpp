#include "print_c.h"

namespace ccc::print {

static void print_storage_class(FILE* dest, ast::StorageClass storage_class);
static void indent(FILE* dest, s32 level);

void print_ast_node_as_c(FILE* dest, const ast::Node& node, s32 indentation_level) {
	switch(node.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = node.as<const ast::Array>();
			print_storage_class(dest, array.storage_class);
			assert(array.element_type.get());
			print_ast_node_as_c(dest, *array.element_type.get(), indentation_level);
			fprintf(dest, "%s[%d]", array.name.c_str(), array.element_count);
			break;
		}
		case ast::FUNCTION_POINTER: {
			const ast::FunctionPointer& function_pointer = node.as<const ast::FunctionPointer>();
			assert(function_pointer.return_type.get());
			print_ast_node_as_c(dest, *function_pointer.return_type.get(), indentation_level);
			fprintf(dest, " (*%s)(", function_pointer.name.c_str());
			for(size_t i = 0; i < function_pointer.parameter_types.size(); i++) {
				assert(function_pointer.parameter_types[i].get());
				print_ast_node_as_c(dest, *function_pointer.parameter_types[i].get(), indentation_level);
				if(i != function_pointer.parameter_types.size() - 1) {
					fprintf(dest, ", ");
				}
			}
			fprintf(dest, ")");
			break;
		}
		case ast::INLINE_ENUM: {
			const ast::InlineEnum& inline_enum = node.as<const ast::InlineEnum>();
			fprintf(dest, "enum {\n");
			indent(dest, indentation_level);
			fprintf(dest, "}\n");
			break;
		}
		case ast::INLINE_STRUCT: {
			const ast::InlineStruct& inline_struct = node.as<const ast::InlineStruct>();
			assert(!inline_struct.name.empty());
			print_storage_class(dest, inline_struct.storage_class);
			fprintf(dest, "struct");
			bool name_on_top = indentation_level == 0 && inline_struct.storage_class != ast::StorageClass::TYPEDEF;
			if(name_on_top) {
				fprintf(dest, " %s", inline_struct.name.c_str());
			}
			fprintf(dest, " {\n");
			for(const std::unique_ptr<ast::Node>& field : inline_struct.fields) {
				indent(dest, indentation_level + 1);
				assert(field.get());
				print_ast_node_as_c(dest, *field.get(), indentation_level + 1);
				fprintf(dest, ";\n");
			}
			indent(dest, indentation_level);
			fprintf(dest, "}");
			if(!name_on_top) {
				fprintf(dest, " %s", inline_struct.name.c_str());
			}
			break;
		}
		case ast::INLINE_UNION: {
			const ast::InlineUnion& inline_union = node.as<const ast::InlineUnion>();
			print_storage_class(dest, inline_union.storage_class);
			fprintf(dest, "union");
			bool name_on_top = indentation_level == 0 && inline_union.storage_class != ast::StorageClass::TYPEDEF;
			if(name_on_top) {
				fprintf(dest, "%s%s", inline_union.name.empty() ? "" : " ", inline_union.name.c_str());
			}
			fprintf(dest, " {\n");
			for(const std::unique_ptr<ast::Node>& field : inline_union.fields) {
				indent(dest, indentation_level + 1);
				assert(field.get());
				print_ast_node_as_c(dest, *field.get(), indentation_level + 1);
				fprintf(dest, ";\n");
			}
			indent(dest, indentation_level);
			fprintf(dest, "}");
			if(!name_on_top) {
				fprintf(dest, "%s%s", inline_union.name.empty() ? "" : " ", inline_union.name.c_str());
			}
			break;
		}
		case ast::POINTER: {
			const ast::Pointer& pointer = node.as<const ast::Pointer>();
			assert(pointer.value_type.get());
			print_ast_node_as_c(dest, *pointer.value_type.get(), indentation_level);
			fprintf(dest, "*");
			fprintf(dest, "%s%s%s", pointer.name.c_str(), pointer.name.empty() ? "" : " ", pointer.name.c_str());
			break;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = node.as<const ast::TypeName>();
			fprintf(dest, "%s%s%s", type_name.type_name.c_str(), type_name.name.empty() ? "" : " ", type_name.name.c_str());
			break;
		}
	}
}

static void print_storage_class(FILE* dest, ast::StorageClass storage_class) {
	switch(storage_class) {
		case ast::StorageClass::NONE: fputs("", dest); break;
		case ast::StorageClass::TYPEDEF: fputs("typedef ", dest); break;
		case ast::StorageClass::EXTERN: fputs("extern ", dest); break;
		case ast::StorageClass::STATIC: fputs("static ", dest); break;
		case ast::StorageClass::AUTO: fputs("auto ", dest); break;
		case ast::StorageClass::REGISTER: fputs("register ", dest); break;
	}
}

static void indent(FILE* dest, s32 level) {
	for(s32 i = 0; i < level; i++) {
		fputc('\t', dest);
	}
}

}
