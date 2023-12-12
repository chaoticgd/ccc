// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "elf.h"
#include "symbol_database.h"

namespace ccc::elf {

Result<SymbolSourceHandle> import_symbol_table(SymbolDatabase& database, const ElfSection& section, const ElfFile& elf, bool ignore_existing_symbols);
[[nodiscard]] Result<void> print_symbol_table(FILE* out, const ElfSection& section, const ElfFile& elf);

}
