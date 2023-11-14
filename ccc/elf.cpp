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
	MIPS  = 0x08
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

CCC_PACKED_STRUCT(ElfFileHeader32,
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

CCC_PACKED_STRUCT(ElfProgramHeader32,
	/* 0x00 */ u32 type;
	/* 0x04 */ u32 offset;
	/* 0x08 */ u32 vaddr;
	/* 0x0c */ u32 paddr;
	/* 0x10 */ u32 filesz;
	/* 0x14 */ u32 memsz;
	/* 0x18 */ u32 flags;
	/* 0x1c */ u32 align;
)

CCC_PACKED_STRUCT(ElfSectionHeader32,
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

ElfSection* ElfFile::lookup_section(const char* name) {
	for(ElfSection& section : sections) {
		if(section.name == name) {
			return &section;
		}
	}
	return nullptr;
}

std::optional<u32> ElfFile::file_offset_to_virtual_address(u32 file_offset) {
	for(ElfSegment& segment : segments) {
		if(file_offset >= segment.file_offset && file_offset < segment.file_offset + segment.size) {
			return segment.virtual_address + file_offset - segment.file_offset;
		}
	}
	return std::nullopt;
}

Result<ElfFile> parse_elf_file(std::vector<u8> image) {
	ElfFile elf;
	elf.image = std::move(image);
	
	const ElfIdentHeader* ident = get_packed<ElfIdentHeader>(elf.image, 0);
	CCC_CHECK(ident, "ELF ident out of range.");
	CCC_CHECK(ident->magic == CCC_FOURCC("\x7f\x45\x4c\x46"), "Invalid ELF file.");
	CCC_CHECK(ident->e_class == ElfIdentClass::B32, "Wrong ELF class (not 32 bit).");
	
	const ElfFileHeader32* header = get_packed<ElfFileHeader32>(elf.image, sizeof(ElfIdentHeader));
	CCC_CHECK(ident, "ELF file header out of range.");
	CCC_CHECK(header->machine == ElfMachine::MIPS, "Wrong architecture.");
	
	for(u32 i = 0; i < header->phnum; i++) {
		u64 header_offset = header->phoff + i * sizeof(ElfProgramHeader32);
		const ElfProgramHeader32* program_header = get_packed<ElfProgramHeader32>(elf.image, header_offset);
		CCC_CHECK(program_header, "ELF program header out of range.");
		
		ElfSegment& segment = elf.segments.emplace_back();
		segment.file_offset = program_header->offset;
		segment.size = program_header->filesz;
		segment.virtual_address = program_header->vaddr;
	}
	
	for(u32 i = 0; i < header->shnum; i++) {
		u64 header_offset = header->shoff + i * sizeof(ElfSectionHeader32);
		const auto& section_header = get_packed<ElfSectionHeader32>(elf.image, header_offset);
		CCC_CHECK(section_header, "ELF section header out of range.");
		
		ElfSection& section = elf.sections.emplace_back();
		section.file_offset = section_header->offset;
		section.size = section_header->size;
		section.type = section_header->type;
		section.name_offset = section_header->name;
		section.virtual_address = section_header->addr;
	}
	
	if(header->shstrndx < elf.sections.size()) {
		for(ElfSection& section : elf.sections) {
			Result<const char*> name = get_string(elf.image, elf.sections[header->shstrndx].file_offset + section.name_offset);
			CCC_CHECK(name.success(), "Section name out of bounds.");
			section.name = *name;
		}
	}
	
	return elf;
}

Result<void> read_virtual(u8* dest, u32 address, u32 size, const std::vector<ElfFile*>& elves) {
	while(size > 0) {
		bool mapped = false;
		
		for(const ElfFile* elf : elves) {
			for(const ElfSegment& segment : elf->segments) {
				if(address >= segment.virtual_address && address < segment.virtual_address + segment.size) {
					u32 offset = address - segment.virtual_address;
					u32 copy_size = std::min(segment.size - offset, size);
					CCC_CHECK(segment.file_offset + offset + copy_size <= elf->image.size(), "Program header is corrupted or executable file is truncated.");
					memcpy(dest, &elf->image[segment.file_offset + offset], copy_size);
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
