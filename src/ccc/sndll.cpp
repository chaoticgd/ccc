// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "sndll.h"

namespace ccc {

CCC_PACKED_STRUCT(SNDLLHeaderCommon,
	/* 0x00 */ u32 magic;
	/* 0x04 */ u32 relocations;
	/* 0x08 */ u32 relocation_count;
	/* 0x0c */ u32 symbols;
	/* 0x10 */ u32 symbol_count;
	/* 0x14 */ u32 elf_path;
	/* 0x18 */ u32 load_func;
	/* 0x1c */ u32 unload_func;
	/* 0x20 */ u32 unknown_20;
	/* 0x24 */ u32 unknown_24;
	/* 0x28 */ u32 unknown_28;
	/* 0x2c */ u32 file_size;
	/* 0x30 */ u32 unknown_30;
)

CCC_PACKED_STRUCT(SNDLLHeaderV1,
	/* 0x00 */ SNDLLHeaderCommon common;
)

CCC_PACKED_STRUCT(SNDLLHeaderV2,
	/* 0x00 */ SNDLLHeaderCommon common;
	/* 0x34 */ u32 unknown_34;
	/* 0x38 */ u32 unknown_38;
)

CCC_PACKED_STRUCT(SNDLLRelocation,
	/* 0x0 */ u32 unknown_0;
	/* 0x4 */ u32 unknown_4;
	/* 0x8 */ u32 unknown_8;
)

CCC_PACKED_STRUCT(SNDLLSymbolHeader,
	/* 0x0 */ u32 string;
	/* 0x4 */ u32 value;
	/* 0x8 */ u8 unknown_8;
	/* 0x9 */ u8 unknown_9;
	/* 0xa */ SNDLLSymbolType type;
	/* 0xb */ u8 processed;
)

static Result<SNDLLFile> parse_sndll_common(std::span<const u8> image, Address address, const SNDLLHeaderCommon& common, SNDLLVersion version);
static const char* sndll_symbol_type_to_string(SNDLLSymbolType type);

Result<SNDLLFile> parse_sndll_file(std::span<const u8> image, Address address) {
	const u32* magic = get_packed<u32>(image, 0);
	CCC_CHECK((*magic & 0xffffff) == CCC_FOURCC("SNR\00"), "Not a SNDLL %s.", address.valid() ? "section" : "file");
	
	char version = *magic >> 24;
	switch(version) {
		case '1': {
			const SNDLLHeaderV1* header = get_packed<SNDLLHeaderV1>(image, 0);
			CCC_CHECK(header, "File too small to contains SNDLL V1 header.");
			return parse_sndll_common(image, address, header->common, SNDLL_V1);
		}
		case '2': {
			const SNDLLHeaderV2* header = get_packed<SNDLLHeaderV2>(image, 0);
			CCC_CHECK(header, "File too small to contains SNDLL V2 header.");
			return parse_sndll_common(image, address, header->common, SNDLL_V2);
		}
	}
	
	return CCC_FAILURE("Unknown SNDLL version '%c'.", version);
}

static Result<SNDLLFile> parse_sndll_common(std::span<const u8> image, Address address, const SNDLLHeaderCommon& common, SNDLLVersion version) {
	SNDLLFile sndll;
	
	CCC_CHECK(common.file_size < 32 * 1024 * 1024, "SNDLL file too big!");
	CCC_CHECK(image.size() >= common.file_size, "SNDLL file truncated.");
	sndll.image = std::vector<u8>(image.begin(), image.begin() + common.file_size);
	
	sndll.version = version;
	
	if(common.elf_path) {
		sndll.elf_path = get_string(sndll.image, common.elf_path);
	}
	
	sndll.symbols.reserve(common.symbol_count);
	for(u32 i = 0; i < common.symbol_count; i++) {
		u32 symbol_offset = common.symbols - address.get_or_zero() + i * sizeof(SNDLLSymbolHeader);
		const SNDLLSymbolHeader* symbol_header = get_packed<SNDLLSymbolHeader>(sndll.image, symbol_offset);
		CCC_CHECK(symbol_header, "SNDLL symbol out of range.");
		
		const char* string = nullptr;
		if(symbol_header->string) {
			string = get_string(sndll.image, symbol_header->string - address.get_or_zero());
		}
		
		SNDLLSymbol& symbol = sndll.symbols.emplace_back();
		symbol.type = symbol_header->type;
		symbol.value = symbol_header->value;
		symbol.string = string;
	}
	
	return sndll;
}

Result<SymbolSourceHandle> import_sndll_symbol_table(SymbolDatabase& database, const SNDLLFile& sndll) {
	SymbolSourceHandle source;
	
	return source;
}

void print_sndll_symbols(FILE* out, const SNDLLFile& sndll) {
	for(const SNDLLSymbol& symbol : sndll.symbols) {
		fprintf(out, "%20s %08x %s\n", sndll_symbol_type_to_string(symbol.type), symbol.value, symbol.string ? symbol.string : "(no string)");
	}
}

static const char* sndll_symbol_type_to_string(SNDLLSymbolType type) {
	switch(type) {
		case SNDLLSymbolType::TYPE_0: return "type 0";
		case SNDLLSymbolType::EXTERNAL: return "external";
		case SNDLLSymbolType::GLOBAL_RELATIVE: return "global";
		case SNDLLSymbolType::WEAK: return "weak";
		case SNDLLSymbolType::GLOBAL_ABSOLUTE: return "absolute global";
	}
	return "invalid";
}

}
