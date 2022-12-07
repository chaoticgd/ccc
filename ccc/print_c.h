#ifndef _CCC_PRINT_H
#define _CCC_PRINT_H

#include "ast.h"

namespace ccc::print {

struct VariableName {
	const std::string* identifier;
	s32 pointer_count = 0;
};

void print_ast_node_as_c(FILE* dest, const ast::Node& node, VariableName& parent_name, s32 indentation_level = 0);

}

#endif
