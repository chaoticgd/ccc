#ifndef _CCC_PRINT_H
#define _CCC_PRINT_H

#include "ast.h"

namespace ccc {

enum PrintFlags {
	NO_PRINT_FLAGS = 0,
	PRINT_VERBOSE = (1 << 0),
	PRINT_OMIT_MEMBER_FUNCTIONS = (1 << 1)
};

void print_cpp_ast_nodes(FILE* dest, const std::vector<std::unique_ptr<ast::Node>>& nodes, u32 flags);

}

#endif
