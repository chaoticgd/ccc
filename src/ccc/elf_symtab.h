// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "symbol_database.h"

namespace ccc::elf {

Result<SymbolSourceHandle> parse_symbol_table(SymbolDatabase& database, const ElfSection& section, const ElfFile& elf);
[[nodiscard]] Result<void> print_symbol_table(FILE* out, const ElfSection& section, const ElfFile& elf);

}
