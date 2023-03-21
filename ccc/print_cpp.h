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
	bool make_globals_extern : 1 = false;
	bool skip_statics : 1 = false;
	bool print_offsets_and_sizes : 1 = true;
	bool print_function_bodies : 1 = true;
	bool print_storage_information : 1 = true;
	bool print_variable_data : 1 = false;
	bool omit_this_parameter : 1 = false;
	bool substitute_parameter_lists : 1 = false;
	bool skip_member_functions_outside_types : 1 = false;
	s32 digits_for_offset = 3;
	const std::map<s32, std::span<char>>* function_bodies = nullptr;
	
	bool last_wants_spacing = false;
	bool has_anything_been_printed = false;
	
	CppPrinter(FILE* o) : out(o) {}
	
	void comment_block_beginning(const fs::path& input_file);
	void comment_block_compiler_version_info(const mdebug::SymbolTable& symbol_table);
	void comment_block_builtin_types(const std::vector<std::unique_ptr<ast::Node>>& ast_nodes);
	void comment_block_file(const char* path);
	void begin_include_guard(const char* macro);
	void end_include_guard(const char* macro);
	void include_directive(const char* path);
	bool data_type(const ast::Node& node);
	void global_variable(const ast::Variable& node);
	void function(const ast::FunctionDefinition& node);
	
	void ast_node(const ast::Node& node, VariableName& parent_name, s32 indentation_level);
	void print_variable_storage_comment(const ast::VariableStorage& storage);
};

}

#endif
