// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "elf.h"
#include "sndll.h"

namespace ccc {

using SymbolFile = std::variant<ElfFile, SNDLLFile>;

// Determine the type of the input file and parse it.
Result<SymbolFile> parse_symbol_file(std::vector<u8> image);

}
