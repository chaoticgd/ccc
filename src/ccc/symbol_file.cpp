// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "symbol_file.h"

namespace ccc {

Result<SymbolFile> parse_symbol_file(std::span<const u8> image) {
	const u32* magic = get_packed<u32>(image, 0);
	CCC_CHECK(magic, "File too small.");
	
	switch(*magic) {
		case CCC_FOURCC("\x7f""ELF"): {
			Result<ElfFile> elf = parse_elf_file(image);
			CCC_RETURN_IF_ERROR(elf);
			return SymbolFile(*elf);
		}
		case CCC_FOURCC("SNR1"):
		case CCC_FOURCC("SNR2"): {
			Result<SNDLLFile> sndll = parse_sndll_file(image);
			CCC_RETURN_IF_ERROR(sndll);
			return SymbolFile(*sndll);
		}
	}
	
	return CCC_FAILURE("Unknown file type.");
}

}
