#ifndef _CCC_IR_H
#define _CCC_IR_H

#include "ast.h"
#include "insn.h"
#include "stabs.h"
#include "mdebug.h"
#include "module.h"
#include "symbols.h"

namespace ccc {

struct BasicBlock {
	std::string name;
	u32 offset;
	u32 size;
};

struct Function {
	std::string name;
	u32 address;
	u32 size;
	std::vector<BasicBlock> basic_blocks;
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
	std::vector<std::vector<ParsedSymbol>> symbols;
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
