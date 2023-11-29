// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "elf.h"
#include "sndll.h"

namespace ccc {

using SymbolFile = std::variant<ElfFile, SNDLLFile>;

Result<SymbolFile> parse_symbol_file(std::span<const u8> image);

}
