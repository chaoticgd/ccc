// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "elf.h"
#include "symbol_file.h"
#include "symbol_database.h"

namespace ccc {

// Determine which symbol tables are present in a given file.

enum SymbolTableFormat {
	SYMTAB = 0, // Standard ELF symbol table
	MDEBUG = 1, // The infamous Third Eye symbol table
	STAB   = 2, // Simpler container format for STABS symbols
	DWARF  = 3, // DWARF 1 symbol table
	SNDLL  = 4  // SNDLL linker symbols
};

struct SymbolTableFormatInfo {
	SymbolTableFormat format;
	const char* format_name;
	const char* section_name;
	u32 utility;
};

extern const SymbolTableFormatInfo SYMBOL_TABLE_FORMATS[];
extern const u32 SYMBOL_TABLE_FORMAT_COUNT;

const SymbolTableFormatInfo* symbol_table_format_from_enum(SymbolTableFormat format);
const SymbolTableFormatInfo* symbol_table_format_from_name(const char* format_name);
const SymbolTableFormatInfo* symbol_table_format_from_section(const char* section_name);

enum ParserFlags {
	NO_PARSER_FLAGS = 0,
	DONT_DEDUPLICATE_TYPES = (1 << 0),
	SKIP_FUNCTION_ANALYSIS = (1 << 1),
	STRIP_ACCESS_SPECIFIERS = (1 << 2),
	STRIP_MEMBER_FUNCTIONS = (1 << 3),
	STRIP_GENERATED_FUNCTIONS = (1 << 4)
};

typedef char* DemanglerFunc(const char* mangled, int options);

struct SymbolTableConfig {
	std::optional<std::string> section;
	std::optional<SymbolTableFormat> format;
	u32 parser_flags = NO_PARSER_FLAGS;
	DemanglerFunc* demangle = nullptr;
};

// The main high-level parsing function for the entire library. Return the
// handle of the newly created symbol source on success, or an invalid handle if
// no symbol table was found.
Result<SymbolSourceHandle> import_symbol_table(SymbolDatabase& database, const SymbolFile& file, const SymbolTableConfig& config);

Result<void> print_symbol_table(FILE* out, const SymbolFile& file, const SymbolTableConfig& config, bool print_locals, bool print_externals);

}
