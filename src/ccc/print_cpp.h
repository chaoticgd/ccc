// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "data_refinement.h"
#include "symbol_database.h"

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
	
	void comment_block_beginning(const char* input_file, const char* tool_name, const char* tool_version);
	void comment_block_toolchain_version_info(const SymbolDatabase& database);
	void comment_block_builtin_types(const SymbolDatabase& database, SourceFileHandle file = SourceFileHandle());
	void comment_block_file(const char* path);
	void begin_include_guard(const char* macro);
	void end_include_guard(const char* macro);
	void include_directive(const char* path);
	
	bool data_type(const DataType& symbol, const SymbolDatabase& database);
	void function(const Function& symbol, const SymbolDatabase& database, const ReadVirtualFunc* read_virtual);
	void global_variable(
		const GlobalVariable& symbol, const SymbolDatabase& database, const ReadVirtualFunc* read_virtual);
	
protected:
	void ast_node(
		const ast::Node& node,
		VariableName& parent_name,
		s32 base_offset,
		s32 indentation_level,
		const SymbolDatabase& database,
		bool print_body = true);
	void function_parameters(std::vector<const ParameterVariable*> parameters, const SymbolDatabase& database);
	void refined_data(const RefinedData& data, s32 indentation_level);
	void global_storage_comment(const GlobalStorage& storage, Address address);
	void register_storage_comment(const RegisterStorage& storage);
	void stack_storage_comment(const StackStorage& storage);
	void offset(const ast::Node& node, s32 base_offset);

	CppPrinterConfig m_config;
	s32 m_digits_for_offset = 3;
	bool m_last_wants_spacing = false;
	bool m_has_anything_been_printed = false;
};

}
