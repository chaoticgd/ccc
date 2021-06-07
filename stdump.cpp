#include "ccc/ccc.h"

#include <algorithm>
#include <set>

void print_address(const char* name, u64 address) {
	fprintf(stderr, "%32s @ 0x%08lx\n", name, address);
}

enum OutputMode : u32 {
	OUTPUT_HELP = 0,
	OUTPUT_SYMBOLS = 1,
	OUTPUT_TYPES = 2
};

struct Options {
	OutputMode mode = OUTPUT_HELP;
	fs::path input_file;
	bool verbose = false;
};

Options parse_args(int argc, char** argv);
void print_symbols(Program& program, SymbolTable& symbol_table);
void print_types(Program& program, SymbolTable& symbol_table, bool verbose);
void print_help();

int main(int argc, char** argv) {
	Options options = parse_args(argc, argv);
	if(options.mode == OUTPUT_HELP) {
		print_help();
		exit(1);
	}
	
	Program program;
	program.images.emplace_back(read_program_image(options.input_file));
	parse_elf_file(program, 0);
	
	SymbolTable symbol_table;
	bool has_symbol_table = false;
	for(ProgramSection& section : program.sections) {
		if(section.type == ProgramSectionType::MIPS_DEBUG) {
			if(options.verbose) {
				print_address("mdebug section", section.file_offset);
			}
			symbol_table = parse_symbol_table(program.images[0], section);
			has_symbol_table = true;
		}
	}
	verify(has_symbol_table, "No symbol table.\n");
	if(options.verbose) {
		print_address("procedure descriptor table", symbol_table.procedure_descriptor_table_offset);
		print_address("local symbol table", symbol_table.local_symbol_table_offset);
		print_address("file descriptor table", symbol_table.file_descriptor_table_offset);
	}
	
	if(options.mode & OUTPUT_SYMBOLS) {
		print_symbols(program, symbol_table);
	}
	if(options.mode & OUTPUT_TYPES) {
		print_types(program, symbol_table, options.verbose);
	}
}

Options parse_args(int argc, char** argv) {
	Options options;
	for(int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if(arg == "--symbols" || arg == "-s") {
			(u32&) options.mode |= OUTPUT_SYMBOLS;
		}
		if(arg == "--types" || arg == "-t") {
			(u32&) options.mode |= OUTPUT_TYPES;
		}
		if(arg == "--verbose" || arg == "-v") {
			options.verbose = true;
		}
	}
	for(int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if(arg == "--symbols" || arg == "-s") {
			continue;
		}
		if(arg == "--types" || arg == "-t") {
			continue;
		}
		if(arg == "--verbose" || arg == "-v") {
			continue;
		}
		verify(options.input_file.empty(), "error: Multiple input files specified.\n");
		options.input_file = arg;
	}
	return options;
}

void print_symbols(Program& program, SymbolTable& symbol_table) {
	for(SymFileDescriptor& fd : symbol_table.files) {
		printf("FILE %s:\n", fd.name.c_str());
		for(Symbol& sym : fd.symbols) {
			const char* symbol_type_str = symbol_type(sym.storage_type);
			const char* symbol_class_str = symbol_class(sym.storage_class);
			printf("\t%8x ", sym.value);
			if(symbol_type_str) {
				printf("%11s ", symbol_type_str);
			} else {
				printf("ST(%5d) ", (u32) sym.storage_type);
			}
			if(symbol_class_str) {
				printf("%6s ", symbol_class_str);
			} else if ((u32)sym.storage_class == 0) {
				printf("       ");
			} else {
				printf("SC(%2d) ", (u32) sym.storage_class);
			}
			printf("%8d %s\n", sym.index, sym.string.c_str());
		}
	}
}

void print_types(Program& program, SymbolTable& symbol_table, bool verbose) {
	std::set<std::string> declared_symbols;

	for(SymFileDescriptor& fd : symbol_table.files) {
		std::string prefix;
		for(Symbol& sym : fd.symbols) {
			if(sym.storage_type == SymbolType::NIL && (u32) sym.storage_class == 0) {
				if(sym.string.find("@") == 0 || sym.string.find("$") == 0 || sym.string.size() == 0) {
					continue;
				}
				// Some STABS symbols are split between multiple strings.
				if(sym.string[sym.string.size() - 1] == '\\') {
					prefix += sym.string.substr(0, sym.string.size() - 1);
				} else {
					const std::string full_symbol = prefix + sym.string;

					if(verbose)
						printf("*** PARSING %s\n", full_symbol.c_str());

					const StabsSymbol stab_sym = parse_stabs_symbol(full_symbol.c_str(), verbose);
					const std::string sym_name = stab_sym.name;

					if (declared_symbols.count(sym_name) == 0) {
						auto aggregate_max_name_size = [](const auto& fields) -> size_t {
							if(fields.empty())
								return 0;
							return std::max_element(fields.begin(), fields.end(),
								[](const std::pair<std::string, s64>& f1, const std::pair<std::string, s64>& f2) {
									return f1.first.size() < f2.first.size();
								})->first.size();
						};

						switch (stab_sym.type.descriptor) {
						case StabsTypeDescriptor::ENUM: {
							const auto& fields = stab_sym.type.enum_type.fields;
							const u32 max_name_size = aggregate_max_name_size(fields);
							printf("typedef enum %s {\n", sym_name.c_str());
							for (const auto [name, value] : fields)
								printf("\t%-*s = 0x%X,\n", max_name_size, name.c_str(), (s32) value);
							printf("} %s;\n", sym_name.c_str());
							break;
						}
						}

						declared_symbols.insert(sym_name);
					}
					else { // Forward declare
						switch (stab_sym.type.descriptor) {
						case StabsTypeDescriptor::STRUCT: {
							printf("struct %s;\n", sym_name.c_str());
							break;
						}
						case StabsTypeDescriptor::ENUM: {
							printf("enum %s;\n", sym_name.c_str());
							break;
						}
						}
					}

					prefix = "";
				}
			}
		}
	}
}

void print_help() {
	puts("stdump: MIPS/GCC symbol table parser.");
	puts("");
	puts("OPTIONS:");
	puts(" --symbols, -s      Print a list of all the local symbols, grouped");
	puts("                    by file descriptor.");
	puts("");
	puts(" --types, -t        TODO");
	puts("");
	puts(" --verbose, -v      Print out addition information e.g. the offsets of");
	puts("                    various data structures in the input file.");
}
