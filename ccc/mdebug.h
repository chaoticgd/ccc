#ifndef _CCC_MDEBUG_H
#define _CCC_MDEBUG_H

#include "util.h"
#include "module.h"

namespace ccc::mdebug {

struct SymbolicHeader;
struct FileDescriptor;
struct ProcedureDescriptor;

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
	NIL = 0,
	TEXT = 1,
	DATA = 2,
	BSS = 3,
	REGISTER = 4,
	ABS = 5,
	UNDEFINED = 6,
	LOCAL = 7,
	BITS = 8,
	DBX = 9,
	REG_IMAGE = 10,
	INFO = 11,
	USER_STRUCT = 12,
	SDATA = 13,
	SBSS = 14,
	RDATA = 15,
	VAR = 16,
	COMMON = 17,
	SCOMMON = 18,
	VAR_REGISTER = 19,
	VARIANT = 20,
	SUNDEFINED = 21,
	INIT = 22,
	BASED_VAR = 23,
	XDATA = 24,
	PDATA = 25,
	FINI = 26,
	NONGP = 27
};

struct Symbol {
	std::string string;
	s32 value;
	SymbolType storage_type;
	SymbolClass storage_class;
	u32 index;
};

struct SymProcedureDescriptor {
	std::string name;
	u32 address;
};

enum class SourceLanguage {
	C,
	CPP,
	ASSEMBLY,
	UNKNOWN
};

struct SymFileDescriptor {
	const FileDescriptor* header;
	std::string base_path;
	std::string raw_path;
	std::string full_path;
	std::vector<Symbol> symbols;
	std::vector<SymProcedureDescriptor> procedures;
	SourceLanguage detected_language = SourceLanguage::UNKNOWN;
};

struct SymbolTable {
	const SymbolicHeader* header;
	std::vector<SymFileDescriptor> files;
	u64 procedure_descriptor_table_offset;
	u64 local_symbol_table_offset;
	u64 file_descriptor_table_offset;
};

SymbolTable parse_symbol_table(const Module& module, const ModuleSection& section);
void print_headers(FILE* dest, const SymbolTable& symbol_table);
const char* symbol_type(SymbolType type);
const char* symbol_class(SymbolClass symbol_class);

}

#endif
