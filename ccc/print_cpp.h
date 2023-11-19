#pragma once

#include "symbol_table.h"

namespace ccc {
	
struct VariableName {
	const std::string* identifier = nullptr;
	std::vector<s8> pointer_chars;
	std::vector<s32> array_indices;
};

struct CppPrinterConfig {
	bool make_globals_extern : 1 = false;
	bool skip_statics : 1 = false;
	bool print_offsets_and_sizes : 1 = true;
	bool print_function_bodies : 1 = true;
	bool print_storage_information : 1 = true;
	bool print_variable_data : 1 = false;
	bool omit_this_parameter : 1 = false;
	bool substitute_parameter_lists : 1 = false;
	bool skip_member_functions_outside_types : 1 = false;
};

class CppPrinter {
public:
	FILE* out;
	const std::map<u32, std::span<char>>* function_bodies = nullptr;
	
	CppPrinter(FILE* o, const CppPrinterConfig& config)
		: out(o)
		, m_config(config) {}
	
	void comment_block_beginning(const char* input_file);
	void comment_block_toolchain_version_info(const SymbolTable& symbol_table);
	void comment_block_builtin_types(const SymbolList<DataType>& data_types);
	void comment_block_file(const char* path);
	void begin_include_guard(const char* macro);
	void end_include_guard(const char* macro);
	void include_directive(const char* path);
	
	bool data_type(const DataType& symbol);
	void function(const Function& symbol, const SymbolTable& symbol_table);
	void global_variable(const GlobalVariable& symbol);

protected:
	void ast_node(const ast::Node& node, VariableName& parent_name, s32 indentation_level);
	void variable_storage_comment(const Variable::Storage& storage);
	void offset(const ast::Node& node);

	CppPrinterConfig m_config;
	s32 m_digits_for_offset = 3;
	bool m_last_wants_spacing = false;
	bool m_has_anything_been_printed = false;
};

}
