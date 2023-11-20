// This file is part of the Chaos Compiler Collection.
//
// SPDX-License-Identifier: MIT

#pragma once

#include "elf.h"
#include "symbol_table.h"

namespace ccc {

void refine_variables(SymbolTable& symbol_table, const std::vector<ElfFile*>& elves);

}
