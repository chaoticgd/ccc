#include "ccc/ccc.h"

#include <algorithm>
#include <set>

using namespace ccc;

enum class OutputMode {
	PRINT_FUNCTIONS,
	PRINT_GLOBALS,
	PRINT_TYPES,
	PRINT_JSON,
	PRINT_SYMBOLS,
	LIST_FILES,
	HELP,
	BAD_COMMAND
};

enum Flags {
	NO_FLAGS = 0,
	FLAG_PER_FILE = (1 << 0),
	FLAG_VERBOSE = (1 << 1),
	FLAG_OMIT_MEMBER_FUNCTIONS = (1 << 2),
	FLAG_INCLUDE_GENERATED_FUNCTIONS = (1 << 3)
};

struct Options {
	OutputMode mode = OutputMode::BAD_COMMAND;
	fs::path input_file;
	u32 flags;
};

static void print_deduplicated(const SymbolTable& symbol_table, Options options);
static std::vector<std::unique_ptr<ast::Node>> build_deduplicated_ast(std::vector<std::vector<ParsedSymbol>>& symbols, const SymbolTable& symbol_table);
static void print_functions(SymbolTable& symbol_table);
static void print_globals(SymbolTable& symbol_table);
static void print_types_deduplicated(SymbolTable& symbol_table, const Options& options);
static void print_types_per_file(SymbolTable& symbol_table, const Options& options);
static void print_symbols(SymbolTable& symbol_table);
static u32 build_analysis_flags(u32 flags);
static void list_files(SymbolTable& symbol_table);
static SymbolTable read_symbol_table(const fs::path& input_file);
static Options parse_args(int argc, char** argv);
static void print_help();

int main(int argc, char** argv) {
	Options options = parse_args(argc, argv);
	SymbolTable symbol_table;
	if(options.mode == OutputMode::HELP) {
		print_help();
	} else if(options.mode == OutputMode::BAD_COMMAND) {
		print_help();
	} else {
		symbol_table = read_symbol_table(options.input_file);
	}
	switch(options.mode) {
		case OutputMode::PRINT_FUNCTIONS: {
			print_functions(symbol_table);
			return 0;
		}
		case OutputMode::PRINT_GLOBALS: {
			print_globals(symbol_table);
			return 0;
		}
		case OutputMode::PRINT_TYPES: {
			if(!(options.flags & FLAG_PER_FILE)) {
				print_types_deduplicated(symbol_table, options);
			} else {
				print_types_per_file(symbol_table, options);
			}
			return 0;
		}
		case OutputMode::PRINT_JSON: {
			if(!(options.flags & FLAG_PER_FILE)) {
				AnalysisResults results = analyse(symbol_table, DEDUPLICATE_TYPES);
				print_json(stdout, results, false);
			} else {
				AnalysisResults results = analyse(symbol_table, NO_ANALYSIS_FLAGS);
				print_json(stdout, results, true);
			}
			break;
		}
		case OutputMode::PRINT_SYMBOLS: {
			print_symbols(symbol_table);
			return 0;
		}
		case OutputMode::LIST_FILES: {
			list_files(symbol_table);
			return 0;
		}
		case OutputMode::HELP: {
			return 0;
		}
		case OutputMode::BAD_COMMAND: {
			return 1;
		}
	}
}

static void print_functions(SymbolTable& symbol_table) {
	for(s32 i = 0; i < (s32) symbol_table.files.size(); i++) {
		AnalysisResults result = analyse(symbol_table, NO_ANALYSIS_FLAGS, i);
		TranslationUnit& translation_unit = result.translation_units.at(0);
		printf("// *****************************************************************************\n");
		printf("// FILE -- %s\n", translation_unit.full_path.c_str());
		printf("// *****************************************************************************\n");
		printf("\n");
		for(const Function& function : translation_unit.functions) {
			VariableName function_name{&function.name};
			print_cpp_ast_node(stdout, *function.return_type.get(), function_name, 0, 3);
			printf("(");
			for(size_t i = 0; i < function.parameters.size(); i++) {
				const Parameter& parameter = function.parameters[i];
				print_variable_storage_comment(stdout, parameter.storage);
				VariableName parameter_name{&parameter.name};
				print_cpp_ast_node(stdout, *parameter.type.get(), parameter_name, 0, 3);
				if(i != function.parameters.size() - 1) {
					printf(", ");
				}
			}
			printf(") {%s", function.locals.empty() ? "" : "\n");
			for(const LocalVariable& local : function.locals) {
				printf("\t ");
				print_variable_storage_comment(stdout, local.storage);
				VariableName local_name{&local.name};
				print_cpp_ast_node(stdout, *local.type.get(), local_name, 1, 3);
				printf(";\n");
			}
			printf("}\n\n");
		}
	}
}

static void print_globals(SymbolTable& symbol_table) {
	for(s32 i = 0; i < (s32) symbol_table.files.size(); i++) {
		AnalysisResults result = analyse(symbol_table, NO_ANALYSIS_FLAGS, i);
		TranslationUnit& translation_unit = result.translation_units.at(0);
		printf("// *****************************************************************************\n");
		printf("// FILE -- %s\n", translation_unit.full_path.c_str());
		printf("// *****************************************************************************\n");
		printf("\n");
		for(const GlobalVariable& global : translation_unit.globals) {
			VariableName name{&global.name};
			print_cpp_ast_node(stdout, *global.type.get(), name, 0, 3);
			printf(";\n");
		}
		if(!translation_unit.globals.empty() && i != (s32) translation_unit.globals.size() - 1) {
			printf("\n");
		}
	}
}

static void print_types_deduplicated(SymbolTable& symbol_table, const Options& options) {
	u32 analysis_flags = build_analysis_flags(options.flags);
	analysis_flags |= DEDUPLICATE_TYPES;
	AnalysisResults results = analyse(symbol_table, analysis_flags);
	print_cpp_comment_block_beginning(stdout, options.input_file);
	print_cpp_comment_block_compiler_version_info(stdout, symbol_table);
	print_cpp_comment_block_builtin_types(stdout, results.deduplicated_types);
	printf("\n");
	print_cpp_ast_nodes(stdout, results.deduplicated_types, options.flags & FLAG_VERBOSE);
}

static void print_types_per_file(SymbolTable& symbol_table, const Options& options) {
	u32 analysis_flags = build_analysis_flags(options.flags);
	print_cpp_comment_block_beginning(stdout, options.input_file);
	printf("\n");
	for(s32 i = 0; i < (s32) symbol_table.files.size(); i++) {
		AnalysisResults result = analyse(symbol_table, analysis_flags, i);
		TranslationUnit& translation_unit = result.translation_units.at(0);
		printf("// *****************************************************************************\n");
		printf("// FILE -- %s\n", translation_unit.full_path.c_str());
		printf("// *****************************************************************************\n");
		printf("\n");
		print_cpp_comment_block_compiler_version_info(stdout, symbol_table);
		print_cpp_comment_block_builtin_types(stdout, translation_unit.types);
		printf("\n");
		print_cpp_ast_nodes(stdout, translation_unit.types, options.flags & FLAG_VERBOSE);
		printf("\n");
	}
}

static void print_symbols(SymbolTable& symbol_table) {
	for(SymFileDescriptor& fd : symbol_table.files) {
		printf("FILE %s:\n", fd.raw_path.c_str());
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

static u32 build_analysis_flags(u32 flags) {
	u32 analysis_flags = NO_ANALYSIS_FLAGS;
	if(flags & FLAG_OMIT_MEMBER_FUNCTIONS) analysis_flags |= STRIP_MEMBER_FUNCTIONS;
	if(!(flags & FLAG_INCLUDE_GENERATED_FUNCTIONS)) analysis_flags |= STRIP_GENERATED_FUNCTIONS;
	return analysis_flags;
}

static void list_files(SymbolTable& symbol_table) {
	for(const SymFileDescriptor& fd : symbol_table.files) {
		printf("%s\n", fd.full_path.c_str());
	}
}

static SymbolTable read_symbol_table(const fs::path& input_file) {
	Module mod = loaders::read_elf_file(input_file);
	ModuleSection* mdebug_section = mod.lookup_section(".mdebug");
	verify(mdebug_section, "No .mdebug section.");
	return parse_mdebug_section(mod, *mdebug_section);
}

static Options parse_args(int argc, char** argv) {
	Options options;
	if(argc < 2) {
		return options;
	}
	const char* command = argv[1];
	if(strcmp(command, "print_functions") == 0) {
		options.mode = OutputMode::PRINT_FUNCTIONS;
	} else if(strcmp(command, "print_globals") == 0) {
		options.mode = OutputMode::PRINT_GLOBALS;
	} else if(strcmp(command, "print_types") == 0) {
		options.mode = OutputMode::PRINT_TYPES;
	} else if(strcmp(command, "print_json") == 0) {
		options.mode = OutputMode::PRINT_JSON;
	} else if(strcmp(command, "print_symbols") == 0) {
		options.mode = OutputMode::PRINT_SYMBOLS;
	} else if(strcmp(command, "list_files") == 0) {
		options.mode = OutputMode::LIST_FILES;
	} else if(strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
		options.mode = OutputMode::HELP;
	}
	for(s32 i = 2; i < argc - 1; i++) {
		const char* flag = argv[i];
		if(strcmp(flag, "--per-file") == 0) {
			options.flags |= FLAG_PER_FILE;
		} else if(strcmp(flag, "--verbose") == 0) {
			options.flags |= FLAG_VERBOSE;
		} else if(strcmp(flag, "--omit-member-functions") == 0) {
			options.flags |= FLAG_OMIT_MEMBER_FUNCTIONS;
		} else if(strcmp(flag, "--include-generated-functions") == 0) {
			options.flags |= FLAG_INCLUDE_GENERATED_FUNCTIONS;
		} else {
			options.mode = OutputMode::BAD_COMMAND;
			return options;
		}
	}
	options.input_file = fs::path(std::string(argv[argc - 1]));
	return options;
}

static void print_help() {
	puts("stdump -- https://github.com/chaoticgd/ccc");
	puts("  MIPS/STABS symbol table parser.");
	puts("");
	puts("Commands:");
	puts("  print_functions <input file>");
	puts("    Print all the functions recovered from the STABS symbols as C++.");
	puts("");
	puts("  print_globals <input file>");
	puts("    Print all the global variables recovered from the STABS symbols as C++.");
	puts("");
	puts("  print_types [options] <input file>");
	puts("    Print all the types recovered from the STABS symbols as C++.");
	puts("");
	puts("    --per-file                    Do not deduplicate types from files.");
	puts("    --verbose                     Print additional information such as the raw");
	puts("                                  STABS symbol along with each type.");
	puts("    --omit-member-functions       Do not print member functions.");
	puts("    --include-generated-functions Include member functions that are likely");
	puts("                                  auto-generated.");
	puts("");
	puts("  print_json <input file>");
	puts("    Print all of the above as JSON.");
	puts("");
	puts("    --per-file                    Do not deduplicate types from files.");
	puts("");
	puts("  print_symbols <input file>");
	puts("    List all of the local symbols for each file.");
	puts("");
	puts("  list_files <input_file>");
	puts("    List the names of each of the source files.");
	puts("");
	puts("  help | --help | -h");
	puts("    Print this help message.");
}
