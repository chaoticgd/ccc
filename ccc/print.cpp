#include "ccc.h"

static void indent(FILE* output, s32 depth);

void print_ast_begin(FILE* output) {
	printf("\n");
	fprintf(output, "struct ccc_int128 {\n");
	fprintf(output, "\tlong int lo;\n");
	fprintf(output, "\tlong int hi;\n");
	fprintf(output, "};\n");
}

void print_forward_declarations(FILE* output, const std::vector<AstNode>& ast_nodes) {
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

void print_ast_node(FILE* output, const AstNode& node, s32 depth, s32 absolute_parent_offset) {
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
				print_ast_node(output, child, depth + 1, absolute_parent_offset + node.offset);
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

static void indent(FILE* output, s32 depth) {
	for(s32 i = 0; i < depth; i++) {
		fprintf(output, "\t");
	}
}
