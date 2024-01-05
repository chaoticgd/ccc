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
	NIL = 0, // I think this is just so that the first real symbol has an index of 1.
	EXTERNAL = 1, // Symbol with an empty value, to be filled in from another module.
	RELATIVE = 2, // Global symbol, value is relative to the start of the SNDLL file.
	WEAK = 3, // Weak symbol, value is relative to the start of the SNDLL file.
	ABSOLUTE = 4 // Global symbol, value is an absolute address.
};

struct SNDLLSymbol {
	SNDLLSymbolType type = SNDLLSymbolType::NIL;
	u32 value = 0;
	std::string string;
};

struct SNDLLFile {
	Address address;
	SNDLLVersion version;
	std::string elf_path;
	std::vector<SNDLLSymbol> symbols;
};

// If a valid address is passed, the pointers in the header will be treated as
// addresses, otherwise they will be treated as file offsets.
Result<SNDLLFile> parse_sndll_file(std::span<const u8> image, Address address = Address());

Result<void> import_sndll_symbols(
	SymbolDatabase& database,
	const SNDLLFile& sndll,
	SymbolSourceHandle source,
	u32 importer_flags,
	DemanglerFunctions demangler);

void print_sndll_symbols(FILE* out, const SNDLLFile& sndll);

}
