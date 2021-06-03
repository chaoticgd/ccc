#include "ccc/ccc.h"

void print_address(const char* name, u64 address) {
	fprintf(stderr, "%32s @ 0x%08lx\n", name, address);
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
		print_address("procedure descriptor table", symbol_table.procedure_descriptor_table_offset);
		print_address("local symbol table", symbol_table.local_symbol_table_offset);
		print_address("file descriptor table", symbol_table.file_descriptor_table_offset);
	}
	for(SymFileDescriptor& fd : symbol_table.files) {
		printf("FILE %s:\n", fd.name.c_str());
		for(Symbol& sym : fd.symbols) {
			const char* symbol_type_str = symbol_type(sym.type);
			if(symbol_type_str) {
				printf("\t %s %s\n", symbol_type_str, sym.string.c_str());
			} else {
				printf("\t UNK(%d) %s\n", (u32) sym.type, sym.string.c_str());
			}
		}
	}
}
