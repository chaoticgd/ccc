// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "mdebug_section.h"
#include "symbol_database.h"
#include "symbol_table.h"

namespace ccc::mdebug {

// Perform all the main analysis passes on the mdebug symbol table and convert
// it to a set of C++ ASTs.
Result<SymbolSourceHandle> import_symbol_table(SymbolDatabase& database, const mdebug::SymbolTableReader& symbol_table, u32 parser_flags, DemanglerFunc* demangle);

// Try to add pointers from member function declarations to their definitions
// using a heuristic.
void fill_in_pointers_to_member_function_definitions(SymbolDatabase& database);

}
