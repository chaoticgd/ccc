#include "old_print.h"

namespace ccc {

static void indent(FILE* output, s32 depth);

static void print_json_ast_node(FILE* output, const AstNode& node, s32 depth);

void print_ast(FILE* output, const std::vector<AstNode>& ast_nodes, OutputLanguage language, bool verbose) {
	switch(language) {
		case OutputLanguage::CPP: {
			print_c_forward_declarations(stdout, ast_nodes);
			print_c_ast_begin(stdout);
			bool last_node_is_not_typedef = true;
			for(const AstNode& node : ast_nodes) {
				bool node_is_not_typedef = node.descriptor != AstNodeDescriptor::TYPEDEF;
				if(node_is_not_typedef || last_node_is_not_typedef) {
					printf("\n");
				}
				last_node_is_not_typedef = node_is_not_typedef;
				
				assert(node.symbol);
				if(verbose) {
					printf("// %s\n", node.name.c_str());
				}
				if(node.conflicting_types) {
					printf("// warning: multiple differing types with the same name, only one recovered\n");
				}
				if(verbose) {
					printf("// symbol:\n");
					printf("//   %s\n", node.symbol->raw.c_str());
					printf("// used by:\n");
					for(const std::string& source_file : node.source_files) {
						printf("//   %s\n", source_file.c_str());
					}
				}
				print_c_ast_node(stdout, node, 0, 0);
			}
			break;
		}
		case OutputLanguage::JSON: {
			fprintf(output, "[\n");
			for(size_t i = 0; i < ast_nodes.size(); i++) {
				bool is_last = i == ast_nodes.size() - 1;
				print_json_ast_node(output, ast_nodes[i], 1);
				if(!is_last) {
					fprintf(output, ",");
				}
				fprintf(output, "\n");
			}
			fprintf(output, "]\n");
		}
	}
}

void print_c_ast_begin(FILE* output) {
	printf("\n");
}

void print_c_forward_declarations(FILE* output, const std::vector<AstNode>& ast_nodes) {
	for(const AstNode& node : ast_nodes) {
		bool print = true;
		switch(node.descriptor) {
			case AstNodeDescriptor::ENUM: fprintf(output, "enum"); break;
			case AstNodeDescriptor::STRUCT: fprintf(output, "struct"); break;
			case AstNodeDescriptor::UNION: fprintf(output, "union"); break;
			default:
				print = false;
		}
		if(print) {
			fprintf(output, " %s;\n", node.name.c_str());
		}
	}
}

void print_c_ast_node(FILE* output, const AstNode& node, s32 depth, s32 absolute_parent_offset) {
	indent(output, depth);
	if(node.is_static) {
		fprintf(output, "static ");
	}
	switch(node.descriptor) {
		case AstNodeDescriptor::LEAF: {
			if(!node.is_static) {
				fprintf(output, "/* %3x */ ", (absolute_parent_offset + node.offset) / 8);
			}
			if(node.leaf.type_name.size() > 0) {
				fprintf(output, "%s", node.leaf.type_name.c_str());
			} else {
				fprintf(output, "/* error: empty type string */ int");
			}
			break;
		}
		case AstNodeDescriptor::ENUM: {
			if(node.name.empty()) {
				fprintf(output, "enum {\n");
			} else {
				fprintf(output, "enum %s {\n", node.name.c_str());
			}
			for(auto& [value, field_name] : node.enum_type.fields) {
				bool is_last = value == node.enum_type.fields.back().first;
				indent(output, depth + 1);
				fprintf(output, "%s = %d%s\n", field_name.c_str(), value, is_last ? "" : ",");
			}
			indent(output, depth);
			printf("}");
			break;
		}
		case AstNodeDescriptor::STRUCT:
		case AstNodeDescriptor::UNION: {
			if(node.descriptor == AstNodeDescriptor::STRUCT) {
				fprintf(output, "struct");
			} else {
				fprintf(output, "union");
			}
			fprintf(output, " %s", node.name.c_str());
			const std::vector<AstBaseClass>& base_classes = node.struct_or_union.base_classes;
			if(base_classes.size() > 0) {
				fprintf(output, " :");
				for(size_t i = 0; i < base_classes.size(); i++) {
					const AstBaseClass& base_class = base_classes[i];
					fprintf(output, " /* %x */ %s", base_class.offset, base_class.type_name.c_str());
					if(i != base_classes.size() - 1) {
						fprintf(output, ",");
					}
				}
			}
			fprintf(output, " {\n");
			for(const AstNode& child : node.struct_or_union.fields) {
				print_c_ast_node(output, child, depth + 1, absolute_parent_offset + node.offset);
			}
			indent(output, depth);
			printf("}");
			break;
		}
		case AstNodeDescriptor::TYPEDEF: {
			printf("typedef %s", node.typedef_type.type_name.c_str());
			fprintf(output, " %s", node.name.c_str());
			break;
		}
	}
	if(!node.top_level) {
		fprintf(output, " %s", node.name.c_str());
	}
	for(s32 index : node.array_indices) {
		fprintf(output, "[%d]", index);
	}
	fprintf(output, ";\n");
}

static void print_json_ast_node(FILE* output, const AstNode& node, s32 depth) {
	indent(output, depth); fprintf(output, "{\n");
	if(node.is_static) {
		indent(output, depth + 1); fprintf(output, "\"is_static\": true,\n");
	} else {
		indent(output, depth + 1); fprintf(output, "\"is_static\": false,\n");
		indent(output, depth + 1); fprintf(output, "\"offset\": %d,\n", node.offset);
		indent(output, depth + 1); fprintf(output, "\"size\": %d,\n", node.size);
	}
	indent(output, depth + 1); fprintf(output, "\"name\": \"%s\",\n", node.name.c_str());
	switch(node.descriptor) {
		case AstNodeDescriptor::LEAF: {
			indent(output, depth + 1); fprintf(output, "\"type\": \"leaf\",\n");
			indent(output, depth + 1); fprintf(output, "\"type_name\": \"%s\",\n", node.leaf.type_name.c_str());
			break;
		}
		case AstNodeDescriptor::ENUM: {
			indent(output, depth + 1); fprintf(output, "\"type\": \"leaf\",\n");
			break;
		}
		case AstNodeDescriptor::STRUCT:
		case AstNodeDescriptor::UNION: {
			bool is_struct = node.descriptor == AstNodeDescriptor::STRUCT;
			indent(output, depth + 1); fprintf(output, "\"type\": \"%s\",\n", is_struct ? "struct" : "union");
			indent(output, depth + 1); fprintf(output, "\"base_classes\": [\n");
			const auto& base_classes = node.struct_or_union.base_classes;
			for(size_t i = 0; i < base_classes.size(); i++) {
				bool is_last = i == base_classes.size() - 1;
				const AstBaseClass& base_class = base_classes[i];
				indent(output, depth + 2); fprintf(output, "{\n");
				indent(output, depth + 3); fprintf(output, "\"offset\": %d,\n", base_class.offset);
				indent(output, depth + 3); fprintf(output, "\"type_name\": \"%s\"\n", base_class.type_name.c_str());
				indent(output, depth + 2); fprintf(output, "}%s\n", is_last ? "" : ",");
			}
			indent(output, depth + 1); fprintf(output, "],\n");
			indent(output, depth + 1); fprintf(output, "\"fields\": [\n");
			for(size_t i = 0; i < node.struct_or_union.fields.size(); i++) {
				bool is_last = i == node.struct_or_union.fields.size() - 1;
				const AstNode& child_node = node.struct_or_union.fields[i];
				print_json_ast_node(output, child_node, depth + 2);
				if(!is_last) {
					fprintf(output, ",");
				}
				fprintf(output, "\n");
			}
			indent(output, depth + 1); fprintf(output, "],\n");
			break;
		}
		case AstNodeDescriptor::TYPEDEF: {
			indent(output, depth + 1); fprintf(output, "\"type\": \"typedef\",\n");
			indent(output, depth + 1); fprintf(output, "\"type_name\": \"%s\",\n", node.typedef_type.type_name.c_str());
			break;
		}
	}
	if(node.symbol) {
		indent(output, depth + 1); fprintf(output, "\"stabs_symbol\": \"%s\",\n", node.symbol->raw.c_str());
	}
	indent(output, depth + 1); fprintf(output, "\"conflicting_types\": %s\n", node.conflicting_types ? "true" : "false");
	indent(output, depth); fprintf(output, "}");
}

static void indent(FILE* output, s32 depth) {
	for(s32 i = 0; i < depth; i++) {
		fprintf(output, "\t");
	}
}

}
