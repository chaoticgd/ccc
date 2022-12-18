#ifndef _CCC_PRINT_GHIDRA_H
#define _CCC_PRINT_GHIDRA_H

#include "ast.h"

namespace ccc {

void print_ghidra_prologue(FILE* dest, const fs::path& input_file);
void print_ghidra_types(FILE* dest, const std::vector<std::unique_ptr<ast::Node>>& ast_nodes, const std::map<std::string, s32>& type_lookup);
void print_ghidra_functions(FILE* dest);
void print_ghidra_epilogue(FILE* dest);

};

#endif
