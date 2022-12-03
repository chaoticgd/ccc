#include "mdebug.h"

namespace ccc {

packed_struct(SymbolicHeader,
	s16 magic;
	s16 vstamp;
	s32 iline_max;
	s32 cb_line;
	s32 cb_line_offset;
	s32 idn_max;
	s32 cb_dn_offset;
	s32 ipd_max;
	s32 cb_pd_offset;
	s32 isym_max;
	s32 cb_sym_offset;
	s32 iopt_max;
	s32 cb_opt_offset;
	s32 iaux_max;
	s32 cb_aux_offset;
	s32 iss_max;
	s32 cb_ss_offset;
	s32 iss_ext_max;
	s32 cb_ss_ext_offset;
	s32 ifd_max;
	s32 cb_fd_offset;
	s32 crfd;
	s32 cb_rfd_offset;
	s32 iext_max;
	s32 cb_ext_offset;
)

packed_struct(ProcedureDescriptorEntry,
	u32 adr;            // 0x00
	s32 isym;           // 0x04
	s32 iline;          // 0x08
	s32 regmask;        // 0x0c
	s32 regoffset;      // 0x10
	s32 iopt;           // 0x14
	s32 fregmask;       // 0x18
	s32 fregoffset;     // 0x1c
	s32 frameoffset;    // 0x20
	s16 framereg;       // 0x24
	s16 pcreg;          // 0x26
	s32 ln_low;         // 0x28
	s32 ln_high;        // 0x2c
	s32 cb_line_offset; // 0x30
)

packed_struct(SymbolEntry,
	u32 iss;
	u32 value;
	u32 st : 6;
	u32 sc : 5;
	u32 reserved : 1;
	u32 index : 20;
)
static_assert(sizeof(SymbolEntry) == 0xc);

packed_struct(FileDescriptorEntry,
	u32 adr;              // 0x00
	s32 rss;              // 0x04
	s32 iss_base;         // 0x08
	s32 cb_ss;            // 0x0c
	s32 isym_base;        // 0x10
	s32 csym;             // 0x14
	s32 iline_base;       // 0x18
	s32 cline;            // 0x1c
	s32 iopt_base;        // 0x20
	s32 copt;             // 0x24
	s16 ipd_first;        // 0x28
	s16 cpd;              // 0x2a
	s32 iaux_base;        // 0x2c
	s32 caux;             // 0x30
	s32 rfd_base;         // 0x34
	s32 crfd;             // 0x38
	u32 lang : 5;         // 0x3c
	u32 f_merge : 1;      // 0x3c
	u32 f_readin : 1;     // 0x3c
	u32 f_big_endian : 1; // 0x3c
	u32 reserved_1 : 22;  // 0x4c
	s32 cb_line_offset;   // 0x40
	s32 cb_line;          // 0x44
	//s16 reserved_2;       // 0x48
	//s16 ifd;              // 0x4a
	//SymbolEntry asym;     // 0x4c
)
static_assert(sizeof(FileDescriptorEntry) == 0x48);

SymbolTable parse_mdebug_section(const Module& module, const ModuleSection& section) {
	SymbolTable symbol_table;
	
	const auto& hdrr = get_packed<SymbolicHeader>(module.image, section.file_offset, "MIPS debug section");
	verify(hdrr.magic == 0x7009, "Invalid symbolic header.");
	
	symbol_table.procedure_descriptor_table_offset = hdrr.cb_pd_offset;
	symbol_table.local_symbol_table_offset = hdrr.cb_sym_offset;
	symbol_table.file_descriptor_table_offset = hdrr.cb_fd_offset;
	for(s64 i = 0; i < hdrr.ifd_max; i++) {
		u64 fd_offset = hdrr.cb_fd_offset + i * sizeof(FileDescriptorEntry);
		const auto& fd_entry = get_packed<FileDescriptorEntry>(module.image, fd_offset, "file descriptor");
		verify(fd_entry.f_big_endian == 0, "Not little endian or bad file descriptor table.");
		
		SymFileDescriptor fd;
		u64 file_name_offset = hdrr.cb_ss_offset + fd_entry.iss_base + fd_entry.rss;
		fd.name = read_string(module.image, file_name_offset);
		fd.procedures = {fd_entry.ipd_first, fd_entry.ipd_first + fd_entry.cpd};
		
		for(s64 j = 0; j < fd_entry.csym; j++) {
			u64 sym_offset = hdrr.cb_sym_offset + (fd_entry.isym_base + j) * sizeof(SymbolEntry);
			const auto& sym_entry = get_packed<SymbolEntry>(module.image, sym_offset, "local symbol");
			
			Symbol sym;
			u64 string_offset = hdrr.cb_ss_offset + fd_entry.iss_base + sym_entry.iss;
			sym.string = read_string(module.image, string_offset);
			sym.value = sym_entry.value;
			sym.storage_type = (SymbolType) sym_entry.st;
			sym.storage_class = (SymbolClass) sym_entry.sc;
			sym.index = sym_entry.index;
			fd.symbols.emplace_back(sym);
		}
		
		symbol_table.files.emplace_back(fd);
	}
	
	return symbol_table;
}

const char* symbol_type(SymbolType type) {
	switch(type) {
		case SymbolType::NIL: return "NIL";
		case SymbolType::GLOBAL: return "GLOBAL";
		case SymbolType::STATIC: return "STATIC";
		case SymbolType::PARAM: return "PARAM";
		case SymbolType::LOCAL: return "LOCAL";
		case SymbolType::LABEL: return "LABEL";
		case SymbolType::PROC: return "PROC";
		case SymbolType::BLOCK: return "BLOCK";
		case SymbolType::END: return "END";
		case SymbolType::MEMBER: return "MEMBER";
		case SymbolType::TYPEDEF: return "TYPEDEF";
		case SymbolType::FILE_SYMBOL: return "FILE_SYMBOL";
		case SymbolType::STATICPROC: return "STATICPROC";
		case SymbolType::CONSTANT: return "CONSTANT";
		default: return nullptr;
	}
}

const char* symbol_class(SymbolClass symbol_class) {
	switch(symbol_class) {
		case SymbolClass::COMPILER_VERSION_INFO: return "COMPILER_VERSION_INFO";
		default: return nullptr;
	}
}

}
