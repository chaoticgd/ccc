#ifndef _CCC_IR_H
#define _CCC_IR_H

#include "ast.h"
#include "insn.h"
#include "stabs.h"
#include "mdebug.h"
#include "module.h"
#include "symbols.h"
#include "registers.h"

namespace ccc {

struct HighSymbolTable {
	std::vector<std::unique_ptr<ast::SourceFile>> source_files;
	std::vector<std::unique_ptr<ast::Node>> deduplicated_types;
};

enum AnalysisFlags {
	NO_ANALYSIS_FLAGS = 0,
	SKIP_FUNCTION_ANALYSIS = (1 << 0),
	DEDUPLICATE_TYPES = (1 << 1),
	STRIP_ACCESS_SPECIFIERS = (1 << 2),
	STRIP_MEMBER_FUNCTIONS = (1 << 3),
	STRIP_GENERATED_FUNCTIONS = (1 << 4)
};

Result<mdebug::SymbolTable> read_symbol_table(Module& mod, const fs::path& input_file);
Result<HighSymbolTable> analyse(const mdebug::SymbolTable& symbol_table, u32 flags, s32 file_descriptor_index = -1);
Result<void> analyse_file(HighSymbolTable& high, ast::TypeDeduplicatorOMatic& deduplicator, const mdebug::SymbolTable& symbol_table, const mdebug::SymFileDescriptor& fd, const std::map<std::string, const mdebug::Symbol*>& globals, s32 file_index, u32 flags);
std::optional<ast::GlobalVariableLocation> symbol_class_to_global_variable_location(mdebug::SymbolClass symbol_class);

struct LocalSymbolTableAnalyser {
	ast::SourceFile& output;
	ast::StabsToAstState& stabs_to_ast_state;
	
	LocalSymbolTableAnalyser(ast::SourceFile& o, ast::StabsToAstState& s)
		: output(o), stabs_to_ast_state(s) {}
	
	enum AnalysisState {
		NOT_IN_FUNCTION,
		IN_FUNCTION_BEGINNING,
		IN_FUNCTION_END
	};
	
	AnalysisState state = NOT_IN_FUNCTION;
	ast::FunctionDefinition* current_function = nullptr;
	ast::FunctionType* current_function_type = nullptr;
	std::vector<ast::Variable*> pending_variables_begin;
	std::map<s32, std::vector<ast::Variable*>> pending_variables_end;
	std::string next_relative_path;
	
	// Functions for processing individual symbols.
	//
	// In most cases these symbols will appear in the following order:
	//   proc
	//   ... line numbers ...
	//   end
	//   func
	//   ... parameters ...
	//   ... blocks ...
	//   
	// For some compiler versions the symbols can appear in this order:
	//   func
	//   ... parameters ...
	//   $LM1
	//   proc
	//   ... line numbers ...
	//   end
	//   ... blocks ...
	Result<void> stab_magic(const char* magic);
	Result<void> source_file(const char* path, s32 text_address);
	Result<void> data_type(const ParsedSymbol& symbol);
	Result<void> global_variable(const char* name, s32 address, const StabsType& type, bool is_static, ast::GlobalVariableLocation location);
	Result<void> sub_source_file(const char* name, s32 text_address);
	Result<void> procedure(const char* name, s32 address, bool is_static);
	Result<void> label(const char* label, s32 address, s32 line_number);
	Result<void> text_end(const char* name, s32 function_size);
	Result<void> function(const char* name, const StabsType& return_type, s32 function_address);
	Result<void> function_end();
	Result<void> parameter(const char* name, const StabsType& type, bool is_stack_variable, s32 offset_or_register, bool is_by_reference);
	Result<void> local_variable(const char* name, const StabsType& type, ast::VariableStorageType storage_type, s32 value, ast::GlobalVariableLocation location, bool is_static);
	Result<void> lbrac(s32 number, s32 begin_offset);
	Result<void> rbrac(s32 number, s32 end_offset);
	
	Result<void> finish();
};

void compute_size_bytes_recursive(ast::Node& node, const HighSymbolTable& high);
void fill_in_pointers_to_member_function_definitions(HighSymbolTable& high);

};

#endif
