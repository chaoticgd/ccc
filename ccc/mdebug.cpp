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

static s32 get_corruption_fixing_fudge_offset(s32 section_offset, const SymbolicHeader& hdrr);
static Result<Symbol> parse_symbol(const SymbolHeader& header, const std::vector<u8>& elf, s32 strings_offset);

Result<void> SymbolTable::init(const std::vector<u8>& elf, s32 section_offset) {
	m_elf = &elf;
	m_section_offset = section_offset;
	
	m_hdrr = get_packed<SymbolicHeader>(*m_elf, m_section_offset);
	CCC_CHECK(m_hdrr != nullptr, "MIPS debug section header out of bounds.");
	CCC_CHECK(m_hdrr->magic == 0x7009, "Invalid symbolic header.");
	
	m_fudge_offset = get_corruption_fixing_fudge_offset(m_section_offset, *m_hdrr);
	
	m_ready = true;
	
	return Result<void>();
}
	
s32 SymbolTable::file_count() const {
	CCC_ASSERT(m_ready);
	return m_hdrr->file_descriptor_count;
}

Result<File> SymbolTable::parse_file(s32 index) const {
	CCC_ASSERT(m_ready);
	
	File file;
	
	u64 fd_offset = m_hdrr->file_descriptors_offset + index * sizeof(FileDescriptor);
	const FileDescriptor* fd_header = get_packed<FileDescriptor>(*m_elf, fd_offset + m_fudge_offset);
	CCC_CHECK(fd_header != nullptr, "MIPS debug file descriptor out of bounds.");
	CCC_CHECK(fd_header->f_big_endian == 0, "Not little endian or bad file descriptor table.");
	
	s32 raw_path_offset = m_hdrr->local_strings_offset + fd_header->strings_offset + fd_header->file_path_string_offset + m_fudge_offset;
	Result<const char*> raw_path = get_string(*m_elf, raw_path_offset);
	CCC_RETURN_IF_ERROR(raw_path);
	file.raw_path = *raw_path;
	
	// Try to detect the source language.
	std::string lower_name = file.raw_path;
	for(char& c : lower_name) c = tolower(c);
	if(lower_name.ends_with(".c")) {
		file.detected_language = SourceLanguage::C;
	} else if(lower_name.ends_with(".cpp") || lower_name.ends_with(".cc") || lower_name.ends_with(".cxx")) {
		file.detected_language = SourceLanguage::CPP;
	} else if(lower_name.ends_with(".s") || lower_name.ends_with(".asm")) {
		file.detected_language = SourceLanguage::ASSEMBLY;
	}
	
	// Parse local symbols.
	for(s64 j = 0; j < fd_header->symbol_count; j++) {
		u64 symbol_offset = m_hdrr->local_symbols_offset + (fd_header->isym_base + j) * sizeof(SymbolHeader) + m_fudge_offset;
		const SymbolHeader* symbol_header = get_packed<SymbolHeader>(*m_elf, symbol_offset);
		CCC_CHECK(symbol_header != nullptr, "Symbol header out of bounds.");
		
		s32 strings_offset = m_hdrr->local_strings_offset + fd_header->strings_offset + m_fudge_offset;
		Result<Symbol> sym = parse_symbol(*symbol_header, *m_elf, strings_offset);
		CCC_RETURN_IF_ERROR(sym);
		
		bool string_offset_equal = (s32) symbol_header->iss == fd_header->file_path_string_offset;
		if(file.base_path.empty() && string_offset_equal && sym->is_stabs && sym->code == N_SO && file.symbols.size() > 2) {
			const Symbol& base_path = file.symbols.back();
			if(base_path.is_stabs && base_path.code == N_SO) {
				file.base_path = base_path.string;
			}
		}
		
		file.symbols.emplace_back(std::move(*sym));
	}
	
	file.full_path = merge_paths(file.base_path, file.raw_path);
	
	return file;
}

Result<std::vector<Symbol>> SymbolTable::parse_external_symbols() const {
	CCC_ASSERT(m_ready);
	
	std::vector<Symbol> external_symbols;
	for(s64 i = 0; i < m_hdrr->external_symbols_count; i++) {
		u64 sym_offset = m_hdrr->external_symbols_offset + i * sizeof(ExternalSymbolHeader);
		const ExternalSymbolHeader* external_header = get_packed<ExternalSymbolHeader>(*m_elf, sym_offset + m_fudge_offset);
		CCC_CHECK(external_header != nullptr, "External header out of bounds.");
		Result<Symbol> sym = parse_symbol(external_header->symbol, *m_elf, m_hdrr->external_strings_offset + m_fudge_offset);
		CCC_RETURN_IF_ERROR(sym);
		external_symbols.emplace_back(std::move(*sym));
	}
	return external_symbols;
}

void SymbolTable::print_header(FILE* dest) const {
	CCC_ASSERT(m_ready);
	
	fprintf(dest, "Symbolic Header, magic = %hx, vstamp = %hx:\n",
		(u16) m_hdrr->magic,
		(u16) m_hdrr->version_stamp);
	fprintf(dest, "\n");
	fprintf(dest, "                              Offset              Size (Bytes)        Count\n");
	fprintf(dest, "                              ------              ------------        -----\n");
	fprintf(dest, "  Line Numbers                0x%-8x          "  "0x%-8x          "  "%-8d\n",
		(u32) m_hdrr->line_numbers_offset,
		(u32) m_hdrr->line_numbers_size_bytes,
		(u32) m_hdrr->line_number_count);
	fprintf(dest, "  Dense Numbers               0x%-8x          "  "0x%-8x          "  "%-8d\n",
		(u32) m_hdrr->dense_numbers_offset,
		(u32) m_hdrr->dense_numbers_count * 8,
		(u32) m_hdrr->dense_numbers_count);
	fprintf(dest, "  Procedure Descriptors       0x%-8x          "  "0x%-8x          "  "%-8d\n",
		(u32) m_hdrr->procedure_descriptors_offset,
		(u32) m_hdrr->procedure_descriptor_count * (u32) sizeof(ProcedureDescriptor),
		(u32) m_hdrr->procedure_descriptor_count);
	fprintf(dest, "  Local Symbols               0x%-8x          "  "0x%-8x          "  "%-8d\n",
		(u32) m_hdrr->local_symbols_offset,
		(u32) m_hdrr->local_symbol_count * (u32) sizeof(SymbolHeader),
		(u32) m_hdrr->local_symbol_count);
	fprintf(dest, "  Optimization Symbols        0x%-8x          "  "-                   "  "%-8d\n",
		(u32) m_hdrr->optimization_symbols_offset,
		(u32) m_hdrr->optimization_symbols_count);
	fprintf(dest, "  Auxiliary Symbols           0x%-8x          "  "0x%-8x          "  "%-8d\n",
		(u32) m_hdrr->auxiliary_symbols_offset,
		(u32) m_hdrr->auxiliary_symbol_count * 4,
		(u32) m_hdrr->auxiliary_symbol_count);
	fprintf(dest, "  Local Strings               0x%-8x          "  "-                   "  "%-8d\n",
		(u32) m_hdrr->local_strings_offset,
		(u32) m_hdrr->local_strings_size_bytes);
	fprintf(dest, "  External Strings            0x%-8x          "  "-                   "  "%-8d\n",
		(u32) m_hdrr->external_strings_offset,
		(u32) m_hdrr->external_strings_size_bytes);
	fprintf(dest, "  File Descriptors            0x%-8x          "  "0x%-8x          "  "%-8d\n",
		(u32) m_hdrr->file_descriptors_offset,
		(u32) m_hdrr->file_descriptor_count * (u32) sizeof(FileDescriptor),
		(u32) m_hdrr->file_descriptor_count);
	fprintf(dest, "  Relative Files Descriptors  0x%-8x          "  "0x%-8x          "  "%-8d\n",
		(u32) m_hdrr->relative_file_descriptors_offset,
		(u32) m_hdrr->relative_file_descriptor_count * 4,
		(u32) m_hdrr->relative_file_descriptor_count);
	fprintf(dest, "  External Symbols            0x%-8x          "  "0x%-8x          "  "%-8d\n",
		(u32) m_hdrr->external_symbols_offset,
		(u32) m_hdrr->external_symbols_count * 16,
		(u32) m_hdrr->external_symbols_count);
}

static s32 get_corruption_fixing_fudge_offset(s32 section_offset, const SymbolicHeader& hdrr) {
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
	
	if(right_after_header == section_offset + (s32) sizeof(SymbolicHeader)) {
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
