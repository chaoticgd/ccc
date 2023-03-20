#ifndef _CCC_MODULE_H
#define _CCC_MODULE_H

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

struct ModuleSection {
	u32 file_offset = -1;
	u32 size = -1;
	ElfSectionType type;
	u32 name_offset = -1;
	std::string name;
	u32 virtual_address = -1;
};

struct ModuleSegment {
	u32 file_offset;
	u32 size;
	u32 virtual_address;
};

struct Module {
	std::vector<u8> image;
	std::vector<ModuleSection> sections;
	std::vector<ModuleSegment> segments;
	
	ModuleSection* lookup_section(const char* name);
	u32 file_offset_to_virtual_address(u32 file_offset);
};

void read_virtual(u8* dest, u32 address, u32 size, const std::vector<Module*>& modules);

template <typename T>
std::vector<T> read_virtual_vector(u32 address, u32 count, const std::vector<Module*>& modules) {
	std::vector<T> result(count);
	read_virtual((u8*) result.data(), address, count * sizeof(T), modules);
	return result;
}
};

#endif
