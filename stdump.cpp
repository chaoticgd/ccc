#include "ccc/ccc.h"

#include <algorithm>

using namespace ccc;

enum class OutputMode {
	FUNCTIONS,
	GLOBALS,
	TYPES,
	JSON,
	MDEBUG,
	SYMBOLS,
	EXTERNALS,
	FILES,
	SECTIONS,
	TYPE_GRAPH,
	TEST,
	HELP,
	BAD_COMMAND
};

enum Flags {
	NO_FLAGS = 0,
	FLAG_PER_FILE = (1 << 0),
	FLAG_VERBOSE = (1 << 1),
	FLAG_OMIT_ACCESS_SPECIFIERS = (1 << 2),
	FLAG_OMIT_MEMBER_FUNCTIONS = (1 << 3),
	FLAG_INCLUDE_GENERATED_FUNCTIONS = (1 << 4)
};

struct Options {
	OutputMode mode = OutputMode::BAD_COMMAND;
	fs::path input_file;
	fs::path output_file;
	u32 flags = NO_FLAGS;
};

static void print_deduplicated(const mdebug::SymbolTable& symbol_table, Options options);
static std::vector<std::unique_ptr<ast::Node>> build_deduplicated_ast(std::vector<std::vector<ParsedSymbol>>& symbols, const mdebug::SymbolTable& symbol_table);
static void print_functions(FILE* out, mdebug::SymbolTable& symbol_table);
static void print_globals(FILE* out, mdebug::SymbolTable& symbol_table);
static void print_types_deduplicated(FILE* out, mdebug::SymbolTable& symbol_table, const Options& options);
static void print_types_per_file(FILE* out, mdebug::SymbolTable& symbol_table, const Options& options);
static void print_local_symbols(FILE* out, const mdebug::SymbolTable& symbol_table);
static void print_external_symbols(FILE* out, const mdebug::SymbolTable& symbol_table);
static void print_symbol(FILE* out, const mdebug::Symbol& symbol, bool indent);
static u32 build_analysis_flags(u32 flags);
static void list_files(FILE* out, const mdebug::SymbolTable& symbol_table);
static void list_sections(FILE* out, const mdebug::SymbolTable& symbol_table, const Module& modules);
static void test(FILE* out, const fs::path& directory);
static Options parse_args(int argc, char** argv);
static void print_help();

int main(int argc, char** argv) {
	Options options = parse_args(argc, argv);
	FILE* out = stdout;
	if(!options.output_file.empty()) {
		out = open_file_w(options.output_file.c_str());
		verify(out, "Failed to open output file '%s'.", options.output_file.string().c_str());
	}
	switch(options.mode) {
		case OutputMode::FUNCTIONS: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			print_functions(out, symbol_table);
			return 0;
		}
		case OutputMode::GLOBALS: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			print_globals(out, symbol_table);
			return 0;
		}
		case OutputMode::TYPES: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			if(!(options.flags & FLAG_PER_FILE)) {
				print_types_deduplicated(out, symbol_table, options);
			} else {
				print_types_per_file(out, symbol_table, options);
			}
			return 0;
		}
		case OutputMode::JSON: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			u32 analysis_flags = STRIP_GENERATED_FUNCTIONS;
			if(!(options.flags & FLAG_PER_FILE)) {
				analysis_flags |= DEDUPLICATE_TYPES;
			}
			HighSymbolTable high = analyse(symbol_table, analysis_flags);
			print_json(out, high, options.flags & FLAG_PER_FILE);
			break;
		}
		case OutputMode::MDEBUG: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			mdebug::print_headers(out, symbol_table);
			break;
		}
		case OutputMode::SYMBOLS: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			print_local_symbols(out, symbol_table);
			return 0;
		}
		case OutputMode::EXTERNALS: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			print_external_symbols(out, symbol_table);
			return 0;
		}
		case OutputMode::FILES: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			list_files(out, symbol_table);
			return 0;
		}
		case OutputMode::SECTIONS: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			list_sections(out, symbol_table, mod);
			return 0;
		}
		case OutputMode::TYPE_GRAPH: {
			Module mod;
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			HighSymbolTable high = analyse(symbol_table, DEDUPLICATE_TYPES | STRIP_GENERATED_FUNCTIONS);
			TypeDependencyAdjacencyList graph = build_type_dependency_graph(high);
			print_type_dependency_graph(out, high, graph);
			return 0;
		}
		case OutputMode::TEST: {
			test(out, options.input_file);
			return 0;
		}
		case OutputMode::HELP:
		case OutputMode::BAD_COMMAND: {
			print_help();
			return 1;
		}
	}
}

static void print_functions(FILE* out, mdebug::SymbolTable& symbol_table) {
	CppPrinter printer(out);
	for(s32 i = 0; i < (s32) symbol_table.files.size(); i++) {
		HighSymbolTable result = analyse(symbol_table, NO_ANALYSIS_FLAGS, i);
		ast::SourceFile& source_file = *result.source_files.at(0);
		printer.comment_block_file(source_file.full_path.c_str());
		for(const std::unique_ptr<ast::Node>& node : source_file.functions) {
			printer.function(node->as<ast::FunctionDefinition>());
		}
	}
}

static void print_globals(FILE* out, mdebug::SymbolTable& symbol_table) {
	CppPrinter printer(out);
	for(s32 i = 0; i < (s32) symbol_table.files.size(); i++) {
		HighSymbolTable result = analyse(symbol_table, NO_ANALYSIS_FLAGS, i);
		ast::SourceFile& source_file = *result.source_files.at(0);
		printer.comment_block_file(source_file.full_path.c_str());
		for(const std::unique_ptr<ast::Node>& node : source_file.globals) {
			printer.global_variable(node->as<ast::Variable>());
		}
	}
}

static void print_types_deduplicated(FILE* out, mdebug::SymbolTable& symbol_table, const Options& options) {
	u32 analysis_flags = build_analysis_flags(options.flags);
	analysis_flags |= DEDUPLICATE_TYPES;
	HighSymbolTable high = analyse(symbol_table, analysis_flags);
	CppPrinter printer(out);
	printer.comment_block_beginning(options.input_file);
	printer.comment_block_compiler_version_info(symbol_table);
	printer.comment_block_builtin_types(high.deduplicated_types);
	printer.verbose = options.flags & FLAG_VERBOSE;
	for(const std::unique_ptr<ast::Node>& type : high.deduplicated_types) {
		printer.data_type(*type);
	}
}

static void print_types_per_file(FILE* out, mdebug::SymbolTable& symbol_table, const Options& options) {
	u32 analysis_flags = build_analysis_flags(options.flags);
	CppPrinter printer(out);
	printer.verbose = options.flags & FLAG_VERBOSE;
	printer.comment_block_beginning(options.input_file);
	for(s32 i = 0; i < (s32) symbol_table.files.size(); i++) {
		HighSymbolTable result = analyse(symbol_table, analysis_flags, i);
		ast::SourceFile& source_file = *result.source_files.at(0);
		printer.comment_block_file(source_file.full_path.c_str());
		printer.comment_block_compiler_version_info(symbol_table);
		printer.comment_block_builtin_types(source_file.data_types);
		printer.verbose = options.flags & FLAG_VERBOSE;
		for(const std::unique_ptr<ast::Node>& type : source_file.data_types) {
			printer.data_type(*type);
		}
	}
}

static void print_local_symbols(FILE* out, const mdebug::SymbolTable& symbol_table) {
	for(const mdebug::SymFileDescriptor& fd : symbol_table.files) {
		fprintf(out, "FILE %s:\n", fd.raw_path.c_str());
		for(const mdebug::Symbol& symbol : fd.symbols) {
			print_symbol(out, symbol, true);
		}
	}
}

static void print_external_symbols(FILE* out, const mdebug::SymbolTable& symbol_table) {
	for(const mdebug::Symbol& symbol : symbol_table.externals) {
		print_symbol(out, symbol, false);
	}
}

static void print_symbol(FILE* out, const mdebug::Symbol& symbol, bool indent) {
	if(indent) {
		fprintf(out, "    ");
	}
	fprintf(out, "%8x ", symbol.value);
	const char* symbol_type_str = symbol_type(symbol.storage_type);
	if(symbol_type_str) {
		fprintf(out, "%-11s ", symbol_type_str);
	} else {
		fprintf(out, "ST(%7d) ", (u32) symbol.storage_type);
	}
	const char* symbol_class_str = symbol_class(symbol.storage_class);
	if(symbol_class_str) {
		fprintf(out, "%-4s ", symbol_class_str);
	} else if ((u32) symbol.storage_class == 0) {
		fprintf(out, "         ");
	} else {
		fprintf(out, "SC(%4d) ", (u32) symbol.storage_class);
	}
	if(symbol.is_stabs) {
		fprintf(out, "%-8s ", mdebug::stabs_code(symbol.code));
	} else {
		fprintf(out, "SI(%4d) ", symbol.index);
	}
	fprintf(out, "%s\n", symbol.string);
}

static u32 build_analysis_flags(u32 flags) {
	u32 analysis_flags = NO_ANALYSIS_FLAGS;
	if(flags & FLAG_OMIT_ACCESS_SPECIFIERS) analysis_flags |= STRIP_ACCESS_SPECIFIERS;
	if(flags & FLAG_OMIT_MEMBER_FUNCTIONS) analysis_flags |= STRIP_MEMBER_FUNCTIONS;
	if(!(flags & FLAG_INCLUDE_GENERATED_FUNCTIONS)) analysis_flags |= STRIP_GENERATED_FUNCTIONS;
	return analysis_flags;
}

static void list_files(FILE* out, const mdebug::SymbolTable& symbol_table) {
	for(const mdebug::SymFileDescriptor& fd : symbol_table.files) {
		fprintf(out, "%s\n", fd.full_path.c_str());
	}
}

static void list_sections(FILE* out, const mdebug::SymbolTable& symbol_table, const Module& module) {
	for(const ModuleSection& section : module.sections) {
		if(section.virtual_address == 0) {
			continue;
		}
		
		u32 section_start = section.virtual_address;
		u32 section_end = section.virtual_address + section.size;
		
		fprintf(out, "%s:\n", section.name.c_str());
		for(const mdebug::SymFileDescriptor& fd : symbol_table.files) {
			// Find the text address without running the whole analysis process.
			u32 text_address = UINT32_MAX;
			for(const mdebug::Symbol& symbol : fd.symbols) {
				if(symbol.is_stabs && symbol.code == mdebug::N_SO) {
					text_address = symbol.value;
					break;
				}
			}
			if(text_address == UINT32_MAX) {
				for(const mdebug::Symbol& symbol : fd.symbols) {
					if(symbol.storage_type == mdebug::SymbolType::PROC && symbol.storage_class == mdebug::SymbolClass::TEXT && symbol.value != -1) {
						text_address = std::min(text_address, (u32) symbol.value);
					}
				}
			}
			if(text_address != UINT32_MAX && text_address >= section_start && text_address < section_end) {
				fprintf(out, "\t%s\n", fd.full_path.c_str());
			}
		}
	}
}

static void test(FILE* out, const fs::path& directory) {
	verify(fs::is_directory(directory), "Input path is not a directory.");
	s32 passed = 0;
	s32 skipped = 0;
	for(auto entry : fs::directory_iterator(directory)) {
		fs::path filename = entry.path().filename();
		if(filename.extension() != ".c" && filename.extension() != ".cpp" && filename.extension() != ".md") {
			printf("%s ", entry.path().filename().string().c_str());
			Module mod = loaders::read_elf_file(entry.path());
			ModuleSection* mdebug_section = mod.lookup_section(".mdebug");
			if(mdebug_section) {
				mdebug::SymbolTable symbol_table = mdebug::parse_symbol_table(mod, *mdebug_section);
				ccc::HighSymbolTable high = analyse(symbol_table, DEDUPLICATE_TYPES);
				CppPrinter printer(out);
				for(const std::unique_ptr<ast::Node>& type : high.deduplicated_types) {
					printer.data_type(*type);
				}
				for(const std::unique_ptr<ast::SourceFile>& file : high.source_files) {
					for(const std::unique_ptr<ast::Node>& node : file->functions) {
						printer.function(node->as<ast::FunctionDefinition>());
					}
					for(const std::unique_ptr<ast::Node>& node : file->globals) {
						printer.global_variable(node->as<ast::Variable>());
					}
				}
				print_json(out, high, false);
				printf("pass\n");
				passed++;
			} else {
				printf("no .mdebug section\n");
				skipped++;
			}
		}
	}
	// If it gets to this point it means all of the tests succeded.
	printf("%d test cases passed, %d skipped, 0 failed\n", passed, skipped);
}

static Options parse_args(int argc, char** argv) {
	Options options;
	if(argc < 2) {
		return options;
	}
	const char* command = argv[1];
	bool require_input_path = false;
	static struct {
		OutputMode mode;
		const char* argument;
		const char* legacy_argument = nullptr;
	} commands[] = {
		{OutputMode::FUNCTIONS, "functions", "print_functions"},
		{OutputMode::GLOBALS, "globals", "print_globals"},
		{OutputMode::TYPES, "types", "print_types"},
		{OutputMode::JSON, "json", "print_json"},
		{OutputMode::MDEBUG, "mdebug", "print_mdebug"},
		{OutputMode::SYMBOLS, "symbols", "print_symbols"},
		{OutputMode::EXTERNALS, "externals", "print_external_symbols"},
		{OutputMode::FILES, "files", "list_files"},
		{OutputMode::SECTIONS, "sections"},
		{OutputMode::TYPE_GRAPH, "type_graph"},
		{OutputMode::TEST, "test"}
	};
	for(auto& cmd : commands) {
		if(strcmp(command, cmd.argument) == 0 || (cmd.legacy_argument && (strcmp(command, cmd.legacy_argument) == 0))) {
			options.mode = cmd.mode;
			require_input_path = true;
			break;
		}
	}
	if(options.mode == OutputMode::BAD_COMMAND) {
		if(strcmp(command, "test") == 0) {
			options.mode = OutputMode::TEST;
			require_input_path = true;
		} else if(strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
			options.mode = OutputMode::HELP;
			require_input_path = false;
		} else {
			verify_not_reached("Unknown command '%s'.", command);
			options.mode = OutputMode::BAD_COMMAND;
			return options;
		}
	}
	bool input_path_provided = false;
	for(s32 i = 2; i < argc; i++) {
		const char* arg = argv[i];
		if(strcmp(arg, "--per-file") == 0) {
			options.flags |= FLAG_PER_FILE;
		} else if(strcmp(arg, "--verbose") == 0) {
			options.flags |= FLAG_VERBOSE;
		} else if(strcmp(arg, "--omit-access-specifiers") == 0) {
			options.flags |= FLAG_OMIT_ACCESS_SPECIFIERS;
		} else if(strcmp(arg, "--omit-member-functions") == 0) {
			options.flags |= FLAG_OMIT_MEMBER_FUNCTIONS;
		} else if(strcmp(arg, "--include-generated-functions") == 0) {
			options.flags |= FLAG_INCLUDE_GENERATED_FUNCTIONS;
		} else if(strcmp(arg, "--output") == 0) {
			if(i + 1 < argc) {
				options.output_file = argv[++i];
			} else {
				verify_not_reached("No output path specified.");
				options.mode = OutputMode::BAD_COMMAND;
				return options;
			}
		} else if(strncmp(arg, "--", 2) == 0) {
			verify_not_reached("Unknown option '%s'.", arg);
			options.mode = OutputMode::BAD_COMMAND;
			return options;
		} else if(input_path_provided) {
			verify_not_reached("Multiple input paths specified.");
			options.mode = OutputMode::BAD_COMMAND;
			return options;
		} else {
			options.input_file = argv[i];
			input_path_provided = true;
		}
	}
	verify(!require_input_path || !options.input_file.empty(), "No input path specified.");
	return options;
}

const char* git_tag();

static void print_help() {
	const char* tag = git_tag();
	printf("stdump %s -- https://github.com/chaoticgd/ccc\n",
		(strlen(tag) > 0) ? tag : "development version");
	puts("  Mdebug/STABS symbol table parser and dumper.");
	puts("");
	puts("Commands:");
	puts("  functions <input file>");
	puts("    Print all the functions recovered from the STABS symbols as C++.");
	puts("");
	puts("  globals <input file>");
	puts("    Print all the global variables recovered from the STABS symbols as C++.");
	puts("");
	puts("  types [options] <input file>");
	puts("    Print all the types recovered from the STABS symbols as C++.");
	puts("");
	puts("    --per-file                    Do not deduplicate types from files.");
	puts("    --verbose                     Print additional information such as the raw");
	puts("                                  STABS symbol along with each type.");
	puts("    --omit-access-specifiers      Do not print access specifiers.");
	puts("    --omit-member-functions       Do not print member functions.");
	puts("    --include-generated-functions Include member functions that are likely");
	puts("                                  auto-generated.");
	puts("    --output <output file>        Write the output to the file specified instead");
	puts("                                  of to the standard output.");
	puts("");
	puts("  json [options] <input file>");
	puts("    Print all of the above as JSON.");
	puts("");
	puts("    --per-file                    Do not deduplicate types from files.");
	puts("");
	puts("  mdebug <input file>");
	puts("    Print mdebug header information.");
	puts("");
	puts("  symbols <input file>");
	puts("    List all of the local symbols for each file.");
	puts("");
	puts("  externals <input file>");
	puts("    List all of the external symbols for each file.");
	puts("");
	puts("  files <input file>");
	puts("    List the names of each of the source files.");
	puts("");
	puts("  sections <input file>");
	puts("    List the names of the source files associated with each ELF section.");
	puts("");
	puts("  type_graph <input_file>");
	puts("    Print out a dependency graph of all the types in graphviz DOT format.");
	puts("");
	puts("  test <input directory>");
	puts("    Parse and print all the ELF files in a directory.");
	puts("    Use '--output /dev/null' to reduce spam.");
	puts("");
	puts("  help | --help | -h");
	puts("    Print this help message.");
	puts("");
	puts("Common Options:");
	puts("");
	puts("  --output <output file>        Write the output to the file specified instead");
	puts("                                of to the standard output.");
}
