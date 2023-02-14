#ifndef _CCC_PRINT_CPP_H
#define _CCC_PRINT_CPP_H

#include "analysis.h"

namespace ccc {

struct PrintCppConfig {
	bool verbose = false;
	bool force_extern = false;
	bool skip_static_variables = false;
	bool print_offsets = true;
	bool print_function_bodies = true;
	bool print_storage_information = true;
	s32 digits_for_offset = 3;
};

struct VariableName {
	const std::string* identifier = nullptr;
	std::vector<s8> pointer_chars;
	std::vector<s32> array_indices;
};

void print_cpp_comment_block_beginning(FILE* out, const fs::path& input_file);
void print_cpp_comment_block_compiler_version_info(FILE* out, const mdebug::SymbolTable& symbol_table);
void print_cpp_comment_block_builtin_types(FILE* out, const std::vector<std::unique_ptr<ast::Node>>& ast_nodes);
void print_cpp_ast_nodes(FILE* out, const std::vector<std::unique_ptr<ast::Node>>& nodes, const PrintCppConfig& config);
bool print_cpp_ast_node(FILE* out, const ast::Node& node, VariableName& parent_name, s32 indentation_level, const PrintCppConfig& config);
void print_variable_storage_comment(FILE* out, const ast::VariableStorage& storage, const PrintCppConfig& config);

}

#endif
