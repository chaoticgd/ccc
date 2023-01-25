#ifndef _CCC_PRINT_CPP_H
#define _CCC_PRINT_CPP_H

#include "analysis.h"

namespace ccc {

struct VariableName {
	const StringPointer* identifier;
	std::vector<s8> pointer_chars;
	std::vector<s32> array_indices;
};

void print_cpp_comment_block_beginning(FILE* dest, const fs::path& input_file);
void print_cpp_comment_block_compiler_version_info(FILE* dest, const mdebug::SymbolTable& symbol_table);
void print_cpp_comment_block_builtin_types(FILE* dest, const std::vector<std::unique_ptr<ast::Node>>& ast_nodes);
void print_cpp_ast_nodes(FILE* dest, const std::vector<std::unique_ptr<ast::Node>>& nodes, bool verbose);
void print_cpp_ast_node(FILE* dest, const ast::Node& node, VariableName& parent_name, s32 indentation_level, s32 digits_for_offset);
void print_variable_storage_comment(FILE* dest, const ast::VariableStorage& storage);

}

#endif
