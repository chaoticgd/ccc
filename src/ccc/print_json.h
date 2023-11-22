// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "symbol_table.h"

namespace ccc {

void print_json(FILE* out, const SymbolTable& symbol_table, bool print_per_file_types);

}