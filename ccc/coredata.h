#ifndef _CCC_COREDATA_H
#define _CCC_COREDATA_H

#include "util.h"

namespace ccc {

struct ProgramImage {
	std::vector<u8> bytes;
};

// This is like a simplified ElfSectionType.
enum class ProgramSectionType {
	MIPS_DEBUG,
	OTHER
};

struct ProgramSection {
	u64 image;
	u64 file_offset;
	u64 size;
	ProgramSectionType type;
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
