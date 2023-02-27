#ifndef _CCC_PRINT_CPP_H
#define _CCC_PRINT_CPP_H

#include "analysis.h"

namespace ccc {

struct PrintCppConfig {
	bool verbose : 1 = false;
	bool force_extern : 1 = false;
	bool skip_statics : 1 = false;
	bool print_offsets : 1 = true;
	bool print_function_bodies : 1 = true;
	bool print_storage_information : 1 = true;
	bool filter_out_types_mapped_to_one_file : 1 = false;
	bool filter_out_types_not_mapped_to_one_file : 1 = false;
	s32 digits_for_offset = 3;
	s32 only_print_out_types_from_this_file = -1;
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
