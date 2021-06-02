#include "ccc/ccc.h"

int main(int argc, char** argv) {
	verify(argc == 2, "error: Wrong number of arguments.\n");
	
	Program program;
	program.images.emplace_back(read_program_image(argv[1]));
	parse_elf_file(program, 0);
	
	for(ProgramSection& section : program.sections) {
		if(section.type == ProgramSectionType::MIPS_DEBUG) {
			printf("mdebug ");
		}
		printf("section %lx %lx\n", section.file_offset, section.size);
	}
}
