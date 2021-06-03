#include "ccc/ccc.h"

void print_address(const char* name, u64 address) {
	printf("%32s @ 0x%08lx\n", name, address);
}

int main(int argc, char** argv) {
	verify(argc == 2, "error: Wrong number of arguments.\n");
	
	bool verbose = true;
	
	Program program;
	program.images.emplace_back(read_program_image(argv[1]));
	parse_elf_file(program, 0);
	
	SymbolTable symbol_table;
	bool has_symbol_table = false;
	for(ProgramSection& section : program.sections) {
		if(section.type == ProgramSectionType::MIPS_DEBUG) {
			if(verbose) {
				print_address("mdebug section", section.file_offset);
			}
			symbol_table = parse_symbol_table(program.images[0], section);
			has_symbol_table = true;
		}
	}
	verify(has_symbol_table, "No symbol table.\n");
	if(verbose) {
		print_address("file descriptor table", symbol_table.file_descriptor_table_offset);
	}
	printf("FILE DESCRIPTORS:\n");
	for(SymFileDescriptor& fd : symbol_table.files) {
		printf("\t%s\n", fd.name.c_str());
	}
}
