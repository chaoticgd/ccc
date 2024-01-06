// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "symbol_database.h"

namespace ccc {

enum class ElfSectionType : u32 {
	NULL_SECTION = 0x0,
	PROGBITS = 0x1,
	SYMTAB = 0x2,
	STRTAB = 0x3,
	RELA = 0x4,
	HASH = 0x5,
	DYNAMIC = 0x6,
	NOTE = 0x7,
	NOBITS = 0x8,
	REL = 0x9,
	SHLIB = 0xa,
	DYNSYM = 0xb,
	INIT_ARRAY = 0xe,
	FINI_ARRAY = 0xf,
	PREINIT_ARRAY = 0x10,
	GROUP = 0x11,
	SYMTAB_SHNDX = 0x12,
	NUM = 0x13,
	LOOS = 0x60000000,
	MIPS_DEBUG = 0x70000005
};

struct ElfSection {
	std::string name;
	ElfSectionType type;
	u32 offset;
	u32 size;
	Address address;
	u32 link;
};

struct ElfSegment {
	u32 offset;
	u32 size;
	Address address;
};

struct ElfFile {
	std::vector<u8> image;
	std::vector<ElfSection> sections;
	std::vector<ElfSegment> segments;
	
	const ElfSection* lookup_section(const char* name) const;
	std::optional<u32> file_offset_to_virtual_address(u32 file_offset) const;
};

// Parse the ELF file header, section headers and program headers.
Result<ElfFile> parse_elf_file(std::vector<u8> image);

Result<void> import_elf_section_headers(SymbolDatabase& database, const ElfFile& elf, SymbolSourceHandle source);

Result<void> read_virtual(u8* dest, u32 address, u32 size, const std::vector<ElfFile*>& elves);

template <typename T>
Result<std::vector<T>> read_virtual_vector(u32 address, u32 count, const std::vector<ElfFile*>& elves)
{
	std::vector<T> vector(count);
	Result<void> result = read_virtual((u8*) vector.data(), address, count * sizeof(T), elves);
	CCC_RETURN_IF_ERROR(result);
	return vector;
}

}
