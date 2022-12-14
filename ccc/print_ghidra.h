#ifndef _CCC_PRINT_GHIDRA_H
#define _CCC_PRINT_GHIDRA_H

#include "ast.h"

namespace ccc {

void print_ghidra_prologue(FILE* dest, const fs::path& input_file);
void print_ghidra_types(FILE* dest, const std::vector<ast::Node>& ast_nodes);
void print_ghidra_epilogue(FILE* dest);

};

#endif
