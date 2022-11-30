#include "elf.h"

namespace ccc {

ProgramImage read_program_image(fs::path path) {
	verify(fs::exists(path), "error: file doesn't exist\n");

	ProgramImage image;
	image.bytes = read_file_bin(path);
	return image;
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
	u8 magic[4];           // 0x0 7f 45 4c 46
	ElfIdentClass e_class; // 0x4
	u8 endianess;          // 0x5
	u8 version;            // 0x6
	u8 os_abi;             // 0x7
	u8 abi_version;        // 0x8
	u8 pad[7];             // 0x9
)

packed_struct(ElfFileHeader32,
	ElfFileType type;   // 0x10
	ElfMachine machine; // 0x12
	u32 version;        // 0x14
	u32 entry;          // 0x18
	u32 phoff;          // 0x1c
	u32 shoff;          // 0x20
	u32 flags;          // 0x24
	u16 ehsize;         // 0x28
	u16 phentsize;      // 0x2a
	u16 phnum;          // 0x2c
	u16 shentsize;      // 0x2e
	u16 shnum;          // 0x30
	u16 shstrndx;       // 0x32
)

packed_struct(ElfProgramHeader32,
	u32 type;   // 0x0
	u32 offset; // 0x4
	u32 vaddr;  // 0x8
	u32 paddr;  // 0xc
	u32 filesz; // 0x10
	u32 memsz;  // 0x14
	u32 flags;  // 0x18
	u32 align;  // 0x1c
)

packed_struct(ElfSectionHeader32,
	u32 name;            // 0x0
	ElfSectionType type; // 0x4
	u32 flags;           // 0x8
	u32 addr;            // 0xc
	u32 offset;          // 0x10
	u32 size;            // 0x14
	u32 link;            // 0x18
	u32 info;            // 0x1c
	u32 addralign;       // 0x20
	u32 entsize;         // 0x24
)

void parse_elf_file(Program& program, u64 image_index) {
	const ProgramImage& image = program.images[image_index];
	
	const auto& ident = get_packed<ElfIdentHeader>(image.bytes, 0, "ELF ident bytes");
	verify(memcmp(ident.magic, "\x7f\x45\x4c\x46", 4) == 0, "error: Invalid ELF file.\n");
	verify(ident.e_class == ElfIdentClass::B32, "error: Wrong ELF class (not 32 bit).\n");
	
	const auto& header = get_packed<ElfFileHeader32>(image.bytes, sizeof(ElfIdentHeader), "ELF file header");
	verify(header.machine == ElfMachine::MIPS, "error: Wrong architecture.\n");
	
	for(s32 i = 0; i < header.shnum; i++) {
		u64 offset = header.shoff + i * sizeof(ElfSectionHeader32);
		const auto& section_header = get_packed<ElfSectionHeader32>(image.bytes, offset, "ELF section header");
		ProgramSection& section = program.sections.emplace_back();
		section.image = image_index;
		section.file_offset = section_header.offset;
		section.size = section_header.size;
		section.type = section_header.type;
		section.name_offset = section_header.name;
	}
	
	if(header.shstrndx < program.sections.size()) {
		for(ProgramSection& section : program.sections) {
			section.name = read_string(image.bytes, program.sections[header.shstrndx].file_offset + section.name_offset);
		}
	}
}

}
