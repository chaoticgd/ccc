#pragma once

#include "elf.h"
#include "symbol_table.h"

namespace ccc {

void refine_variables(SymbolTable& symbol_table, const std::vector<ElfFile*>& elves);

}
