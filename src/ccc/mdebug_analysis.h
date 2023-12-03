// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "mdebug_section.h"
#include "stabs.h"
#include "stabs_to_ast.h"
#include "symbols.h"
#include "symbol_database.h"
#include "symbol_table.h"

namespace ccc::mdebug {
	
struct AnalysisContext {
	const mdebug::SymbolTableReader* reader;
	const std::map<std::string, const mdebug::Symbol*>* globals;
	SymbolSourceHandle symbol_source;
	u32 parser_flags;
	DemanglerFunc* demangle;
};

class LocalSymbolTableAnalyser {
public:
	LocalSymbolTableAnalyser(SymbolDatabase& database, const StabsToAstState& stabs_to_ast_state, const AnalysisContext& context, SourceFile& source_file)
		: m_database(database)
		, m_context(context)
		, m_stabs_to_ast_state(stabs_to_ast_state)
		, m_source_file(source_file) {}
	
	// Functions for processing individual symbols.
	//
	// In most cases these symbols will appear in the following order:
	//   PROC TEXT
	//   ... line numbers ... ($LM<N>)
	//   END TEXT
	//   LABEL TEXT FUN
	//   ... parameters ...
	//   ... blocks ... (... local variables ... LBRAC ... subblocks ... RBRAC)
	//   NIL NIL FUN
	//
	// For some compiler versions the symbols can appear in this order:
	//   LABEL TEXT FUN
	//   ... parameters ...
	//   first line number ($LM1)
	//   PROC TEXT
	//   ... line numbers ... ($LM<N>)
	//   END TEXT
	//   ... blocks ... (... local variables ... LBRAC ... subblocks ... RBRAC)
	[[nodiscard]] Result<void> stab_magic(const char* magic);
	[[nodiscard]] Result<void> source_file(const char* path, Address text_address);
	[[nodiscard]] Result<void> data_type(const ParsedSymbol& symbol);
	[[nodiscard]] Result<void> global_variable(const char* mangled_name, Address address, const StabsType& type, bool is_static, Variable::GlobalStorage::Location location);
	[[nodiscard]] Result<void> sub_source_file(const char* name, Address text_address);
	[[nodiscard]] Result<void> procedure(const char* mangled_name, Address address, bool is_static);
	[[nodiscard]] Result<void> label(const char* label, Address address, s32 line_number);
	[[nodiscard]] Result<void> text_end(const char* name, s32 function_size);
	[[nodiscard]] Result<void> function(const char* mangled_name, const StabsType& return_type, Address address);
	[[nodiscard]] Result<void> function_end();
	[[nodiscard]] Result<void> parameter(const char* name, const StabsType& type, bool is_stack_variable, s32 offset_or_register, bool is_by_reference);
	[[nodiscard]] Result<void> local_variable(const char* name, const StabsType& type, const Variable::Storage& storage, bool is_static);
	[[nodiscard]] Result<void> lbrac(s32 begin_offset);
	[[nodiscard]] Result<void> rbrac(s32 end_offset);
	
	[[nodiscard]] Result<void> finish();
	
	[[nodiscard]] Result<void> create_function(const char* mangled_name, Address address);
	
	std::optional<std::string> demangle_name(const char* name);
	
protected:
	enum AnalysisState {
		NOT_IN_FUNCTION,
		IN_FUNCTION_BEGINNING,
		IN_FUNCTION_END
	};
	
	SymbolDatabase& m_database;
	const AnalysisContext& m_context;
	const StabsToAstState& m_stabs_to_ast_state;
	
	AnalysisState m_state = NOT_IN_FUNCTION;
	SourceFile& m_source_file;
	DataTypeRange m_data_types;
	FunctionRange m_functions;
	GlobalVariableRange m_global_variables;
	Function* m_current_function = nullptr;
	ParameterVariableRange m_current_parameter_variables;
	LocalVariableRange m_current_local_variables;
	std::vector<std::vector<LocalVariableHandle>> m_blocks;
	std::vector<LocalVariableHandle> m_pending_local_variables;
	std::string m_next_relative_path;
};

std::optional<Variable::GlobalStorage::Location> symbol_class_to_global_variable_location(SymbolClass symbol_class);

};
