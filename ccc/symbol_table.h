#pragma once

#include "elf.h"

namespace ccc {

enum SymbolTableFormat {
	SYMTAB = 1 << 0, // Standard ELF symbol table
	MAP    = 1 << 1, // Text-based (.map) symbol table
	MDEBUG = 1 << 2, // The infamous Third Eye symbol table
	STAB   = 1 << 3, // Simpler container format for STABS symbols
	DWARF  = 1 << 4, // DWARF 1 symbol table
	SNDATA = 1 << 5, // SNDLL linker symbols from an executable (.elf)
	SNDLL  = 1 << 6  // SNDLL linker smybols from a dynamic library (.rel)
};

enum {
	NO_SYMBOL_TABLE = 0,      // No symbol table present
	MAX_SYMBOL_TABLE = 1 << 7 // End marker
};

u32 identify_symbol_tables(const ElfFile& elf);

struct SourceFile {
	
};

struct DataType {
	
};

struct SymbolTable {
	std::vector<SourceFile> source_files;
	std::vector<DataType> data_types;
};

void print_symbol_table_formats_to_string(FILE* out, u32 formats);
const char* symbol_table_format_to_string(SymbolTableFormat format);

}
