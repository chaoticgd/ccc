#ifndef _CCC_PRINT_CPP_H
#define _CCC_PRINT_CPP_H

#include "analysis.h"

namespace ccc {
	
struct VariableName {
	const std::string* identifier = nullptr;
	std::vector<s8> pointer_chars;
	std::vector<s32> array_indices;
};

struct CppPrinter {
	FILE* out;
	bool verbose : 1 = false;
	bool force_extern : 1 = false;
	bool skip_statics : 1 = false;
	bool print_offsets_and_sizes : 1 = true;
	bool print_function_bodies : 1 = true;
	bool print_storage_information : 1 = true;
	s32 digits_for_offset = 3;
	const std::map<s32, std::span<char>>* function_bodies = nullptr;
	
	bool last_type_was_multiline = true;
	
	CppPrinter(FILE* o) : out(o) {}
	
	void print_cpp_comment_block_beginning(const fs::path& input_file);
	void print_cpp_comment_block_compiler_version_info(const mdebug::SymbolTable& symbol_table);
	void print_cpp_comment_block_builtin_types(const std::vector<std::unique_ptr<ast::Node>>& ast_nodes);
	bool top_level_type(const ast::Node& node, bool is_last);
	bool ast_node(const ast::Node& node, VariableName& parent_name, s32 indentation_level);
	void print_variable_storage_comment(const ast::VariableStorage& storage);
};

}

#endif
