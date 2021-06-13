#include "ccc/ccc.h"

#include <algorithm>
#include <set>

void print_address(const char* name, u64 address) {
	fprintf(stderr, "%32s @ 0x%08lx\n", name, address);
}

enum OutputMode : u32 {
	OUTPUT_DEFAULT,
	OUTPUT_HELP,
	OUTPUT_SYMBOLS,
	OUTPUT_FILES,
	OUTPUT_TYPES,
	OUTPUT_PER_FILE,
	OUTPUT_TEST
};

struct Options {
	OutputMode mode = OUTPUT_DEFAULT;
	fs::path input_file;
	bool verbose = false;
};

static void print_symbols(SymbolTable& symbol_table);
static void print_files(SymbolTable& symbol_table);
static void print_c_deduplicated(const SymbolTable& symbol_table, bool verbose);
static void print_c_per_file(const SymbolTable& symbol_table, bool verbose);
static void print_c_test(const SymbolTable& symbol_table);
static Options parse_args(int argc, char** argv);
static void print_help();

int main(int argc, char** argv) {
	Options options = parse_args(argc, argv);
	if(options.mode == OUTPUT_DEFAULT || options.mode == OUTPUT_HELP) {
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
	switch(options.mode) {
		case OUTPUT_SYMBOLS:
			print_symbols(symbol_table);
			break;
		case OUTPUT_FILES:
			print_files(symbol_table);
			break;
		case OUTPUT_TYPES:
			print_c_deduplicated(symbol_table, options.verbose);
			break;
		case OUTPUT_PER_FILE:
			print_c_per_file(symbol_table, options.verbose);
			break;
		case OUTPUT_TEST:
			print_c_test(symbol_table);
			break;
	}
}

static void print_symbols(SymbolTable& symbol_table) {
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

static void print_files(SymbolTable& symbol_table) {
	for(const SymFileDescriptor& fd : symbol_table.files) {
		printf("%s\n", fd.name.c_str());
	}
}

static std::vector<AstNode> symbols_to_ast(const std::vector<StabsSymbol>& symbols, const std::map<s32, TypeName>& type_names) {
	auto is_data_type = [&](const StabsSymbol& symbol) {
		return symbol.mdebug_symbol.storage_type == SymbolType::NIL
			&& (u32) symbol.mdebug_symbol.storage_class == 0
			&& (symbol.descriptor == StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG
				|| symbol.descriptor == StabsSymbolDescriptor::TYPE_NAME);
	};
	
	std::vector<AstNode> ast_nodes;
	for(const StabsSymbol& symbol : symbols) {
		if(is_data_type(symbol)) {
			std::optional<AstNode> node = stabs_symbol_to_ast(symbol, type_names);
			if(node.has_value()) {
				node->top_level = true;
				node->symbol = &symbol;
				ast_nodes.emplace_back(std::move(*node));
			}
		}
	}
	return ast_nodes;
}

static std::vector<AstNode> build_deduplicated_ast(std::vector<std::vector<StabsSymbol>>& symbols, const SymbolTable& symbol_table) {
	std::vector<std::pair<std::string, std::vector<AstNode>>> per_file_ast;
	for(const SymFileDescriptor& fd : symbol_table.files) {
		symbols.emplace_back(parse_stabs_symbols(fd.symbols));
		const std::map<s32, const StabsType*> types = enumerate_numbered_types(symbols.back());
		const std::map<s32, TypeName> type_names = resolve_c_type_names(types);
		per_file_ast.emplace_back(fd.name, symbols_to_ast(symbols.back(), type_names));
	}
	return deduplicate_ast(per_file_ast);
}

static void print_c_deduplicated(const SymbolTable& symbol_table, bool verbose) {
	std::vector<std::vector<StabsSymbol>> symbols;
	const std::vector<AstNode> ast_nodes = build_deduplicated_ast(symbols, symbol_table);
	
	print_forward_declarations(ast_nodes);
	print_ast_begin(stdout);
	bool last_node_was_struct_or_union = true;
	for(const AstNode& node : ast_nodes) {
		bool node_is_struct_or_union =
			node.descriptor == AstNodeDescriptor::STRUCT ||
			node.descriptor == AstNodeDescriptor::UNION;
		if(node_is_struct_or_union || last_node_was_struct_or_union) {
			printf("\n");
		}
		last_node_was_struct_or_union = node_is_struct_or_union;
		
		assert(node.symbol);
		if(verbose) {
			printf("// %s\n", node.name.c_str());
		}
		if(node.conflicting_types) {
			printf("// warning: multiple differing types with the same name, only one recovered\n");
		}
		if(verbose) {
			printf("// symbol:\n");
			printf("//   %s\n", node.symbol->raw.c_str());
			printf("// used by:\n");
			for(const std::string& source_file : node.source_files) {
				printf("//   %s\n", source_file.c_str());
			}
		}
		print_ast_node(stdout, node, 0, 0);
	}
}

static void print_c_per_file(const SymbolTable& symbol_table, bool verbose) {
	for(const SymFileDescriptor& fd : symbol_table.files) {
		const std::vector<StabsSymbol> symbols = parse_stabs_symbols(fd.symbols);
		const std::map<s32, const StabsType*> types = enumerate_numbered_types(symbols);
		const std::map<s32, TypeName> type_names = resolve_c_type_names(types);
		const std::vector<AstNode> ast_nodes = symbols_to_ast(symbols, type_names);
		
		printf("// *****************************************************************************\n");
		printf("// FILE -- %s\n", fd.name.c_str());
		printf("// *****************************************************************************\n");
		printf("\n");
		print_ast_begin(stdout);
		for(const AstNode& node : ast_nodes) {
			assert(node.symbol);
			printf("// %s\n", node.symbol->raw.c_str());
			print_ast_node(stdout, node, 0, 0);
			printf("\n");
		}
	}
}

static void print_c_test(const SymbolTable& symbol_table) {
	std::vector<std::vector<StabsSymbol>> symbols;
	const std::vector<AstNode> ast_nodes = build_deduplicated_ast(symbols, symbol_table);
	
	print_ast_begin(stdout);
	print_forward_declarations(ast_nodes);
	for(const AstNode& node : ast_nodes) {
		assert(node.symbol);
		printf("// %s\n", node.symbol->raw.c_str());
		print_ast_node(stdout, node, 0, 0);
		printf("\n");
	}
	printf("#define CCC_OFFSETOF(type, field) ((int) &((type*) 0)->field)\n");
	for(const AstNode& node : ast_nodes) {
		if(node.descriptor == AstNodeDescriptor::STRUCT) {
			for(const AstNode& field : node.struct_or_union.fields) {
				if(!field.is_static) {
					printf("typedef int o_%s__%s[(CCC_OFFSETOF(%s,%s)==%d)?1:-1];\n",
						node.name.c_str(), field.name.c_str(),
						node.name.c_str(), field.name.c_str(), field.offset / 8);
					printf("typedef int s_%s__%s[(sizeof(%s().%s)==%d)?1:-1];\n",
						node.name.c_str(), field.name.c_str(),
						node.name.c_str(), field.name.c_str(), field.size / 8);
				}
			}
		}
	}
}

static Options parse_args(int argc, char** argv) {
	Options options;
	auto only_one = [&]() {
		verify(options.mode == OUTPUT_DEFAULT, "error: Multiple mode flags specified.\n");
	};
	for(int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if(arg == "--symbols" || arg == "-s") {
			only_one();
			options.mode = OUTPUT_SYMBOLS;
			continue;
		}
		if(arg == "--files" || arg == "-f") {
			only_one();
			options.mode = OUTPUT_FILES;
			continue;
		}
		if(arg == "--types" || arg == "-t") {
			only_one();
			options.mode = OUTPUT_TYPES;
			continue;
		}
		if(arg == "--per-file" || arg == "-p") {
			only_one();
			options.mode = OUTPUT_PER_FILE;
			continue;
		}
		if(arg == "--test" || arg == "-t") {
			only_one();
			options.mode = OUTPUT_TEST;
			continue;
		}
		if(arg == "--verbose" || arg == "-v") {
			options.verbose = true;
			continue;
		}
		verify(options.input_file.empty(), "error: Multiple input files specified.\n");
		options.input_file = arg;
	}
	return options;
}

void print_help() {
	puts("stdump: MIPS/GCC symbol table parser.");
	puts("");
	puts("OPTIONS:");
	puts(" --symbols, -s      Print a list of all the local symbols, grouped");
	puts("                    by file descriptor.");
	puts("");
	puts(" --files, -f        Print a list of all the file descriptors.");
	puts("");
	puts(" --types, -t        Print a deduplicated list of all the types defined");
	puts("                    in the MIPS debug section as C data structures.");
	puts("");
	puts(" --per-file, -t     Print a list of all the types defined in the MIPS");
	puts("                    debug section, where for each file descriptor");
	puts("                    all types used are duplicated.");
	puts("");
	puts(" --test             Print a C++ program containing static assertions");
	puts("                    to test if the offset and size of each field is");
	puts("                    correct for a given platform/compiler.");
	puts("");
	puts("                    Currently only symbols from the second file");
	puts("                    descriptor are used.");
	puts("");
	puts(" --verbose, -v      Print out additional information e.g. the offsets");
	puts("                    of various data structures in the input file.");
}
