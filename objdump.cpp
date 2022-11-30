#include "ccc/ccc.h"

using namespace ccc;

int main(int argc, char** argv) {
	verify(argc == 2, "Incorrect number of arguments.");
	
	Program program;
	program.images.emplace_back(read_program_image(fs::path(argv[1])));
	parse_elf_file(program, 0);
	
	
	for(const ProgramSection& section : program.sections) {
		printf("s %s\n", section.name.c_str());
		if(section.name == ".text") {
			for(u64 i = 0; i < section.size; i += 4) {
				Insn insn(program.images[0].bytes.at(section.file_offset + i));
				printf("%s\n", insn.info().mnemonic);
			}
			break;
		}
	}
}
