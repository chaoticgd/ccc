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

enum class VariableStorageLocation {
	REGISTER,
	STACK
};

struct VariableStorage {
	VariableStorageLocation location;
	mips::RegisterClass register_class = mips::RegisterClass::GPR;
	s32 dbx_register_number = -1;
	s32 register_index_relative = -1;
	s32 stack_pointer_offset = -1;
};

struct Parameter {
	std::string name;
	std::unique_ptr<ast::Node> type;
	VariableStorage storage;
};

struct LocalVariable {
	std::string name;
	std::unique_ptr<ast::Node> type;
	VariableStorage storage;
};

struct Function {
	std::string name;
	std::unique_ptr<ast::Node> return_type;
	std::vector<Parameter> parameters;
	std::vector<LocalVariable> locals;
	u32 address = 0;
};

struct GlobalVariable {
	std::string name;
	std::unique_ptr<ast::Node> type;
};

struct TranslationUnit {
	std::string full_path;
	std::vector<Function> functions;
	std::vector<GlobalVariable> globals;
	std::vector<std::unique_ptr<ast::Node>> types;
	std::vector<ParsedSymbol> symbols;
};

struct AnalysisResults {
	std::vector<TranslationUnit> translation_units;
	std::vector<std::unique_ptr<ast::Node>> deduplicated_types;
};

enum AnalysisFlags {
	NO_ANALYSIS_FLAGS = 0,
	SKIP_FUNCTION_ANALYSIS = (1 << 0),
	DEDUPLICATE_TYPES = (1 << 1),
	STRIP_MEMBER_FUNCTIONS = (1 << 2),
	STRIP_GENERATED_FUNCTIONS = (1 << 3)
};

SymbolTable read_symbol_table(const std::vector<Module*>& modules);
AnalysisResults analyse(const SymbolTable& symbol_table, u32 flags, s32 file_descriptor_index = -1);
void analyse_file(AnalysisResults& results, const SymbolTable& symbol_table, const SymFileDescriptor& fd, u32 flags);
std::map<u32, Function> scan_for_functions(u32 address, std::span<mips::Insn> insns);

};

#endif
