// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "stabs.h"
#include "mdebug.h"
#include "symbols.h"
#include "symbol_table.h"

namespace ccc {

// Perform all the main analysis passes on the mdebug symbol table and convert
// it to a set of C++ ASTs.
Result<SymbolTable> analyse(const mdebug::SymbolTableReader& symbol_table, u32 parser_flags, s32 file_index = -1);

// Try to add pointers from member function declarations to their definitions
// using a heuristic.
void fill_in_pointers_to_member_function_definitions(SymbolTable& symbol_table);

};
