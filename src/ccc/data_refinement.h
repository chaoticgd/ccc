// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "elf.h"
#include "symbol_database.h"

namespace ccc {

void refine_variables(SymbolDatabase& database, const std::vector<ElfFile*>& elves);

}
