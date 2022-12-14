#ifndef _CCC_PRINT_CPP_H
#define _CCC_PRINT_CPP_H

#include "ast.h"

namespace ccc {

void print_cpp_comment_block_beginning(FILE* dest, const fs::path& input_file);
void print_cpp_comment_block_compiler_version_info(FILE* dest, const SymbolTable& symbol_table);
void print_cpp_comment_block_builtin_types(FILE* dest, const std::set<std::pair<std::string, RangeClass>>& builtins);
void print_cpp_ast_nodes(FILE* dest, const std::vector<std::unique_ptr<ast::Node>>& nodes, bool verbose);

}

#endif
