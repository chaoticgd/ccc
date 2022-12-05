#ifndef _CCC_PRINT_H
#define _CCC_PRINT_H

#include "ast.h"

namespace ccc::print {

void print_ast_node_as_c(FILE* dest, const ast::Node& node, s32 indentation_level = 0);

}

#endif
