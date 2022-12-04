#ifndef _CCC_MDEBUG_H
#define _CCC_MDEBUG_H

#include "util.h"
#include "module.h"

namespace ccc {

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

struct SymProcedureDescriptor {
	std::string name;
	u32 address;
};

struct SymFileDescriptor {
	std::string name;
	std::vector<Symbol> symbols;
	std::vector<SymProcedureDescriptor> procedures;
};

struct SymbolTable {
	std::vector<SymFileDescriptor> files;
	u64 procedure_descriptor_table_offset;
	u64 local_symbol_table_offset;
	u64 file_descriptor_table_offset;
};

SymbolTable parse_mdebug_section(const Module& module, const ModuleSection& section);
const char* symbol_type(SymbolType type);
const char* symbol_class(SymbolClass symbol_class);

}

#endif
