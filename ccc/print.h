#ifndef _CCC_PRINT_H
#define _CCC_PRINT_H

#include "ast.h"

namespace ccc {

enum class OutputLanguage {
	CPP, JSON
};

void print_ast_nodes(FILE* dest, const std::vector<std::unique_ptr<ast::Node>>& nodes, OutputLanguage language, bool verbose);

}

#endif
