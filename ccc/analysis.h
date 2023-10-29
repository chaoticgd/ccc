#ifndef _CCC_IR_H
#define _CCC_IR_H

#include "ast.h"
#include "stabs.h"
#include "mdebug.h"
#include "symbols.h"

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

Result<HighSymbolTable> analyse(const mdebug::SymbolTable& symbol_table, u32 flags, s32 file_descriptor_index = -1);
Result<void> analyse_file(HighSymbolTable& high, ast::TypeDeduplicatorOMatic& deduplicator, const mdebug::SymbolTable& symbol_table, const mdebug::SymFileDescriptor& fd, const std::map<std::string, const mdebug::Symbol*>& globals, s32 file_index, u32 flags);
std::optional<ast::GlobalVariableLocation> symbol_class_to_global_variable_location(mdebug::SymbolClass symbol_class);

void compute_size_bytes_recursive(ast::Node& node, const HighSymbolTable& high);
void fill_in_pointers_to_member_function_definitions(HighSymbolTable& high);

};

#endif
