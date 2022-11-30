#ifndef _CCC_COREDATA_H
#define _CCC_COREDATA_H

#include "util.h"

namespace ccc {

struct ProgramImage {
	std::vector<u8> bytes;
};

enum class ElfSectionType : u32 {
	NULL_SECTION = 0x0,
	PROGBITS = 0x1,
	SYMTAB = 0x2,
	STRTAB = 0x3,
	RELA = 0x4,
	HASH = 0x5,
	DYNAMIC = 0x6,
	NOTE = 0x7,
	NOBITS = 0x8,
	REL = 0x9,
	SHLIB = 0xa,
	DYNSYM = 0xb,
	INIT_ARRAY = 0xe,
	FINI_ARRAY = 0xf,
	PREINIT_ARRAY = 0x10,
	GROUP = 0x11,
	SYMTAB_SHNDX = 0x12,
	NUM = 0x13,
	LOOS = 0x60000000,
	MIPS_DEBUG = 0x70000005
};

struct ProgramSection {
	u64 image;
	u64 file_offset;
	u64 size;
	ElfSectionType type;
	u32 name_offset;
	std::string name;
};

enum class SymbolType : u32 {
	NIL = 0,
	GLOBAL = 1,
	STATIC = 2,
	PARAM = 3,
	LOCAL = 4,
	LABEL = 5,
	PROC = 6,
	BLOCK = 7,
	END = 8,
	MEMBER = 9,
	TYPEDEF = 10,
	FILE_SYMBOL = 11,
	STATICPROC = 14,
	CONSTANT = 15
};

enum class SymbolClass : u32 {
	COMPILER_VERSION_INFO = 11
};

struct Symbol {
	std::string string;
	u32 value;
	SymbolType storage_type;
	SymbolClass storage_class;
	u32 index;
};

struct SymFileDescriptor {
	std::string name;
	Range procedures;
	std::vector<Symbol> symbols;
};

struct SymProcedureDescriptor {
	std::string name;
};

struct SymbolTable {
	std::vector<SymProcedureDescriptor> procedures;
	std::vector<SymFileDescriptor> files;
	u64 procedure_descriptor_table_offset;
	u64 local_symbol_table_offset;
	u64 file_descriptor_table_offset;
};

struct Program {
	std::vector<ProgramImage> images;
	std::vector<ProgramSection> sections;
};

};

#endif
