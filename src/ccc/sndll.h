// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "symbol_database.h"

namespace ccc {

enum SNDLLVersion {
	SNDLL_V1,
	SNDLL_V2
};

enum class SNDLLSymbolType : u8 {
	TYPE_0 = 0,
	EXTERNAL = 1,
	GLOBAL_RELATIVE = 2,
	WEAK = 3,
	GLOBAL_ABSOLUTE = 4
};

struct SNDLLSymbol {
	SNDLLSymbolType type = SNDLLSymbolType::TYPE_0;
	u32 value = 0;
	const char* string = nullptr;
};

struct SNDLLFile {
	std::span<const u8> image;
	SNDLLVersion version;
	const char* elf_path = nullptr;
	std::vector<SNDLLSymbol> symbols;
};

// If a valid address is passed, the pointers in the header will be treated as
// addresses, otherwise they will be treated as file offsets.
Result<SNDLLFile> parse_sndll_file(std::span<const u8> image, Address address = Address());

Result<SymbolSourceHandle> import_sndll_symbol_table(SymbolDatabase& database, const SNDLLFile& sndll);
void print_sndll_symbols(FILE* out, const SNDLLFile& sndll);

}
