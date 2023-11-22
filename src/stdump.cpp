// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "ccc/ccc.h"
#include "platform/file.h"

using namespace ccc;

enum class OutputMode {
	IDENTIFY,
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
	FLAG_OMIT_ACCESS_SPECIFIERS = (1 << 1),
	FLAG_OMIT_MEMBER_FUNCTIONS = (1 << 2),
	FLAG_INCLUDE_GENERATED_FUNCTIONS = (1 << 3)
};

struct Options {
	OutputMode mode = OutputMode::BAD_COMMAND;
	fs::path input_file;
	fs::path output_file;
	u32 flags = NO_FLAGS;
};

struct SymbolTableCounts {
	s32 symtab_count = 0;
	s32 map_count = 0;
	s32 mdebug_count = 0;
	s32 stab_count = 0;
	s32 dwarf_count = 0;
	s32 sndata_count = 0;
	s32 sndll_count = 0;
	s32 unknown_count = 0;
};

static SymbolDatabase read_symbol_table(ElfFile& elf, const fs::path& input_file, u32 parser_flags);
static void identify_symbol_databases(FILE* out, const fs::path& input_path);
static void identify_symbol_databases_in_file(FILE* out, SymbolTableCounts& totals, const fs::path& file_path);
static void print_functions(FILE* out, SymbolDatabase& symbol_database);
static void print_globals(FILE* out, SymbolDatabase& symbol_database);
static void print_types_deduplicated(FILE* out, SymbolDatabase& symbol_database, const Options& options);
static void print_types_per_file(FILE* out, SymbolDatabase& symbol_database, const Options& options);
static void print_local_symbols(FILE* out, const SymbolDatabase& symbol_database);
static void print_external_symbols(FILE* out, const SymbolDatabase& symbol_database);
static void print_symbol(FILE* out, const mdebug::Symbol& symbol, bool indent);
static u32 command_line_flags_to_parser_flags(u32 flags);
static void list_files(FILE* out, const SymbolDatabase& symbol_database);
static void list_sections(FILE* out, const SymbolDatabase& symbol_database, const ElfFile& elf);
static void test(FILE* out, const fs::path& directory);
static Options parse_args(int argc, char** argv);
static void print_help();

int main(int argc, char** argv) {
	Options options = parse_args(argc, argv);
	u32 parser_flags = command_line_flags_to_parser_flags(options.flags);
	FILE* out = stdout;
	if(!options.output_file.empty()) {
		out = fopen(options.output_file.string().c_str(), "w");
		CCC_CHECK_FATAL(out, "Failed to open output file '%s'.", options.output_file.string().c_str());
	}
	switch(options.mode) {
		case OutputMode::IDENTIFY: {
			identify_symbol_databases(out, options.input_file);
			return 0;
		}
		case OutputMode::FUNCTIONS: {
			ElfFile elf;
			SymbolDatabase symbol_database = read_symbol_table(elf, options.input_file, parser_flags);
			
			print_functions(out, symbol_database);
			return 0;
		}
		case OutputMode::GLOBALS: {
			ElfFile elf;
			SymbolDatabase symbol_database = read_symbol_table(elf, options.input_file, parser_flags);
			
			print_globals(out, symbol_database);
			return 0;
		}
		case OutputMode::TYPES: {
			ElfFile elf;
			SymbolDatabase symbol_database = read_symbol_table(elf, options.input_file, parser_flags);
			
			if(!(options.flags & FLAG_PER_FILE)) {
				print_types_deduplicated(out, symbol_database, options);
			} else {
				print_types_per_file(out, symbol_database, options);
			}
			return 0;
		}
		case OutputMode::JSON: {
			u32 json_parser_flags = STRIP_GENERATED_FUNCTIONS;
			if(options.flags & FLAG_PER_FILE) {
				json_parser_flags |= DONT_DEDUPLICATE_TYPES;
			}
			
			ElfFile elf;
			SymbolDatabase symbol_database = read_symbol_table(elf, options.input_file, json_parser_flags);
			print_json(out, symbol_database, options.flags & FLAG_PER_FILE);
			
			break;
		}
		case OutputMode::MDEBUG: {
			//ElfFile elf;
			//Result<SymbolDatabase> symbol_database = read_symbol_table(elf, options.input_file);
			//CCC_EXIT_IF_ERROR(symbol_database);
			//
			//symbol_database->print_header(out);
			break;
		}
		case OutputMode::SYMBOLS: {
			ElfFile elf;
			SymbolDatabase symbol_database = read_symbol_table(elf, options.input_file, parser_flags);
			print_local_symbols(out, symbol_database);
			
			return 0;
		}
		case OutputMode::EXTERNALS: {
			ElfFile elf;
			SymbolDatabase symbol_database = read_symbol_table(elf, options.input_file, parser_flags);
			print_external_symbols(out, symbol_database);
			return 0;
		}
		case OutputMode::FILES: {
			ElfFile elf;
			SymbolDatabase symbol_database = read_symbol_table(elf, options.input_file, parser_flags);
			list_files(out, symbol_database);
			return 0;
		}
		case OutputMode::SECTIONS: {
			ElfFile elf;
			SymbolDatabase symbol_database = read_symbol_table(elf, options.input_file, parser_flags);
			list_sections(out, symbol_database, elf);
			return 0;
		}
		case OutputMode::TYPE_GRAPH: {
			ElfFile elf;
			SymbolDatabase symbol_database = read_symbol_table(elf, options.input_file, parser_flags);
			TypeDependencyAdjacencyList graph = build_type_dependency_graph(symbol_database);
			print_type_dependency_graph(out, symbol_database, graph);
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

static SymbolDatabase read_symbol_table(ElfFile& elf, const fs::path& input_file, u32 parser_flags) {
	Result<std::vector<u8>> image = platform::read_binary_file(input_file);
	CCC_EXIT_IF_ERROR(image);
	
	SymbolDatabase database;
	Result<SymbolSourceHandle> symbol_source = parse_symbol_table(database, std::move(*image), parser_flags);
	CCC_EXIT_IF_ERROR(symbol_source);
	
	return database;
}

static void identify_symbol_databases(FILE* out, const fs::path& input_path) {
	SymbolTableCounts totals;
	
	if(fs::is_regular_file(input_path)) {
		identify_symbol_databases_in_file(out, totals, input_path);
		fprintf(out, "\n");
	} else if(fs::is_directory(input_path)) {
		for(auto entry : fs::recursive_directory_iterator(input_path)) {
			if(entry.is_regular_file()) {
				identify_symbol_databases_in_file(out, totals, entry.path());
				fprintf(out, "\n");
			}
		}
	} else {
		CCC_FATAL("Input file '%s' is neither a regular file nor a directory.", input_path.string().c_str());
	}
	
	fprintf(out, "\n");
	fprintf(out, "Totals:\n");
	fprintf(out, "  %4d .symtab sections\n", totals.symtab_count);
	fprintf(out, "  %4d .map files\n", totals.map_count);
	fprintf(out, "  %4d .mdebug sections\n", totals.mdebug_count);
	fprintf(out, "  %4d .stab sections\n", totals.stab_count);
	fprintf(out, "  %4d .debug sections (dwarf)\n", totals.dwarf_count);
	fprintf(out, "  %4d .sndata sections\n", totals.sndata_count);
	fprintf(out, "  %4d .rel files (sndll)\n", totals.sndll_count);
	fprintf(out, "  %4d unknown\n", totals.unknown_count);
}

static void identify_symbol_databases_in_file(FILE* out, SymbolTableCounts& totals, const fs::path& file_path) {
	fprintf(out, "%s ", file_path.string().c_str());
	
	Result<std::vector<u8>> file = platform::read_binary_file(file_path);
	CCC_EXIT_IF_ERROR(file);
	
	const u32* fourcc = get_packed<u32>(*file, 0);
	if(!fourcc) {
		fprintf(out, "file too small");
		return;
	}
	
	switch(*fourcc) {
		case CCC_FOURCC("\x7f\x45\x4c\x46"): {// ELF
			Result<ElfFile> elf = parse_elf_file(std::move(*file));
			CCC_EXIT_IF_ERROR(elf);
			
			u32 formats = identify_elf_symbol_tables(*elf);
		
			if(formats & SYMTAB) totals.symtab_count++;
			if(formats & MDEBUG) totals.mdebug_count++;
			if(formats & STAB) totals.stab_count++;
			if(formats & DWARF) totals.dwarf_count++;
			if(formats & SNDATA) totals.sndata_count++;
			
			std::string string = symbol_table_formats_to_string(formats);
			fprintf(out, "%s\n", string.c_str());
			break;
		}
		case CCC_FOURCC("SNR2"): { // SN systems DLL file
			totals.sndll_count++;
			fprintf(out, "sndll\n");
			break;
		}
		default: {
			totals.unknown_count++;
			fprintf(out, "unknown format\n");
			break;
		}
	}
}

static void print_functions(FILE* out, SymbolDatabase& symbol_database) {
	//CppPrinter printer(out);
	//s32 file_count = symbol_database.file_count();
	//for(s32 i = 0; i < file_count; i++) {
	//	Result<SymbolDatabase> symbol_database = analyse(symbol_database, NO_ANALYSIS_FLAGS, i);
	//	CCC_EXIT_IF_ERROR(symbol_database);
	//	ast::SourceFile& source_file = *symbol_database->source_files.at(0);
	//	printer.comment_block_file(source_file.full_path.c_str());
	//	for(const std::unique_ptr<ast::Node>& node : source_file.functions) {
	//		printer.function(node->as<ast::FunctionDefinition>());
	//	}
	//}
}

static void print_globals(FILE* out, SymbolDatabase& symbol_database) {
	//CppPrinter printer(out);
	//s32 file_count = symbol_database.file_count();
	//for(s32 i = 0; i < file_count; i++) {
	//	Result<SymbolDatabase> symbol_database = analyse(symbol_database, NO_ANALYSIS_FLAGS, i);
	//	CCC_EXIT_IF_ERROR(symbol_database);
	//	ast::SourceFile& source_file = *symbol_database->source_files.at(0);
	//	printer.comment_block_file(source_file.full_path.c_str());
	//	for(const std::unique_ptr<ast::Node>& node : source_file.globals) {
	//		printer.global_variable(node->as<ast::Variable>());
	//	}
	//}
}

static void print_types_deduplicated(FILE* out, SymbolDatabase& symbol_database, const Options& options) {
	CppPrinterConfig config;
	CppPrinter printer(out, config);
	printer.comment_block_beginning(options.input_file.filename().string().c_str());
	printer.comment_block_toolchain_version_info(symbol_database);
	printer.comment_block_builtin_types(symbol_database.data_types);
	for(const DataType& data_type : symbol_database.data_types) {
		printer.data_type(data_type);
	}
}

static void print_types_per_file(FILE* out, SymbolDatabase& symbol_database, const Options& options) {
	//u32 analysis_flags = build_analysis_flags(options.flags);
	//CppPrinter printer(out);
	//printer.comment_block_beginning(options.input_file.filename().string().c_str());
	//
	//s32 file_count = symbol_database.file_count();
	//for(s32 i = 0; i < file_count; i++) {
	//	Result<SymbolDatabase> symbol_database = analyse(symbol_database, analysis_flags, i);
	//	CCC_EXIT_IF_ERROR(symbol_database);
	//	ast::SourceFile& source_file = *symbol_database->source_files.at(0);
	//	printer.comment_block_file(source_file.full_path.c_str());
	//	printer.comment_block_toolchain_version_info(*symbol_database);
	//	printer.comment_block_builtin_types(source_file.data_types);
	//	for(const std::unique_ptr<ast::Node>& type : source_file.data_types) {
	//		printer.data_type(*type);
	//	}
	//}
}

static void print_local_symbols(FILE* out, const SymbolDatabase& symbol_database) {
	//s32 file_count = symbol_database.file_count();
	//for(s32 i = 0; i < file_count; i++) {
	//	Result<mdebug::File> file = symbol_database.parse_file(i);
	//	CCC_EXIT_IF_ERROR(file);
	//	
	//	fprintf(out, "FILE %s:\n", file->raw_path.c_str());
	//	for(const mdebug::Symbol& symbol : file->symbols) {
	//		print_symbol(out, symbol, true);
	//	}
	//}
}//

static void print_external_symbols(FILE* out, const SymbolDatabase& symbol_database) {
	//Result<std::vector<mdebug::Symbol>> external_symbols = symbol_database.parse_external_symbols();
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

static void list_files(FILE* out, const SymbolDatabase& symbol_database) {
	//s32 file_count = symbol_database.file_count();
	//for(s32 i = 0; i < file_count; i++) {
	//	Result<mdebug::File> file = symbol_database.parse_file(i);
	//	CCC_EXIT_IF_ERROR(file);
	//	
	//	fprintf(out, "%s\n", file->full_path.c_str());
	//}
}

static void list_sections(FILE* out, const SymbolDatabase& symbol_database, const ElfFile& elf) {
	for(const ElfSection& section : elf.sections) {
		if(section.virtual_address == 0) {
			continue;
		}
		
		u32 section_start = section.virtual_address;
		u32 section_end = section.virtual_address + section.size;
		
		fprintf(out, "%s:\n", section.name.c_str());
		
		//s32 file_count = symbol_database.file_count();
		//for(s32 i = 0; i < file_count; i++) {
		//	Result<mdebug::File> file = symbol_database.parse_file(i);
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

static void test(FILE* out, const fs::path& directory) {
	CCC_CHECK_FATAL(fs::is_directory(directory), "Input path is not a directory.");
	s32 passed = 0;
	s32 skipped = 0;
	for(auto entry : fs::directory_iterator(directory)) {
		fs::path filename = entry.path().filename();
		if(filename.extension() != ".c" && filename.extension() != ".cpp" && filename.extension() != ".md") {
			printf("%s ", entry.path().filename().string().c_str());
			
			Result<std::vector<u8>> binary = platform::read_binary_file(entry.path());
			CCC_EXIT_IF_ERROR(binary);
			
			Result<ElfFile> elf = parse_elf_file(std::move(*binary));
			CCC_EXIT_IF_ERROR(elf);
			
			ElfSection* mdebug_section = elf->lookup_section(".mdebug");
			if(mdebug_section) {
				mdebug::SymbolTableReader reader;
				Result<void> reader_result = reader.init(elf->image, (s32) mdebug_section->file_offset);
				CCC_EXIT_IF_ERROR(reader_result);
				
				SymbolDatabase database;
				Result<SymbolSourceHandle> symbol_source = analyse(database, reader, NO_PARSER_FLAGS);
				CCC_EXIT_IF_ERROR(symbol_source);
				
				//CppPrinter printer(out);
				//for(const std::unique_ptr<ast::Node>& type : symbol_database->deduplicated_types) {
				//	printer.data_type(*type);
				//}
				//for(const std::unique_ptr<ast::SourceFile>& file : symbol_database->source_files) {
				//	for(const std::unique_ptr<ast::Node>& node : file->functions) {
				//		printer.function(node->as<ast::FunctionDefinition>());
				//	}
				//	for(const std::unique_ptr<ast::Node>& node : file->globals) {
				//		printer.global_variable(node->as<ast::Variable>());
				//	}
				//}
				print_json(out, database, false);
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
		{OutputMode::IDENTIFY, "identify"},
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
	puts("  Mdebug/STABS symbol table parser and dumper.");
	puts("");
	puts("Commands:");
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
