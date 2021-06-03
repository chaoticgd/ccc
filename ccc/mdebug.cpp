#include "ccc.h"

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

packed_struct(SymbolRecord,
	u32 iss;
	u32 value;
	u32 st : 6;
	u32 sc : 5;
	u32 reserved : 1;
	u32 index : 20;
)
static_assert(sizeof(SymbolRecord) == 0xc);

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
	//SymbolRecord asym;    // 0x4c
)
static_assert(sizeof(FileDescriptorEntry) == 0x48);

SymbolTable parse_symbol_table(const ProgramImage& image, const ProgramSection& section) {
	SymbolTable symbol_table;
	
	const auto& hdrr = get_packed<SymbolicHeader>(image.bytes, section.file_offset, "MIPS debug section");
	verify(hdrr.magic == 0x7009, "error: Invalid symbolic header.\n");
	symbol_table.file_descriptor_table_offset = hdrr.cb_fd_offset;
	
	for(s64 i = 0; i < hdrr.ifd_max; i++) {
		u64 offset = hdrr.cb_fd_offset + i * sizeof(FileDescriptorEntry);
		const auto& entry = get_packed<FileDescriptorEntry>(image.bytes, offset, "file descriptor");
		verify(entry.f_big_endian == 0, "error: Not little endian or bad file descriptor table.\n");
		
		SymFileDescriptor fd;
		u64 file_name_offset = hdrr.cb_ss_offset + entry.iss_base + entry.rss;
		fd.name = read_string(image.bytes, file_name_offset);
		fd.procedures = {entry.ipd_first, entry.cpd};
		symbol_table.files.emplace_back(fd);
	}
	
	return symbol_table;
}
