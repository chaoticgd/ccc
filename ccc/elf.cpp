#include "elf.h"

namespace ccc::loaders {

static void parse_elf_file(Module& mod);

Module read_elf_file(fs::path path) {
	Module mod;
	mod.image = read_binary_file(path);
	parse_elf_file(mod);
	return mod;
}

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

packed_struct(ElfIdentHeader,
	/* 0x0 */ u8 magic[4]; // 7f 45 4c 46
	/* 0x4 */ ElfIdentClass e_class;
	/* 0x5 */ u8 endianess;
	/* 0x6 */ u8 version;
	/* 0x7 */ u8 os_abi;
	/* 0x8 */ u8 abi_version;
	/* 0x9 */ u8 pad[7];
)

packed_struct(ElfFileHeader32,
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

packed_struct(ElfProgramHeader32,
	/* 0x00 */ u32 type;
	/* 0x04 */ u32 offset;
	/* 0x08 */ u32 vaddr;
	/* 0x0c */ u32 paddr;
	/* 0x10 */ u32 filesz;
	/* 0x14 */ u32 memsz;
	/* 0x18 */ u32 flags;
	/* 0x1c */ u32 align;
)

packed_struct(ElfSectionHeader32,
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

void parse_elf_file(Module& mod) {
	const auto& ident = get_packed<ElfIdentHeader>(mod.image, 0, "ELF ident bytes");
	verify(memcmp(ident.magic, "\x7f\x45\x4c\x46", 4) == 0, "Invalid ELF file.");
	verify(ident.e_class == ElfIdentClass::B32, "Wrong ELF class (not 32 bit).");
	
	const auto& header = get_packed<ElfFileHeader32>(mod.image, sizeof(ElfIdentHeader), "ELF file header");
	verify(header.machine == ElfMachine::MIPS, "Wrong architecture.");
	
	for(u32 i = 0; i < header.phnum; i++) {
		u64 header_offset = header.phoff + i * sizeof(ElfProgramHeader32);
		const auto& program_header = get_packed<ElfProgramHeader32>(mod.image, header_offset, "ELF program header");
		ModuleSegment& segment = mod.segments.emplace_back();
		segment.file_offset = program_header.offset;
		segment.size = program_header.filesz;
		segment.virtual_address = program_header.vaddr;
	}
	
	for(u32 i = 0; i < header.shnum; i++) {
		u64 header_offset = header.shoff + i * sizeof(ElfSectionHeader32);
		const auto& section_header = get_packed<ElfSectionHeader32>(mod.image, header_offset, "ELF section header");
		ModuleSection& section = mod.sections.emplace_back();
		section.file_offset = section_header.offset;
		section.size = section_header.size;
		section.type = section_header.type;
		section.name_offset = section_header.name;
	}
	
	if(header.shstrndx < mod.sections.size()) {
		for(ModuleSection& section : mod.sections) {
			section.name = get_string(mod.image, mod.sections[header.shstrndx].file_offset + section.name_offset);
		}
	}
}

}
