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

enum StabsCode {
	STAB = 0x00,
	N_GSYM = 0x20,
	N_FNAME = 0x22,
	N_FUN = 0x24,
	N_STSYM = 0x26,
	N_LCSYM = 0x28,
	N_MAIN = 0x2a,
	N_PC = 0x30,
	N_NSYMS = 0x32,
	N_NOMAP = 0x34,
	N_OBJ = 0x38,
	N_OPT = 0x3c,
	N_RSYM = 0x40,
	N_M2C = 0x42,
	N_SLINE = 0x44,
	N_DSLINE = 0x46,
	N_BSLINE = 0x48,
	N_EFD = 0x4a,
	N_EHDECL = 0x50,
	N_CATCH = 0x54,
	N_SSYM = 0x60,
	N_SO = 0x64,
	N_LSYM = 0x80,
	N_BINCL = 0x82,
	N_SOL = 0x84,
	N_PSYM = 0xa0,
	N_EINCL = 0xa2,
	N_ENTRY = 0xa4,
	N_LBRAC = 0xc0,
	N_EXCL = 0xc2,
	N_SCOPE = 0xc4,
	N_RBRAC = 0xe0,
	N_BCOMM = 0xe2,
	N_ECOMM = 0xe4,
	N_ECOML = 0xe8,
	N_NBTEXT = 0xf0,
	N_NBDATA = 0xf2,
	N_NBBSS = 0xf4,
	N_NBSTS = 0xf6,
	N_NBLCS = 0xf8,
	N_LENG = 0xfe
};

struct Symbol {
	const char* string;
	s32 value;
	SymbolType storage_type;
	SymbolClass storage_class;
	u32 index;
	bool is_stabs = false;
	StabsCode code;
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
	bool is_windows_path = false;
	std::vector<Symbol> symbols;
	std::vector<SymProcedureDescriptor> procedures;
	SourceLanguage detected_language = SourceLanguage::UNKNOWN;
};

struct SymbolTable {
	const SymbolicHeader* header;
	std::vector<SymFileDescriptor> files;
	std::vector<Symbol> externals;
};

SymbolTable parse_symbol_table(const Module& module, const ModuleSection& section);
void print_headers(FILE* dest, const SymbolTable& symbol_table);
const char* symbol_type(SymbolType type);
const char* symbol_class(SymbolClass symbol_class);
const char* stabs_code(StabsCode code);

}

#endif
