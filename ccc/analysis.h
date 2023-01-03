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

struct Function {
	std::string name;
	std::unique_ptr<ast::Node> return_type;
	std::vector<ast::Variable> parameters;
	ast::Scope body;
	u32 address = 0;
};

struct TranslationUnit {
	std::string full_path;
	std::vector<std::unique_ptr<ast::Node>> types;
	std::vector<std::unique_ptr<ast::Node>> functions_and_globals;
	std::vector<ParsedSymbol> symbols;
	u32 text_address = 0;
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

mdebug::SymbolTable read_symbol_table(const std::vector<Module*>& modules);
AnalysisResults analyse(const mdebug::SymbolTable& symbol_table, u32 flags, s32 file_descriptor_index = -1);
void analyse_file(AnalysisResults& results, const mdebug::SymbolTable& symbol_table, const mdebug::SymFileDescriptor& fd, u32 flags);

};

#endif
