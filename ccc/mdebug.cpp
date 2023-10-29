#include "mdebug.h"

namespace ccc::mdebug {

CCC_PACKED_STRUCT(SymbolicHeader,
	/* 0x00 */ s16 magic;
	/* 0x02 */ s16 version_stamp;
	/* 0x04 */ s32 line_number_count;
	/* 0x08 */ s32 line_numbers_size_bytes;
	/* 0x0c */ s32 line_numbers_offset;
	/* 0x10 */ s32 dense_numbers_count;
	/* 0x14 */ s32 dense_numbers_offset;
	/* 0x18 */ s32 procedure_descriptor_count;
	/* 0x1c */ s32 procedure_descriptors_offset;
	/* 0x20 */ s32 local_symbol_count;
	/* 0x24 */ s32 local_symbols_offset;
	/* 0x28 */ s32 optimization_symbols_count;
	/* 0x2c */ s32 optimization_symbols_offset;
	/* 0x30 */ s32 auxiliary_symbol_count;
	/* 0x34 */ s32 auxiliary_symbols_offset;
	/* 0x38 */ s32 local_strings_size_bytes;
	/* 0x3c */ s32 local_strings_offset;
	/* 0x40 */ s32 external_strings_size_bytes;
	/* 0x44 */ s32 external_strings_offset;
	/* 0x48 */ s32 file_descriptor_count;
	/* 0x4c */ s32 file_descriptors_offset;
	/* 0x50 */ s32 relative_file_descriptor_count;
	/* 0x54 */ s32 relative_file_descriptors_offset;
	/* 0x58 */ s32 external_symbols_count;
	/* 0x5c */ s32 external_symbols_offset;
)

CCC_PACKED_STRUCT(FileDescriptor,
	/* 0x00 */ u32 address;
	/* 0x04 */ s32 file_path_string_offset;
	/* 0x08 */ s32 strings_offset;
	/* 0x0c */ s32 cb_ss;
	/* 0x10 */ s32 isym_base;
	/* 0x14 */ s32 symbol_count;
	/* 0x18 */ s32 iline_base;
	/* 0x1c */ s32 cline;
	/* 0x20 */ s32 iopt_base;
	/* 0x24 */ s32 copt;
	/* 0x28 */ s16 ipd_first;
	/* 0x2a */ s16 cpd;
	/* 0x2c */ s32 iaux_base;
	/* 0x30 */ s32 caux;
	/* 0x34 */ s32 rfd_base;
	/* 0x38 */ s32 crfd;
	/* 0x3c */ u32 lang : 5;
	/* 0x3c */ u32 f_merge : 1;
	/* 0x3c */ u32 f_readin : 1;
	/* 0x3c */ u32 f_big_endian : 1;
	/* 0x4c */ u32 reserved_1 : 22;
	/* 0x40 */ s32 cb_line_offset;
	/* 0x44 */ s32 cb_line;
)
static_assert(sizeof(FileDescriptor) == 0x48);

CCC_PACKED_STRUCT(ProcedureDescriptor,
	/* 0x00 */ u32 address;
	/* 0x04 */ s32 isym;
	/* 0x08 */ s32 iline;
	/* 0x0c */ s32 regmask;
	/* 0x10 */ s32 regoffset;
	/* 0x14 */ s32 iopt;
	/* 0x18 */ s32 fregmask;
	/* 0x1c */ s32 fregoffset;
	/* 0x20 */ s32 frameoffset;
	/* 0x24 */ s16 framereg;
	/* 0x26 */ s16 pcreg;
	/* 0x28 */ s32 ln_low;
	/* 0x2c */ s32 ln_high;
	/* 0x30 */ s32 cb_line_offset;
)

CCC_PACKED_STRUCT(SymbolHeader,
	/* 0x0 */ u32 iss;
	/* 0x4 */ s32 value;
	/* 0x8:00 */ u32 st : 6;
	/* 0x8:06 */ u32 sc : 5;
	/* 0x8:11 */ u32 reserved : 1;
	/* 0x8:12 */ u32 index : 20;
)
static_assert(sizeof(SymbolHeader) == 0xc);

CCC_PACKED_STRUCT(ExternalSymbolHeader,
	/* 0x0 */ u16 flags;
	/* 0x2 */ s16 ifd;
	/* 0x4 */ SymbolHeader symbol;
)

static s32 get_corruption_fixing_fudge_offset(u32 section_offset, const SymbolicHeader& hdrr);
static Result<Symbol> parse_symbol(const SymbolHeader& header, const std::vector<u8>& elf, s32 strings_offset);

Result<SymbolTable> parse_symbol_table(const std::vector<u8>& elf, u32 section_offset) {
	SymbolTable symbol_table;
	
	const SymbolicHeader* hdrr = get_packed<SymbolicHeader>(elf, section_offset);
	CCC_CHECK(hdrr != nullptr, "MIPS_DEBUG section header out of bounds.");
	CCC_CHECK(hdrr->magic == 0x7009, "Invalid symbolic header.");
	
	symbol_table.header = hdrr;
	
	s32 fudge_offset = get_corruption_fixing_fudge_offset(section_offset, *hdrr);
	
	// Iterate over file descriptors.
	for(s64 i = 0; i < hdrr->file_descriptor_count; i++) {
		u64 fd_offset = hdrr->file_descriptors_offset + i * sizeof(FileDescriptor);
		const FileDescriptor* fd_header = get_packed<FileDescriptor>(elf, fd_offset + fudge_offset);
		CCC_CHECK(fd_header != nullptr, "MIPS_DEBUG file descriptor out of bounds.");
		CCC_CHECK(fd_header->f_big_endian == 0, "Not little endian or bad file descriptor table.");
		
		SymFileDescriptor& fd = symbol_table.files.emplace_back();
		fd.header = fd_header;
		
		s32 raw_path_offset = hdrr->local_strings_offset + fd_header->strings_offset + fd_header->file_path_string_offset + fudge_offset;
		Result<const char*> raw_path = get_string(elf, raw_path_offset);
		CCC_RETURN_IF_ERROR(raw_path);
		fd.raw_path = *raw_path;
		
		// Try to detect the source language.
		std::string lower_name = fd.raw_path;
		for(char& c : lower_name) c = tolower(c);
		if(lower_name.ends_with(".c")) {
			fd.detected_language = SourceLanguage::C;
		} else if(lower_name.ends_with(".cpp") || lower_name.ends_with(".cc") || lower_name.ends_with(".cxx")) {
			fd.detected_language = SourceLanguage::CPP;
		} else if(lower_name.ends_with(".s") || lower_name.ends_with(".asm")) {
			fd.detected_language = SourceLanguage::ASSEMBLY;
		}
		
		// Parse local symbols.
		for(s64 j = 0; j < fd_header->symbol_count; j++) {
			u64 sym_offset = hdrr->local_symbols_offset + (fd_header->isym_base + j) * sizeof(SymbolHeader);
			const SymbolHeader* symbol_header = get_packed<SymbolHeader>(elf, sym_offset + fudge_offset);
			CCC_CHECK(symbol_header != nullptr, "Symbol header out of bounds.");
			
			s32 strings_offset = hdrr->local_strings_offset + fd_header->strings_offset + fudge_offset;
			Result<Symbol> sym = parse_symbol(*symbol_header, elf, strings_offset);
			CCC_RETURN_IF_ERROR(sym);
			
			bool string_offset_equal = (s32) symbol_header->iss == fd_header->file_path_string_offset;
			if(fd.base_path.empty() && string_offset_equal && sym->is_stabs && sym->code == N_SO && fd.symbols.size() > 2) {
				const Symbol& base_path = fd.symbols.back();
				if(base_path.is_stabs && base_path.code == N_SO) {
					fd.base_path = base_path.string;
				}
			}
			
			fd.symbols.emplace_back(std::move(*sym));
		}
		
		fd.full_path = merge_paths(fd.base_path, fd.raw_path);
	}
	
	// Parse external symbols.
	for(s64 i = 0; i < hdrr->external_symbols_count; i++) {
		u64 sym_offset = hdrr->external_symbols_offset + i * sizeof(ExternalSymbolHeader);
		const ExternalSymbolHeader* external_header = get_packed<ExternalSymbolHeader>(elf, sym_offset + fudge_offset);
		CCC_CHECK(external_header != nullptr, "External header out of bounds.");
		Result<Symbol> sym = parse_symbol(external_header->symbol, elf, hdrr->external_strings_offset + fudge_offset);
		CCC_RETURN_IF_ERROR(sym);
		symbol_table.externals.emplace_back(std::move(*sym));
	}
	
	return symbol_table;
}

static s32 get_corruption_fixing_fudge_offset(u32 section_offset, const SymbolicHeader& hdrr) {
	// If the .mdebug section was moved without updating its contents all the
	// absolute file offsets stored within will be incorrect by a fixed amount.
	
	// Test for corruption.
	s32 right_after_header = INT32_MAX;
	if(hdrr.line_numbers_offset > 0) right_after_header = std::min(hdrr.line_numbers_offset, right_after_header);
	if(hdrr.dense_numbers_offset > 0) right_after_header = std::min(hdrr.dense_numbers_offset, right_after_header);
	if(hdrr.procedure_descriptors_offset > 0) right_after_header = std::min(hdrr.procedure_descriptors_offset, right_after_header);
	if(hdrr.local_symbols_offset > 0) right_after_header = std::min(hdrr.local_symbols_offset, right_after_header);
	if(hdrr.optimization_symbols_offset > 0) right_after_header = std::min(hdrr.optimization_symbols_offset, right_after_header);
	if(hdrr.auxiliary_symbols_offset > 0) right_after_header = std::min(hdrr.auxiliary_symbols_offset, right_after_header);
	if(hdrr.local_strings_offset > 0) right_after_header = std::min(hdrr.local_strings_offset, right_after_header);
	if(hdrr.external_strings_offset > 0) right_after_header = std::min(hdrr.external_strings_offset, right_after_header);
	if(hdrr.file_descriptors_offset > 0) right_after_header = std::min(hdrr.file_descriptors_offset, right_after_header);
	if(hdrr.relative_file_descriptors_offset > 0) right_after_header = std::min(hdrr.relative_file_descriptors_offset, right_after_header);
	if(hdrr.external_symbols_offset > 0) right_after_header = std::min(hdrr.external_symbols_offset, right_after_header);
	
	if(right_after_header == (s32) (section_offset + sizeof(SymbolicHeader))) {
		return 0; // It's probably fine.
	}
	
	if(right_after_header < 0 || right_after_header == INT32_MAX) {
		CCC_WARN("The .mdebug section is probably corrupted and can't be automatically fixed.");
		return 0; // It's probably not fine.
	}
	
	// Try to fix it.
	s32 fudge_offset = section_offset - (right_after_header - sizeof(SymbolicHeader));
	if(fudge_offset != 0) {
		CCC_WARN("The .mdebug section is probably corrupted, but I can try to fix it for you (fudge offset %d).", fudge_offset);
	}
	return fudge_offset;
}

static Result<Symbol> parse_symbol(const SymbolHeader& header, const std::vector<u8>& elf, s32 strings_offset) {
	Symbol symbol;
	
	Result<const char*> string = get_string(elf, strings_offset + header.iss);
	CCC_RETURN_IF_ERROR(string);
	symbol.string = *string;
	
	symbol.value = header.value;
	symbol.storage_type = (SymbolType) header.st;
	symbol.storage_class = (SymbolClass) header.sc;
	symbol.index = header.index;
	if((symbol.index & 0xfff00) == 0x8f300) {
		symbol.is_stabs = true;
		symbol.code = (StabsCode) (symbol.index - 0x8f300);
		CCC_CHECK(stabs_code(symbol.code) != nullptr, "Bad stabs symbol code '%x'.", symbol.code);
	} else {
		symbol.is_stabs = false;
	}
	return symbol;
}

void print_headers(FILE* dest, const SymbolTable& symbol_table) {
	const SymbolicHeader& hdrr = *symbol_table.header;
	fprintf(dest, "Symbolic Header, magic = %hx, vstamp = %hx:\n", hdrr.magic, hdrr.version_stamp);
	fprintf(dest, "\n");
	fprintf(dest, "                              Offset              Size (Bytes)        Count\n");
	fprintf(dest, "                              ------              ------------        -----\n");
	fprintf(dest, "  Line Numbers                0x%-8x          "  "0x%-8x          "  "%-8d\n",
		hdrr.line_numbers_offset,
		hdrr.line_numbers_size_bytes,
		hdrr.line_number_count);
	fprintf(dest, "  Dense Numbers               0x%-8x          "  "0x%-8x          "  "%-8d\n",
		hdrr.dense_numbers_offset,
		hdrr.dense_numbers_count * 8,
		hdrr.dense_numbers_count);
	fprintf(dest, "  Procedure Descriptors       0x%-8x          "  "0x%-8x          "  "%-8d\n",
		hdrr.procedure_descriptors_offset,
		hdrr.procedure_descriptor_count * (s32) sizeof(ProcedureDescriptor),
		hdrr.procedure_descriptor_count);
	fprintf(dest, "  Local Symbols               0x%-8x          "  "0x%-8x          "  "%-8d\n",
		hdrr.local_symbols_offset,
		hdrr.local_symbol_count * (s32) sizeof(SymbolHeader),
		hdrr.local_symbol_count);
	fprintf(dest, "  Optimization Symbols        0x%-8x          "  "-                   "  "%-8d\n",
		hdrr.optimization_symbols_offset,
		hdrr.optimization_symbols_count);
	fprintf(dest, "  Auxiliary Symbols           0x%-8x          "  "0x%-8x          "  "%-8d\n",
		hdrr.auxiliary_symbols_offset,
		hdrr.auxiliary_symbol_count * 4,
		hdrr.auxiliary_symbol_count);
	fprintf(dest, "  Local Strings               0x%-8x          "  "-                   "  "%-8d\n",
		hdrr.local_strings_offset,
		hdrr.local_strings_size_bytes);
	fprintf(dest, "  External Strings            0x%-8x          "  "-                   "  "%-8d\n",
		hdrr.external_strings_offset,
		hdrr.external_strings_size_bytes);
	fprintf(dest, "  File Descriptors            0x%-8x          "  "0x%-8x          "  "%-8d\n",
		hdrr.file_descriptors_offset,
		hdrr.file_descriptor_count * (s32) sizeof(FileDescriptor),
		hdrr.file_descriptor_count);
	fprintf(dest, "  Relative Files Descriptors  0x%-8x          "  "0x%-8x          "  "%-8d\n",
		hdrr.relative_file_descriptors_offset,
		hdrr.relative_file_descriptor_count * 4,
		hdrr.relative_file_descriptor_count);
	fprintf(dest, "  External Symbols            0x%-8x          "  "0x%-8x          "  "%-8d\n",
		hdrr.external_symbols_offset,
		hdrr.external_symbols_count * 16,
		hdrr.external_symbols_count);
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
	}
	return nullptr;
}

const char* symbol_class(SymbolClass symbol_class) {
	switch(symbol_class) {
		case SymbolClass::NIL: return "NIL";
		case SymbolClass::TEXT: return "TEXT";
		case SymbolClass::DATA: return "DATA";
		case SymbolClass::BSS: return "BSS";
		case SymbolClass::REGISTER: return "REGISTER";
		case SymbolClass::ABS: return "ABS";
		case SymbolClass::UNDEFINED: return "UNDEFINED";
		case SymbolClass::LOCAL: return "LOCAL";
		case SymbolClass::BITS: return "BITS";
		case SymbolClass::DBX: return "DBX";
		case SymbolClass::REG_IMAGE: return "REG_IMAGE";
		case SymbolClass::INFO: return "INFO";
		case SymbolClass::USER_STRUCT: return "USER_STRUCT";
		case SymbolClass::SDATA: return "SDATA";
		case SymbolClass::SBSS: return "SBSS";
		case SymbolClass::RDATA: return "RDATA";
		case SymbolClass::VAR: return "VAR";
		case SymbolClass::COMMON: return "COMMON";
		case SymbolClass::SCOMMON: return "SCOMMON";
		case SymbolClass::VAR_REGISTER: return "VAR_REGISTER";
		case SymbolClass::VARIANT: return "VARIANT";
		case SymbolClass::SUNDEFINED: return "SUNDEFINED";
		case SymbolClass::INIT: return "INIT";
		case SymbolClass::BASED_VAR: return "BASED_VAR";
		case SymbolClass::XDATA: return "XDATA";
		case SymbolClass::PDATA: return "PDATA";
		case SymbolClass::FINI: return "FINI";
		case SymbolClass::NONGP: return "NONGP";
	}
	return nullptr;
}

const char* stabs_code(StabsCode code) {
	switch(code) {
		case STAB: return "STAB";
		case N_GSYM: return "GSYM";
		case N_FNAME: return "FNAME";
		case N_FUN: return "FUN";
		case N_STSYM: return "STSYM";
		case N_LCSYM: return "LCSYM";
		case N_MAIN: return "MAIN";
		case N_PC: return "PC";
		case N_NSYMS: return "NSYMS";
		case N_NOMAP: return "NOMAP";
		case N_OBJ: return "OBJ";
		case N_OPT: return "OPT";
		case N_RSYM: return "RSYM";
		case N_M2C: return "M2C";
		case N_SLINE: return "SLINE";
		case N_DSLINE: return "DSLINE";
		case N_BSLINE: return "BSLINE";
		case N_EFD: return "EFD";
		case N_EHDECL: return "EHDECL";
		case N_CATCH: return "CATCH";
		case N_SSYM: return "SSYM";
		case N_SO: return "SO";
		case N_LSYM: return "LSYM";
		case N_BINCL: return "BINCL";
		case N_SOL: return "SOL";
		case N_PSYM: return "PSYM";
		case N_EINCL: return "EINCL";
		case N_ENTRY: return "ENTRY";
		case N_LBRAC: return "LBRAC";
		case N_EXCL: return "EXCL";
		case N_SCOPE: return "SCOPE";
		case N_RBRAC: return "RBRAC";
		case N_BCOMM: return "BCOMM";
		case N_ECOMM: return "ECOMM";
		case N_ECOML: return "ECOML";
		case N_NBTEXT: return "NBTEXT";
		case N_NBDATA: return "NBDATA";
		case N_NBBSS: return "NBBSS";
		case N_NBSTS: return "NBSTS";
		case N_NBLCS: return "NBLCS";
		case N_LENG: return "LENG";
	}
	return nullptr;
}

}
