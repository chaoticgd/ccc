// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "elf.h"
#include "symbol_database.h"

namespace ccc {

// Determine which symbol tables are present in a given file.

enum SymbolTableFormat {
	SYMTAB = 1 << 0, // Standard ELF symbol table
	MAP    = 1 << 1, // Text-based (.map) symbol table
	MDEBUG = 1 << 2, // The infamous Third Eye symbol table
	STAB   = 1 << 3, // Simpler container format for STABS symbols
	DWARF  = 1 << 4, // DWARF 1 symbol table
	SNDATA = 1 << 5, // SNDLL linker symbols from an executable (.elf)
	SNDLL  = 1 << 6  // SNDLL linker symbols from a dynamic library (.rel)
};

const char* symbol_table_format_to_string(SymbolTableFormat format);

enum {
	NO_SYMBOL_TABLE = 0,      // No symbol table present
	MAX_SYMBOL_TABLE = 1 << 7 // End marker
};

u32 identify_elf_symbol_tables(const ElfFile& elf);
std::string symbol_table_formats_to_string(u32 formats);

enum ParserFlags {
	NO_PARSER_FLAGS = 0,
	DONT_DEDUPLICATE_TYPES = (1 << 0),
	SKIP_FUNCTION_ANALYSIS = (1 << 1),
	STRIP_ACCESS_SPECIFIERS = (1 << 2),
	STRIP_MEMBER_FUNCTIONS = (1 << 3),
	STRIP_GENERATED_FUNCTIONS = (1 << 4)
};

// The main high-level parsing function for the entire library.

Result<SymbolSourceHandle> parse_symbol_table(SymbolDatabase& database, std::vector<u8> image, u32 parser_flags);

}
