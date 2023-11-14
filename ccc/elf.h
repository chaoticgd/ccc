#pragma once

#include "util.h"

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
	u32 file_offset = -1;
	u32 size = -1;
	ElfSectionType type;
	u32 name_offset = -1;
	std::string name;
	u32 virtual_address = -1;
};

struct ElfSegment {
	u32 file_offset;
	u32 size;
	u32 virtual_address;
};

struct ElfFile {
	std::vector<u8> image;
	std::vector<ElfSection> sections;
	std::vector<ElfSegment> segments;
	
	ElfSection* lookup_section(const char* name);
	std::optional<u32> file_offset_to_virtual_address(u32 file_offset);
};

// Parse the ELF file header, section headers and program headers.
Result<ElfFile> parse_elf_file(std::vector<u8> image);

Result<void> read_virtual(u8* dest, u32 address, u32 size, const std::vector<ElfFile*>& elves);

template <typename T>
std::vector<T> read_virtual_vector(u32 address, u32 count, const std::vector<ElfFile*>& elves) {
	std::vector<T> result(count);
	read_virtual((u8*) result.data(), address, count * sizeof(T), elves);
	return result;
}

}
