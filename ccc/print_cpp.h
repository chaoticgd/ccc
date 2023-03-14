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
	bool filter_out_types_mapped_to_one_file : 1 = false;
	bool filter_out_types_probably_defined_in_cpp_file : 1 = false;
	bool filter_out_types_probably_defined_in_h_file : 1 = false;
	s32 digits_for_offset = 3;
	s32 only_print_out_types_from_this_file = -1;
	
	CppPrinter(FILE* o) : out(o) {}
	
	void print_cpp_comment_block_beginning(const fs::path& input_file);
	void print_cpp_comment_block_compiler_version_info(const mdebug::SymbolTable& symbol_table);
	void print_cpp_comment_block_builtin_types(const std::vector<std::unique_ptr<ast::Node>>& ast_nodes);
	s32 print_cpp_ast_nodes(const std::vector<std::unique_ptr<ast::Node>>& nodes);
	bool print_cpp_ast_node(const ast::Node& node, VariableName& parent_name, s32 indentation_level);
	void print_variable_storage_comment(const ast::VariableStorage& storage);
};





}

#endif
