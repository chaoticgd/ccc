// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "elf.h"

namespace ccc {

enum class ElfIdentClass : u8 {
	B32 = 0x1,
	B64 = 0x2
};

enum class ElfFileType : u16 {
	NONE   = 0x00,
	REL    = 0x01,
	EXEC   = 0x02,
	DYN    = 0x03,
	CORE   = 0x04,
	LOOS   = 0xfe00,
	HIOS   = 0xfeff,
	LOPROC = 0xff00,
	HIPROC = 0xffff
};

enum class ElfMachine : u16 {
	MIPS = 0x08
};

CCC_PACKED_STRUCT(ElfIdentHeader,
	/* 0x0 */ u32 magic; // 7f 45 4c 46
	/* 0x4 */ ElfIdentClass e_class;
	/* 0x5 */ u8 endianess;
	/* 0x6 */ u8 version;
	/* 0x7 */ u8 os_abi;
	/* 0x8 */ u8 abi_version;
	/* 0x9 */ u8 pad[7];
)

CCC_PACKED_STRUCT(ElfFileHeader,
	/* 0x10 */ ElfFileType type;
	/* 0x12 */ ElfMachine machine;
	/* 0x14 */ u32 version;
	/* 0x18 */ u32 entry;
	/* 0x1c */ u32 phoff;
	/* 0x20 */ u32 shoff;
	/* 0x24 */ u32 flags;
	/* 0x28 */ u16 ehsize;
	/* 0x2a */ u16 phentsize;
	/* 0x2c */ u16 phnum;
	/* 0x2e */ u16 shentsize;
	/* 0x30 */ u16 shnum;
	/* 0x32 */ u16 shstrndx;
)

CCC_PACKED_STRUCT(ElfProgramHeader,
	/* 0x00 */ u32 type;
	/* 0x04 */ u32 offset;
	/* 0x08 */ u32 vaddr;
	/* 0x0c */ u32 paddr;
	/* 0x10 */ u32 filesz;
	/* 0x14 */ u32 memsz;
	/* 0x18 */ u32 flags;
	/* 0x1c */ u32 align;
)

CCC_PACKED_STRUCT(ElfSectionHeader,
	/* 0x00 */ u32 name;
	/* 0x04 */ ElfSectionType type;
	/* 0x08 */ u32 flags;
	/* 0x0c */ u32 addr;
	/* 0x10 */ u32 offset;
	/* 0x14 */ u32 size;
	/* 0x18 */ u32 link;
	/* 0x1c */ u32 info;
	/* 0x20 */ u32 addralign;
	/* 0x24 */ u32 entsize;
)

const ElfSection* ElfFile::lookup_section(const char* name) const
{
	for(const ElfSection& section : sections) {
		if(section.name == name) {
			return &section;
		}
	}
	return nullptr;
}

std::optional<u32> ElfFile::file_offset_to_virtual_address(u32 file_offset) const
{
	for(const ElfSegment& segment : segments) {
		if(file_offset >= segment.offset && file_offset < segment.offset + segment.size) {
			return segment.address.get_or_zero() + file_offset - segment.offset;
		}
	}
	return std::nullopt;
}

Result<ElfFile> parse_elf_file(std::vector<u8> image)
{
	ElfFile elf;
	elf.image = std::move(image);
	
	const ElfIdentHeader* ident = get_packed<ElfIdentHeader>(elf.image, 0);
	CCC_CHECK(ident, "ELF ident header out of range.");
	CCC_CHECK(ident->magic == CCC_FOURCC("\x7f\x45\x4c\x46"), "Not an ELF file.");
	CCC_CHECK(ident->e_class == ElfIdentClass::B32, "Wrong ELF class (not 32 bit).");
	
	const ElfFileHeader* header = get_packed<ElfFileHeader>(elf.image, sizeof(ElfIdentHeader));
	CCC_CHECK(ident, "ELF file header out of range.");
	CCC_CHECK(header->machine == ElfMachine::MIPS, "Wrong architecture.");
	
	const ElfSectionHeader* shstr_section_header = get_packed<ElfSectionHeader>(elf.image, header->shoff + header->shstrndx * sizeof(ElfSectionHeader));
	CCC_CHECK(shstr_section_header, "ELF section name header out of range.");
	
	for(u32 i = 0; i < header->shnum; i++) {
		u64 header_offset = header->shoff + i * sizeof(ElfSectionHeader);
		const ElfSectionHeader* section_header = get_packed<ElfSectionHeader>(elf.image, header_offset);
		CCC_CHECK(section_header, "ELF section header out of range.");
		
		const char* name = get_string(elf.image, shstr_section_header->offset + section_header->name);
		CCC_CHECK(section_header, "ELF section name out of range.");
		
		ElfSection& section = elf.sections.emplace_back();
		section.name = name;
		section.type = section_header->type;
		section.offset = section_header->offset;
		section.size = section_header->size;
		if(section_header->addr != 0) {
			section.address = section_header->addr;
		}
		section.link = section_header->link;
	}
	
	for(u32 i = 0; i < header->phnum; i++) {
		u64 header_offset = header->phoff + i * sizeof(ElfProgramHeader);
		const ElfProgramHeader* program_header = get_packed<ElfProgramHeader>(elf.image, header_offset);
		CCC_CHECK(program_header, "ELF program header out of range.");
		
		ElfSegment& segment = elf.segments.emplace_back();
		segment.offset = program_header->offset;
		segment.size = program_header->filesz;
		if(program_header->vaddr != 0) {
			segment.address = program_header->vaddr;
		}
	}
	
	return elf;
}

Result<void> import_elf_section_headers(
	SymbolDatabase& database, const ElfFile& elf, SymbolSourceHandle source)
{
	for(const ElfSection& section : elf.sections) {
		Result<Section*> symbol = database.sections.create_symbol(section.name, source, section.address);
		CCC_RETURN_IF_ERROR(symbol);
		
		(*symbol)->set_size(section.size);
	}
	
	return Result<void>();
}

Result<void> read_virtual(u8* dest, u32 address, u32 size, const std::vector<ElfFile*>& elves)
{
	while(size > 0) {
		bool mapped = false;
		
		for(const ElfFile* elf : elves) {
			for(const ElfSegment& segment : elf->segments) {
				if(address >= segment.address && address < segment.address.get_or_zero() + segment.size) {
					u32 offset = address - segment.address.get_or_zero();
					u32 copy_size = std::min(segment.size - offset, size);
					CCC_CHECK(segment.offset + offset + copy_size <= elf->image.size(), "Program header is corrupted or executable file is truncated.");
					memcpy(dest, &elf->image[segment.offset + offset], copy_size);
					dest += copy_size;
					address += copy_size;
					size -= copy_size;
					mapped = true;
				}
			}
		}
		
		CCC_CHECK(mapped, "Tried to read from memory that wouldn't have come from any of the loaded ELF files");
	}
	return Result<void>();
}

}
