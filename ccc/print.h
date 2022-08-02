#include "util.h"
#include "ast.h"

namespace ccc {

enum class OutputLanguage {
	CPP, JSON
};

void print_ast(FILE* output, const std::vector<AstNode>& ast_nodes, OutputLanguage language, bool verbose);

void print_c_ast_begin(FILE* output);
void print_c_forward_declarations(FILE* output, const std::vector<AstNode>& ast_nodes);
void print_c_ast_node(FILE* output, const AstNode& node, s32 depth, s32 absolute_parent_offset);

}
