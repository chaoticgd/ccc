// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "symbol_database.h"

namespace ccc {

void print_json(FILE* out, const SymbolDatabase& database, bool print_per_file_types);

}
