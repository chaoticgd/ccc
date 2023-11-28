// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "ccc/ccc.h"
#include "platform/file.h"

using namespace ccc;

enum class OutputMode {
	// Common Commands
	IDENTIFY,
	FUNCTIONS,
	GLOBALS,
	TYPES,
	JSON,
	HELP,
	// ELF Symbol Table Commands
	SYMTAB,
	// MIPS Symbol Table commands
	MDEBUG,
	SYMBOLS,
	EXTERNALS,
	FILES,
	SECTIONS,
	TYPE_GRAPH,
	BAD_COMMAND
};

enum Flags {
	NO_FLAGS = 0,
	FLAG_PER_FILE = (1 << 0),
	FLAG_OMIT_ACCESS_SPECIFIERS = (1 << 1),
	FLAG_OMIT_MEMBER_FUNCTIONS = (1 << 2),
	FLAG_INCLUDE_GENERATED_FUNCTIONS = (1 << 3)
};

struct Options {
	OutputMode mode = OutputMode::BAD_COMMAND;
	fs::path input_file;
	fs::path output_file;
	u32 flags = NO_FLAGS;
	std::optional<std::string> section;
	std::optional<SymbolTableFormat> format;
};

static SymbolDatabase read_symbol_table(std::vector<u8>& image, ElfFile& elf, const Options& options);
static void identify_symbol_tables(FILE* out, const fs::path& input_path);
static void identify_symbol_tables_in_file(FILE* out, u32* totals, u32* unknown_total, const fs::path& file_path);
static void print_functions(FILE* out, SymbolDatabase& database);
static void print_globals(FILE* out, SymbolDatabase& database);
static void print_types_deduplicated(FILE* out, SymbolDatabase& database, const Options& options);
static void print_types_per_file(FILE* out, SymbolDatabase& database, const Options& options);

static void print_symtab(FILE* out, const Options& options);

static void print_local_symbols(FILE* out, const SymbolDatabase& database);
static void print_external_symbols(FILE* out, const SymbolDatabase& database);
static void print_symbol(FILE* out, const mdebug::Symbol& symbol, bool indent);
static u32 command_line_flags_to_parser_flags(u32 flags);
static void list_files(FILE* out, const SymbolDatabase& database);
static void list_sections(FILE* out, const SymbolDatabase& database, const ElfFile& elf);
static Options parse_args(int argc, char** argv);
static void print_help();

int main(int argc, char** argv) {
	Options options = parse_args(argc, argv);
	
	FILE* out = stdout;
	if(!options.output_file.empty()) {
		out = fopen(options.output_file.string().c_str(), "w");
		CCC_CHECK_FATAL(out, "Failed to open output file '%s'.", options.output_file.string().c_str());
	}
	
	switch(options.mode) {
		case OutputMode::IDENTIFY: {
			identify_symbol_tables(out, options.input_file);
			return 0;
		}
		case OutputMode::FUNCTIONS: {
			std::vector<u8> image;
			ElfFile elf;
			SymbolDatabase database = read_symbol_table(image, elf, options);
			
			print_functions(out, database);
			return 0;
		}
		case OutputMode::GLOBALS: {
			std::vector<u8> image;
			ElfFile elf;
			SymbolDatabase database = read_symbol_table(image, elf, options);
			
			print_globals(out, database);
			return 0;
		}
		case OutputMode::TYPES: {
			std::vector<u8> image;
			ElfFile elf;
			SymbolDatabase database = read_symbol_table(image, elf, options);
			
			if(!(options.flags & FLAG_PER_FILE)) {
				print_types_deduplicated(out, database, options);
			} else {
				print_types_per_file(out, database, options);
			}
			return 0;
		}
		case OutputMode::JSON: {
			u32 json_parser_flags = STRIP_GENERATED_FUNCTIONS;
			if(options.flags & FLAG_PER_FILE) {
				json_parser_flags |= DONT_DEDUPLICATE_TYPES;
			}
			
			std::vector<u8> image;
			ElfFile elf;
			SymbolDatabase database = read_symbol_table(image, elf, options);
			print_json(out, database, options.flags & FLAG_PER_FILE);
			
			break;
		}
		case OutputMode::SYMTAB: {
			print_symtab(out, options);
			break;
		}
		case OutputMode::MDEBUG: {
			//ElfFile elf;
			//Result<SymbolDatabase> database = read_symbol_table(elf, options.input_file);
			//CCC_EXIT_IF_ERROR(database);
			//
			//database->print_header(out);
			break;
		}
		case OutputMode::SYMBOLS: {
			std::vector<u8> image;
			ElfFile elf;
			SymbolDatabase database = read_symbol_table(image, elf, options);
			print_local_symbols(out, database);
			
			return 0;
		}
		case OutputMode::EXTERNALS: {
			std::vector<u8> image;
			ElfFile elf;
			SymbolDatabase database = read_symbol_table(image, elf, options);
			print_external_symbols(out, database);
			return 0;
		}
		case OutputMode::FILES: {
			std::vector<u8> image;
			ElfFile elf;
			SymbolDatabase database = read_symbol_table(image, elf, options);
			list_files(out, database);
			return 0;
		}
		case OutputMode::SECTIONS: {
			std::vector<u8> image;
			ElfFile elf;
			SymbolDatabase database = read_symbol_table(image, elf, options);
			list_sections(out, database, elf);
			return 0;
		}
		case OutputMode::TYPE_GRAPH: {
			std::vector<u8> image;
			ElfFile elf;
			SymbolDatabase database = read_symbol_table(image, elf, options);
			TypeDependencyAdjacencyList graph = build_type_dependency_graph(database);
			print_type_dependency_graph(out, database, graph);
			return 0;
		}
		case OutputMode::HELP:
		case OutputMode::BAD_COMMAND: {
			print_help();
			return 1;
		}
	}
}

static SymbolDatabase read_symbol_table(std::vector<u8>& image, ElfFile& elf, const Options& options) {
	Result<std::vector<u8>> image_result = platform::read_binary_file(options.input_file);
	CCC_EXIT_IF_ERROR(image_result);
	image = std::move(*image_result);
	
	Result<ElfFile> elf_result = parse_elf_file(image);
	CCC_EXIT_IF_ERROR(elf_result);
	elf = std::move(*elf_result);
	
	SymbolTableParserConfig config;
	config.section = options.section;
	config.format = options.format;
	config.parser_flags = command_line_flags_to_parser_flags(options.flags);
	
	SymbolDatabase database;
	Result<SymbolSourceHandle> symbol_source = parse_symbol_table(database, elf, config);
	CCC_EXIT_IF_ERROR(symbol_source);
	
	return database;
}

static void identify_symbol_tables(FILE* out, const fs::path& input_path) {
	if(fs::is_regular_file(input_path)) {
		identify_symbol_tables_in_file(out, nullptr, nullptr, input_path);
	} else if(fs::is_directory(input_path)) {
		std::vector<u32> totals(SYMBOL_TABLE_FORMAT_COUNT, 0);
		u32 unknown_total = 0;
		
		for(auto entry : fs::recursive_directory_iterator(input_path)) {
			if(entry.is_regular_file()) {
				identify_symbol_tables_in_file(out, totals.data(), &unknown_total, entry.path());
			}
		}
		
		fprintf(out, "\n");
		fprintf(out, "Totals:\n");
		for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
			fprintf(out, "  %4d %s sections\n", totals[i], SYMBOL_TABLE_FORMATS[i].section_name);
		}
		fprintf(out, "  %4d unknown\n", unknown_total);
	} else {
		CCC_FATAL("Input path '%s' is neither a regular file nor a directory.", input_path.string().c_str());
	}
}

static void identify_symbol_tables_in_file(FILE* out, u32* totals, u32* unknown_total, const fs::path& file_path) {
	fprintf(out, "%100s:", file_path.string().c_str());
	
	Result<std::vector<u8>> file = platform::read_binary_file(file_path);
	CCC_EXIT_IF_ERROR(file);
	
	const u32* fourcc = get_packed<u32>(*file, 0);
	if(!fourcc) {
		fprintf(out, " file too small\n");
		return;
	}
	
	switch(*fourcc) {
		case CCC_FOURCC("\x7f""ELF"): {
			Result<ElfFile> elf = parse_elf_file(std::move(*file));
			CCC_EXIT_IF_ERROR(elf);
			
			bool print_none = true;
			for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
				if(elf->lookup_section(SYMBOL_TABLE_FORMATS[i].section_name)) {
					fprintf(out, " %s", SYMBOL_TABLE_FORMATS[i].section_name);
					if(totals) {
						totals[i]++;
					}
					print_none = false;
				}
			}
			
			if(print_none) {
				fprintf(out, " none");
			}
			
			fprintf(out, "\n");
			
			break;
		}
		case CCC_FOURCC("SNR1"):
		case CCC_FOURCC("SNR2"): {
			if(totals) {
				totals[SNDLL]++;
			}
			fprintf(out, " sndll\n");
			break;
		}
		default: {
			if(unknown_total) {
				(*unknown_total)++;
			}
			fprintf(out, " unknown format\n");
			break;
		}
	}
}

static void print_functions(FILE* out, SymbolDatabase& database) {
	CppPrinterConfig config;
	CppPrinter printer(out, config);
	
	bool first_iteration = true;
	SourceFileHandle source_file_handle;
	for(const Function& function : database.functions) {
		if(function.source_file() != source_file_handle || first_iteration) {
			SourceFile* source_file = database.source_files.symbol_from_handle(function.source_file());
			if(source_file) {
				printer.comment_block_file(source_file->full_path().c_str());
				source_file_handle = source_file->handle();
			} else {
				printer.comment_block_file("(unknown)");
				source_file_handle = SourceFileHandle();
			}
			first_iteration = false;
		}
		
		printer.function(function, database, nullptr);
	}
}

static void print_globals(FILE* out, SymbolDatabase& database) {
	CppPrinterConfig config;
	CppPrinter printer(out, config);
	
	bool first_iteration = true;
	SourceFileHandle source_file_handle;
	for(const GlobalVariable& global_variable : database.global_variables) {
		if(global_variable.source_file() != source_file_handle || first_iteration) {
			SourceFile* source_file = database.source_files.symbol_from_handle(global_variable.source_file());
			if(source_file) {
				printer.comment_block_file(source_file->full_path().c_str());
				source_file_handle = source_file->handle();
			} else {
				printer.comment_block_file("(unknown)");
				source_file_handle = SourceFileHandle();
			}
			first_iteration = false;
		}
		
		printer.global_variable(global_variable, nullptr);
	}
}

static void print_types_deduplicated(FILE* out, SymbolDatabase& database, const Options& options) {
	CppPrinterConfig config;
	CppPrinter printer(out, config);
	printer.comment_block_beginning(options.input_file.filename().string().c_str());
	printer.comment_block_toolchain_version_info(database);
	printer.comment_block_builtin_types(database.data_types);
	for(const DataType& data_type : database.data_types) {
		printer.data_type(data_type);
	}
}

static void print_types_per_file(FILE* out, SymbolDatabase& database, const Options& options) {
	//u32 analysis_flags = build_analysis_flags(options.flags);
	//CppPrinter printer(out);
	//printer.comment_block_beginning(options.input_file.filename().string().c_str());
	//
	//s32 file_count = database.file_count();
	//for(s32 i = 0; i < file_count; i++) {
	//	Result<SymbolDatabase> database = analyse(database, analysis_flags, i);
	//	CCC_EXIT_IF_ERROR(database);
	//	ast::SourceFile& source_file = *database->source_files.at(0);
	//	printer.comment_block_file(source_file.full_path.c_str());
	//	printer.comment_block_toolchain_version_info(*database);
	//	printer.comment_block_builtin_types(source_file.data_types);
	//	for(const std::unique_ptr<ast::Node>& type : source_file.data_types) {
	//		printer.data_type(*type);
	//	}
	//}
}

static void print_symtab(FILE* out, const Options& options) {
	Result<std::vector<u8>> image = platform::read_binary_file(options.input_file);
	CCC_EXIT_IF_ERROR(image);
	
	Result<ElfFile> elf = parse_elf_file(*image);
	CCC_EXIT_IF_ERROR(elf);
	
	const ElfSection* symtab_section = elf->lookup_section(".symtab");
	CCC_CHECK_FATAL(symtab_section, "No .symtab section.");
	
	Result<void> print_result = elf::print_symbol_table(out, *symtab_section, *elf);
	CCC_EXIT_IF_ERROR(print_result)
}

static void print_local_symbols(FILE* out, const SymbolDatabase& database) {
	//s32 file_count = database.file_count();
	//for(s32 i = 0; i < file_count; i++) {
	//	Result<mdebug::File> file = database.parse_file(i);
	//	CCC_EXIT_IF_ERROR(file);
	//	
	//	fprintf(out, "FILE %s:\n", file->raw_path.c_str());
	//	for(const mdebug::Symbol& symbol : file->symbols) {
	//		print_symbol(out, symbol, true);
	//	}
	//}
}//

static void print_external_symbols(FILE* out, const SymbolDatabase& database) {
	//Result<std::vector<mdebug::Symbol>> external_symbols = database.parse_external_symbols();
	//CCC_EXIT_IF_ERROR(external_symbols);
	//
	//for(const mdebug::Symbol& symbol : *external_symbols) {
	//	print_symbol(out, symbol, false);
	//}
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

static u32 command_line_flags_to_parser_flags(u32 flags) {
	u32 parser_flags = NO_PARSER_FLAGS;
	if(flags & FLAG_PER_FILE) parser_flags |= DONT_DEDUPLICATE_TYPES;
	if(flags & FLAG_OMIT_ACCESS_SPECIFIERS) parser_flags |= STRIP_ACCESS_SPECIFIERS;
	if(flags & FLAG_OMIT_MEMBER_FUNCTIONS) parser_flags |= STRIP_MEMBER_FUNCTIONS;
	if(!(flags & FLAG_INCLUDE_GENERATED_FUNCTIONS)) parser_flags |= STRIP_GENERATED_FUNCTIONS;
	return parser_flags;
}

static void list_files(FILE* out, const SymbolDatabase& database) {
	//s32 file_count = database.file_count();
	//for(s32 i = 0; i < file_count; i++) {
	//	Result<mdebug::File> file = database.parse_file(i);
	//	CCC_EXIT_IF_ERROR(file);
	//	
	//	fprintf(out, "%s\n", file->full_path.c_str());
	//}
}

static void list_sections(FILE* out, const SymbolDatabase& database, const ElfFile& elf) {
	for(const ElfSection& section : elf.sections) {
		if(section.address == 0) {
			continue;
		}
		
		u32 section_start = section.address;
		u32 section_end = section.address + section.size;
		
		fprintf(out, "%s:\n", section.name.c_str());
		
		//s32 file_count = database.file_count();
		//for(s32 i = 0; i < file_count; i++) {
		//	Result<mdebug::File> file = database.parse_file(i);
		//	CCC_EXIT_IF_ERROR(file);
		//	
		//	// Find the text address without running the whole analysis process.
		//	u32 text_address = UINT32_MAX;
		//	for(const mdebug::Symbol& symbol : file->symbols) {
		//		if(symbol.is_stabs && symbol.code == mdebug::N_SO) {
		//			text_address = symbol.value;
		//			break;
		//		}
		//	}
		//	if(text_address == UINT32_MAX) {
		//		for(const mdebug::Symbol& symbol : file->symbols) {
		//			if(symbol.storage_type == mdebug::SymbolType::PROC && symbol.storage_class == mdebug::SymbolClass::TEXT && symbol.value != -1) {
		//				text_address = std::min(text_address, (u32) symbol.value);
		//			}
		//		}
		//	}
		//	if(text_address != UINT32_MAX && text_address >= section_start && text_address < section_end) {
		//		fprintf(out, "\t%s\n", file->full_path.c_str());
		//	}
		//}
	}
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
		// Common Commands
		{OutputMode::IDENTIFY, "identify"},
		{OutputMode::FUNCTIONS, "functions", "print_functions"},
		{OutputMode::GLOBALS, "globals", "print_globals"},
		{OutputMode::TYPES, "types", "print_types"},
		{OutputMode::JSON, "json", "print_json"},
		// ELF Symbol Table Commands
		{OutputMode::SYMTAB, "symtab"},
		// MIPS Symbol Table Commands
		{OutputMode::MDEBUG, "mdebug", "print_mdebug"},
		{OutputMode::SYMBOLS, "symbols", "print_symbols"},
		{OutputMode::EXTERNALS, "externals", "print_external_symbols"},
		{OutputMode::FILES, "files", "list_files"},
		{OutputMode::SECTIONS, "sections"},
		{OutputMode::TYPE_GRAPH, "type_graph"}
	};
	for(auto& cmd : commands) {
		if(strcmp(command, cmd.argument) == 0 || (cmd.legacy_argument && (strcmp(command, cmd.legacy_argument) == 0))) {
			options.mode = cmd.mode;
			require_input_path = true;
			break;
		}
	}
	if(options.mode == OutputMode::BAD_COMMAND) {
		if(strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
			options.mode = OutputMode::HELP;
			require_input_path = false;
		} else {
			CCC_CHECK_FATAL("Unknown command '%s'.", command);
			options.mode = OutputMode::BAD_COMMAND;
			return options;
		}
	}
	bool input_path_provided = false;
	for(s32 i = 2; i < argc; i++) {
		const char* arg = argv[i];
		if(strcmp(arg, "--per-file") == 0) {
			options.flags |= FLAG_PER_FILE;
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
				CCC_FATAL("No output path specified.");
				options.mode = OutputMode::BAD_COMMAND;
				return options;
			}
		} else if(strcmp(arg, "--section") == 0) {
			if(i + 1 < argc) {
				options.section = argv[++i];
			} else {
				CCC_FATAL("No section name specified.");
				options.mode = OutputMode::BAD_COMMAND;
				return options;
			}
		} else if(strcmp(arg, "--format") == 0) {
			if(i + 1 < argc) {
				std::string format = argv[++i];
				const SymbolTableFormatInfo* info = symbol_table_format_from_name(format.c_str());
				CCC_CHECK_FATAL(info, "Invalid symbol table format specified.");
				options.format = info->format;
			} else {
				CCC_FATAL("No section name specified.");
				options.mode = OutputMode::BAD_COMMAND;
				return options;
			}
		} else if(strncmp(arg, "--", 2) == 0) {
			CCC_FATAL("Unknown option '%s'.", arg);
		} else if(input_path_provided) {
			CCC_FATAL("Multiple input paths specified.");
		} else {
			options.input_file = argv[i];
			input_path_provided = true;
		}
	}
	CCC_CHECK_FATAL(!require_input_path || !options.input_file.empty(), "No input path specified.");
	return options;
}

const char* git_tag();

static void print_help() {
	const char* tag = git_tag();
	printf("stdump %s -- https://github.com/chaoticgd/ccc\n",
		(strlen(tag) > 0) ? tag : "development version");
	puts("  MDebug/STABS symbol table parser and dumper.");
	puts("");
	puts("Common Commands:");
	puts("");
	puts("  identify <input path>");
	puts("    Identify the symbol tables present in the input file(s). If the input path");
	puts("    is a directory, it will be walked recursively.");
	puts("");
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
	puts("    --omit-access-specifiers      Do not print access specifiers.");
	puts("    --omit-member-functions       Do not print member functions.");
	puts("    --include-generated-functions Include member functions that are likely");
	puts("                                  auto-generated.");
	puts("");
	puts("  json [options] <input file>");
	puts("    Print all of the above as JSON.");
	puts("");
	puts("    --per-file                    Do not deduplicate types from files.");
	puts("");
	puts("ELF Symbol Table Commands:");
	puts("");
	puts("  symtab <input file>");
	puts("    Print all of the symbols from the .symtab section.");
	puts("");
	puts("MIPS Symbol Table Commands:");
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
	puts("  help | --help | -h");
	puts("    Print this help message.");
	puts("");
	puts("Common Options:");
	puts("");
	puts("  --output <output file>        Write the output to the file specified instead");
	puts("                                of to the standard output.");
	puts("");
	puts("  --section <section name>      Choose which symbol table you want to read from.");
	puts("                                Common section names are: .symtab, .mdebug,");
	puts("                                .stab, .debug, .sndll.");
	puts("");
	puts("  --format <format name>        Explicitly specify the symbol table format.");
	puts("                                Possible options are: symtab, mdebug, stab,");
	puts("                                dwarf, sndll.");
}
