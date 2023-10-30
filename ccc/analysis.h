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

// Perform all the main analysis passes on the mdebug symbol table and convert
// it to a set of C++ ASTs.
Result<HighSymbolTable> analyse(const mdebug::SymbolTable& symbol_table, u32 flags, s32 file_descriptor_index = -1);

// Build a map of type names to their index is the deduplicated_types array.
std::map<std::string, s32> build_type_name_to_deduplicated_type_index_map(const HighSymbolTable& symbol_table);

// Lookup a type by its STABS type number. If that fails, and the
// type_name_to_deduplicated_type_index argument isn't a null pointer, try to
// lookup the type by its name. On success return the index of the type in the
// deduplicated_types array, otherwise return -1.
s32 lookup_type(const ast::TypeName& type_name, const HighSymbolTable& symbol_table, const std::map<std::string, s32>* type_name_to_deduplicated_type_index);

// Try to add pointers from member function declarations to their definitions
// using a heuristic.
void fill_in_pointers_to_member_function_definitions(HighSymbolTable& high);

};

#endif
