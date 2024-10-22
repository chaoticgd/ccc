// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <algorithm>

#include "ccc/ccc.h"
#include "platform/file.h"
#define HAVE_DECL_BASENAME 1
#include "demangle.h"

using namespace ccc;

enum Flags {
	NO_FLAGS = 0,
	FLAG_SORT_BY_ADDRESS = 1 << 0,
	FLAG_CALLER_STACK_OFFSETS = 1 << 1,
	FLAG_LOCAL_SYMBOLS = 1 << 2,
	FLAG_PROCEDURE_DESCRIPTORS = 1 << 3,
	FLAG_EXTERNAL_SYMBOLS = 1 << 4
};

struct Options {
	void (*function)(FILE* out, const Options& options) = nullptr;
	fs::path input_file;
	fs::path output_file;
	u32 flags = NO_FLAGS;
	u32 importer_flags = NO_IMPORTER_FLAGS;
	std::vector<SymbolTableLocation> sections;
};

static void identify_symbol_tables(FILE* out, const Options& options);
static void identify_symbol_tables_in_file(FILE* out, u32* totals, u32* unknown_total, const fs::path& file_path);
static void print_functions(FILE* out, const Options& options);
static void print_globals(FILE* out, const Options& options);
static void print_types(FILE* out, const Options& options);
static void print_types_deduplicated(FILE* out, SymbolDatabase& database, const Options& options);
static void print_types_per_file(FILE* out, SymbolDatabase& database, const Options& options);
static void print_type_graph(FILE* out, const Options& options);
static void print_labels(FILE* out, const Options& options);
static void print_json(FILE* out, const Options& options);
static void print_symbols(FILE* out, const Options& options);
static void print_headers(FILE* out, const Options& options);
static void print_files(FILE* out, const Options& options);
static void print_includes(FILE* out, const Options& options);
static void print_sections(FILE* out, const Options& options);
static SymbolDatabase read_symbol_table(std::unique_ptr<SymbolFile>& symbol_file, const Options& options);
static std::vector<std::unique_ptr<SymbolTable>> select_symbol_tables(
	SymbolFile& symbol_file, const std::vector<SymbolTableLocation>& sections);
static Options parse_command_line_arguments(int argc, char** argv);
static void print_help(FILE* out);
static const char* get_version();

struct StdumpCommand {
	void (*function)(FILE* out, const Options& options);
	const char* name;
	std::vector<const char*> help_text;
};

static const StdumpCommand commands[] = {
	{identify_symbol_tables, "identify", {
		"Identify the symbol table(s) present in the input file(s). If the input path",
		"is a directory, it will be walked recursively."
	}},
	{print_functions, "functions", {
		"Print all the functions defined in the input symbol table(s) as C++."
	}},
	{print_globals, "globals", {
		"Print all the global variables defined in the input symbol table(s) as C++."
	}},
	{print_types, "types", {
		"Print all the types defined in the input symbol table(s) as C++."
	}},
	{print_type_graph, "type_graph", {
		"Print a dependency graph of all the types as a graphviz DOT file."
	}},
	{print_labels, "labels", {
		"Print all the labels defined in the input symbol table(s). Note that this",
		"may include other symbols where their type is not recoverable."
	}},
	{print_json, "json", {
		"Print all of the above as JSON."
	}},
	{print_symbols, "symbols", {
		"Print the raw symbols in the input symbol table(s). If no additional options",
		"are passed, the default behaviour is to print the local and external .mdebug",
		"symbols, but not the procedure descriptors.",
		"",
		"--locals                      Print local .mdebug symbols.",
		"",
		"--procedures                  Print .mdebug procedure descriptors.",
		"",
		"--externals                   Print external .mdebug symbols."
	}},
	{print_headers, "headers", {
		"Print the contents of the .mdebug header."
	}},
	{print_files, "files", {
		"Print a list of all the source files."
	}},
	{print_includes, "includes", {
		"Print a list of the include paths stored with .mdebug inlining information."
	}},
	{print_sections, "sections", {
		"List the names of the source files associated with each ELF section."
	}}
};

int main(int argc, char** argv)
{
	Options options = parse_command_line_arguments(argc, argv);
	
	FILE* out = stdout;
	if (!options.output_file.empty()) {
		out = fopen(options.output_file.string().c_str(), "w");
		CCC_EXIT_IF_FALSE(out, "Failed to open output file '%s'.", options.output_file.string().c_str());
	}
	
	if (options.function) {
		options.function(out, options);
	} else {
		print_help(out);
		return 1;
	}
}

static void identify_symbol_tables(FILE* out, const Options& options)
{
	if (fs::is_regular_file(options.input_file)) {
		identify_symbol_tables_in_file(out, nullptr, nullptr, options.input_file);
	} else if (fs::is_directory(options.input_file)) {
		std::vector<u32> totals(SYMBOL_TABLE_FORMATS.size(), 0);
		u32 unknown_total = 0;
		
		for (auto entry : fs::recursive_directory_iterator(options.input_file)) {
			if (entry.is_regular_file()) {
				identify_symbol_tables_in_file(out, totals.data(), &unknown_total, entry.path());
			}
		}
		
		fprintf(out, "\n");
		fprintf(out, "Totals:\n");
		for (size_t i = 0; i < SYMBOL_TABLE_FORMATS.size(); i++) {
			fprintf(out, "  %4d %s sections\n", totals[i], SYMBOL_TABLE_FORMATS[i].section_name);
		}
		fprintf(out, "  %4d unknown\n", unknown_total);
	} else {
		CCC_EXIT("Input path '%s' is neither a regular file nor a directory.", options.input_file.string().c_str());
	}
}

static void identify_symbol_tables_in_file(FILE* out, u32* totals, u32* unknown_total, const fs::path& file_path)
{
	fprintf(out, "%100s:", file_path.string().c_str());
	
	Result<std::vector<u8>> file = platform::read_binary_file(file_path);
	CCC_EXIT_IF_ERROR(file);
	
	const u32* fourcc = get_packed<u32>(*file, 0);
	if (!fourcc) {
		fprintf(out, " file too small\n");
		return;
	}
	
	switch (*fourcc) {
		case CCC_FOURCC("\x7f""ELF"): {
			Result<ElfFile> elf = ElfFile::parse(std::move(*file));
			if (!elf.success()) {
				fprintf(out, " %s\n", elf.error().message.c_str());
				break;
			}
			
			bool print_none = true;
			for (size_t i = 0; i < SYMBOL_TABLE_FORMATS.size(); i++) {
				if (elf->lookup_section(SYMBOL_TABLE_FORMATS[i].section_name)) {
					fprintf(out, " %s", SYMBOL_TABLE_FORMATS[i].section_name);
					if (totals) {
						totals[i]++;
					}
					print_none = false;
				}
			}
			
			if (print_none) {
				fprintf(out, " none");
			}
			
			fprintf(out, "\n");
			
			break;
		}
		case CCC_FOURCC("SNR1"):
		case CCC_FOURCC("SNR2"): {
			if (totals) {
				totals[SNDLL]++;
			}
			fprintf(out, " sndll\n");
			break;
		}
		default: {
			if (unknown_total) {
				(*unknown_total)++;
			}
			fprintf(out, " unknown format\n");
			break;
		}
	}
}

static void print_functions(FILE* out, const Options& options)
{
	std::unique_ptr<SymbolFile> symbol_file;
	SymbolDatabase database = read_symbol_table(symbol_file, options);
	
	std::vector<const Function*> functions;
	for (const Function& function : database.functions) {
		functions.emplace_back(&function);
	}
	
	if (options.flags & FLAG_SORT_BY_ADDRESS) {
		std::sort(CCC_BEGIN_END(functions), [&](const Function* lhs, const Function* rhs) {
			return lhs->address() < rhs->address();
		});
	}
	
	CppPrinterConfig config;
	config.caller_stack_offsets = options.flags & FLAG_CALLER_STACK_OFFSETS;
	
	CppPrinter printer(out, config);
	
	printer.comment_block_beginning(options.input_file.filename().string().c_str(), "stdump", get_version());
	
	bool first_iteration = true;
	SourceFileHandle source_file_handle;
	for (const Function* function : functions) {
		if (function->source_file() != source_file_handle || first_iteration) {
			SourceFile* source_file = database.source_files.symbol_from_handle(function->source_file());
			if (source_file) {
				printer.comment_block_file(source_file->full_path().c_str());
				source_file_handle = source_file->handle();
			} else {
				printer.comment_block_file("(unknown)");
				source_file_handle = SourceFileHandle();
			}
			first_iteration = false;
		}
		
		printer.function(*function, database, nullptr);
	}
}

static void print_globals(FILE* out, const Options& options)
{
	std::unique_ptr<SymbolFile> symbol_file;
	SymbolDatabase database = read_symbol_table(symbol_file, options);
	
	std::vector<const GlobalVariable*> global_variables;
	for (const GlobalVariable& global_variable : database.global_variables) {
		global_variables.emplace_back(&global_variable);
	}
	
	if (options.flags & FLAG_SORT_BY_ADDRESS) {
		std::sort(CCC_BEGIN_END(global_variables), [&](const GlobalVariable* lhs, const GlobalVariable* rhs) {
			return lhs->address() < rhs->address();
		});
	}
	
	CppPrinterConfig config;
	config.caller_stack_offsets = options.flags & FLAG_CALLER_STACK_OFFSETS;
	
	CppPrinter printer(out, config);
	
	printer.comment_block_beginning(options.input_file.filename().string().c_str(), "stdump", get_version());
	
	bool first_iteration = true;
	SourceFileHandle source_file_handle;
	for (const GlobalVariable* global_variable : global_variables) {
		if (global_variable->source_file() != source_file_handle || first_iteration) {
			SourceFile* source_file = database.source_files.symbol_from_handle(global_variable->source_file());
			if (source_file) {
				printer.comment_block_file(source_file->full_path().c_str());
				source_file_handle = source_file->handle();
			} else {
				printer.comment_block_file("(unknown)");
				source_file_handle = SourceFileHandle();
			}
			first_iteration = false;
		}
		
		printer.global_variable(*global_variable, database, nullptr);
	}
}

static void print_types(FILE* out, const Options& options)
{
	std::unique_ptr<SymbolFile> symbol_file;
	SymbolDatabase database = read_symbol_table(symbol_file, options);
	
	if ((options.flags & DONT_DEDUPLICATE_TYPES) == 0) {
		print_types_deduplicated(out, database, options);
	} else {
		print_types_per_file(out, database, options);
	}
}

static void print_types_deduplicated(FILE* out, SymbolDatabase& database, const Options& options)
{
	CppPrinterConfig config;
	config.caller_stack_offsets = options.flags & FLAG_CALLER_STACK_OFFSETS;
	
	CppPrinter printer(out, config);
	
	printer.comment_block_beginning(options.input_file.filename().string().c_str(), "stdump", get_version());
	printer.comment_block_toolchain_version_info(database);
	printer.comment_block_builtin_types(database);
	
	for (const DataType& data_type : database.data_types) {
		printer.data_type(data_type, database);
	}
}

static void print_types_per_file(FILE* out, SymbolDatabase& database, const Options& options)
{
	CppPrinterConfig config;
	config.caller_stack_offsets = options.flags & FLAG_CALLER_STACK_OFFSETS;
	
	CppPrinter printer(out, config);
	
	printer.comment_block_beginning(options.input_file.filename().string().c_str(), "stdump", get_version());
	
	for (const SourceFile& source_file : database.source_files) {
		printer.comment_block_file(source_file.full_path().c_str());
		printer.comment_block_toolchain_version_info(database);
		printer.comment_block_builtin_types(database, source_file.handle());
		for (const DataType& data_type : database.data_types) {
			if (data_type.files.size() == 1 && data_type.files[0] == source_file.handle()) {
				printer.data_type(data_type, database);
			}
		}
	}
}

static void print_type_graph(FILE* out, const Options& options)
{
	std::unique_ptr<SymbolFile> symbol_file;
	SymbolDatabase database = read_symbol_table(symbol_file, options);
	
	TypeDependencyAdjacencyList graph = build_type_dependency_graph(database);
	print_type_dependency_graph(out, database, graph);
}

static void print_labels(FILE* out, const Options& options)
{
	std::unique_ptr<SymbolFile> symbol_file;
	SymbolDatabase database = read_symbol_table(symbol_file, options);
	
	std::vector<const Label*> labels;
	for (const Label& label : database.labels) {
		labels.emplace_back(&label);
	}
	
	if (options.flags & FLAG_SORT_BY_ADDRESS) {
		std::sort(CCC_BEGIN_END(labels), [&](const Label* lhs, const Label* rhs) {
			return lhs->address() < rhs->address();
		});
	}
	
	for (const Label* label : labels) {
		fprintf(out, "%08x %s\n", label->address().value, label->name().c_str());
	}
}

static void print_json(FILE* out, const Options& options)
{
	std::unique_ptr<SymbolFile> symbol_file;
	SymbolDatabase database = read_symbol_table(symbol_file, options);
	rapidjson::StringBuffer buffer;
	JsonWriter writer(buffer);
	write_json(writer, database, "stdump");
	fprintf(out, "%s", buffer.GetString());
}

static void print_symbols(FILE* out, const Options& options)
{
	Result<std::vector<u8>> image = platform::read_binary_file(options.input_file);
	CCC_EXIT_IF_ERROR(image);
	
	Result<std::unique_ptr<SymbolFile>> symbol_file = parse_symbol_file(
		std::move(*image), options.input_file.filename().string());
	CCC_EXIT_IF_ERROR(symbol_file);
	
	std::vector<std::unique_ptr<SymbolTable>> symbol_tables = select_symbol_tables(**symbol_file, options.sections);
	
	u32 print_flags = 0;
	if (options.flags & FLAG_LOCAL_SYMBOLS) print_flags |= PRINT_LOCALS;
	if (options.flags & FLAG_PROCEDURE_DESCRIPTORS) print_flags |= PRINT_PROCEDURE_DESCRIPTORS;
	if (options.flags & FLAG_EXTERNAL_SYMBOLS) print_flags |= PRINT_EXTERNALS;
	
	if (print_flags == 0) {
		print_flags |= PRINT_LOCALS;
		print_flags |= PRINT_EXTERNALS;
	}
	
	for (const std::unique_ptr<SymbolTable>& symbol_table : symbol_tables) {
		Result<void> result = symbol_table->print_symbols(out, print_flags);
		CCC_EXIT_IF_ERROR(result);
	}
}

static void print_headers(FILE* out, const Options& options)
{
	Result<std::vector<u8>> image = platform::read_binary_file(options.input_file);
	CCC_EXIT_IF_ERROR(image);
	
	Result<std::unique_ptr<SymbolFile>> symbol_file = parse_symbol_file(
		std::move(*image), options.input_file.filename().string());
	CCC_EXIT_IF_ERROR(symbol_file);
	
	std::vector<std::unique_ptr<SymbolTable>> symbol_tables = select_symbol_tables(**symbol_file, options.sections);
	
	for (const std::unique_ptr<SymbolTable>& symbol_table : symbol_tables) {
		Result<void> result = symbol_table->print_headers(out);
		CCC_EXIT_IF_ERROR(result);
	}
}

static void print_files(FILE* out, const Options& options)
{
	std::unique_ptr<SymbolFile> symbol_file;
	SymbolDatabase database = read_symbol_table(symbol_file, options);
	
	for (const SourceFile& source_file : database.source_files) {
		fprintf(out, "%08x %s\n", source_file.address().value, source_file.name().c_str());
	}
}

static void print_includes(FILE* out, const Options& options)
{
	std::unique_ptr<SymbolFile> symbol_file;
	SymbolDatabase database = read_symbol_table(symbol_file, options);
	
	std::set<std::string> includes;
	for (const Function& function : database.functions) {
		const SourceFile* source_file = database.source_files.symbol_from_handle(function.source_file());
		if (!source_file) {
			continue;
		}
		
		for (const Function::SubSourceFile& sub_source : function.sub_source_files) {
			if (sub_source.relative_path != source_file->command_line_path) {
				includes.emplace(sub_source.relative_path);
			}
		}
	}
	
	for (const std::string& include : includes) {
		fprintf(out, "%s\n", include.c_str());
	}
}

static void print_sections(FILE* out, const Options& options)
{
	std::unique_ptr<SymbolFile> symbol_file;
	SymbolDatabase database = read_symbol_table(symbol_file, options);
	
	for (const Section& section : database.sections) {
		if (!section.address().valid()) {
			continue;
		}
		
		u32 section_start = section.address().value;
		u32 section_end = section.address().value + section.size();
		
		fprintf(out, "%s:\n", section.name().c_str());
		
		for (const SourceFile& source_file : database.source_files) {
			if (source_file.address().valid() && source_file.address() >= section_start && source_file.address() < section_end) {
				fprintf(out, "\t%s\n", source_file.full_path().c_str());
			}
		}
	}
}

static SymbolDatabase read_symbol_table(std::unique_ptr<SymbolFile>& symbol_file, const Options& options)
{
	Result<std::vector<u8>> image = platform::read_binary_file(options.input_file);
	CCC_EXIT_IF_ERROR(image);
	
	Result<std::unique_ptr<SymbolFile>> symbol_file_result = parse_symbol_file(
		std::move(*image), options.input_file.filename().string());
	CCC_EXIT_IF_ERROR(symbol_file_result);
	symbol_file = std::move(*symbol_file_result);
	
	SymbolDatabase database;
	
	std::vector<std::unique_ptr<SymbolTable>> symbol_tables = select_symbol_tables(*symbol_file, options.sections);
	
	DemanglerFunctions demangler;
	demangler.cplus_demangle = cplus_demangle;
	demangler.cplus_demangle_opname = cplus_demangle_opname;
	
	Result<ModuleHandle> module_handle = import_symbol_tables(
		database, symbol_file->name(), symbol_tables, options.importer_flags, demangler, nullptr);
	CCC_EXIT_IF_ERROR(module_handle);
	
	return database;
}

static std::vector<std::unique_ptr<SymbolTable>> select_symbol_tables(
	SymbolFile& symbol_file, const std::vector<SymbolTableLocation>& sections)
{
	std::vector<std::unique_ptr<SymbolTable>> symbol_tables;
	if (!sections.empty()) {
		Result<std::vector<std::unique_ptr<SymbolTable>>> symbol_tables_result = symbol_file.get_symbol_tables_from_sections(sections);
		CCC_EXIT_IF_ERROR(symbol_tables_result);
		symbol_tables = std::move(*symbol_tables_result);
	} else {
		Result<std::vector<std::unique_ptr<SymbolTable>>> symbol_tables_result = symbol_file.get_all_symbol_tables();
		CCC_EXIT_IF_ERROR(symbol_tables_result);
		symbol_tables = std::move(*symbol_tables_result);
	}
	return symbol_tables;
}

static Options parse_command_line_arguments(int argc, char** argv)
{
	Options options;
	if (argc < 2) {
		return options;
	}
	
	const char* name = argv[1];
	bool require_input_path = false;
	for (const StdumpCommand& command : commands) {
		if (strcmp(name, command.name) == 0) {
			options.function = command.function;
			require_input_path = true;
			break;
		}
	}
	
	bool input_path_provided = false;
	for (s32 i = 2; i < argc; i++) {
		const char* arg = argv[i];
		
		u32 importer_flag = parse_importer_flag(arg);
		if (importer_flag != NO_IMPORTER_FLAGS) {
			options.importer_flags |= importer_flag;
		} else if (strcmp(arg, "--sort-by-address") == 0) {
			options.flags |= FLAG_SORT_BY_ADDRESS;
		} else if (strcmp(arg, "--caller-stack-offsets") == 0) {
			options.flags |= FLAG_CALLER_STACK_OFFSETS;
		} else if (strcmp(arg, "--locals") == 0) {
			options.flags |= FLAG_LOCAL_SYMBOLS;
		} else if (strcmp(arg, "--procedures") == 0) {
			options.flags |= FLAG_PROCEDURE_DESCRIPTORS;
		} else if (strcmp(arg, "--externals") == 0) {
			options.flags |= FLAG_EXTERNAL_SYMBOLS;
		} else if (strcmp(arg, "--output") == 0 || strcmp(arg, "-o") == 0) {
			if (i + 1 < argc) {
				options.output_file = argv[++i];
			} else {
				CCC_EXIT("No output path specified.");
			}
		} else if (strcmp(arg, "--section") == 0) {
			if (i + 2 < argc) {
				SymbolTableLocation& section = options.sections.emplace_back();
				section.section_name = argv[++i];
				
				const SymbolTableFormatInfo* info = symbol_table_format_from_name(argv[++i]);
				CCC_EXIT_IF_FALSE(info, "Invalid symbol table format specified.");
				
				section.format = info->format;
			} else if (i + 1 < argc) {
				CCC_EXIT("Missing format after --section.");
			} else {
				CCC_EXIT("Missing section name after --section.");
			}
		} else if (strncmp(arg, "--", 2) == 0) {
			CCC_EXIT("Unknown option '%s'.", arg);
		} else if (input_path_provided) {
			CCC_EXIT("Multiple input paths specified.");
		} else {
			options.input_file = argv[i];
			input_path_provided = true;
		}
	}
	
	CCC_EXIT_IF_FALSE(!require_input_path || !options.input_file.empty(), "No input path specified.");
	
	return options;
}

static void print_help(FILE* out)
{
	fprintf(out, "stdump %s -- https://github.com/chaoticgd/ccc\n", get_version());
	fprintf(out, "  Symbol table parser and dumper for PlayStation 2 games.\n");
	fprintf(out, "\n");
	fprintf(out, "Commands:\n");
	fprintf(out, "\n");
	for (const StdumpCommand& command : commands) {
		fprintf(out, "  %s [options] <input file>\n", command.name);
		for (const char* line : command.help_text) {
			fprintf(out, "    %s\n", line);
		}
		fprintf(out, "\n");
	}
	fprintf(out, "  help | --help | -h\n");
	fprintf(out, "    Print this help message.\n");
	fprintf(out, "\n");
	fprintf(out, "Options:\n");
	fprintf(out, "\n");
	fprintf(out, "  --output | -o <output file>   Write the output to the file specified instead\n");
	fprintf(out, "                                of to the standard output.\n");
	fprintf(out, "\n");
	
	fprintf(out, "  --section <section> <format>  Explicitly specify a symbol table to load. This\n");
	fprintf(out, "                                option can be used multiple times to specify\n");
	fprintf(out, "                                multiple symbol tables to load. The symbol\n");
	fprintf(out, "                                tables specified first, on the left side of the\n");
	fprintf(out, "                                command line, will be given higher priority. If\n");
	fprintf(out, "                                this option is not used, all recognized symbol\n");
	fprintf(out, "                                tables will be loaded.\n");
	fprintf(out, "\n");
	
	const char* common_section_names_are = "Common section names are: ";
	fprintf(out, "                                %s", common_section_names_are);
	
	// Print out a line wrapped list of common section names.
	s32 column = 32 + (s32) strlen(common_section_names_are);
	for (size_t i = 0; i < SYMBOL_TABLE_FORMATS.size(); i++) {
		const SymbolTableFormatInfo& format = SYMBOL_TABLE_FORMATS[i];
		if (column + strlen(format.section_name) + 2 > 80) {
			fprintf(out, "\n                                ");
			column = 32;
		}
		fprintf(out, "%s", format.section_name);
		if (i + 1 == SYMBOL_TABLE_FORMATS.size()) {
			fprintf(out, ".\n");
		} else {
			fprintf(out, ", ");
		}
		column += (s32) strlen(format.section_name) + 2;
	}
	
	// Print out a line wrapped list of supported symbol table formats.
	const char* supported_formats_are = "Supported formats are: ";
	fprintf(out, "\n");
	fprintf(out, "                                %s", supported_formats_are);
	column = 32 + (s32) strlen(supported_formats_are);
	for (u32 i = 0; i < SYMBOL_TABLE_FORMATS.size(); i++) {
		const SymbolTableFormatInfo& format = SYMBOL_TABLE_FORMATS[i];
		if (column + strlen(format.format_name) + 2 > 80) {
			fprintf(out, "\n                                ");
			column = 32;
		}
		fprintf(out, "%s", format.format_name);
		if (i + 1 == SYMBOL_TABLE_FORMATS.size()) {
			fprintf(out, ".\n");
		} else {
			fprintf(out, ", ");
		}
		column += (s32) strlen(format.format_name) + 2;
	}
	
	fprintf(out, "\n");
	fprintf(out, "  --sort-by-address             Sort symbols by their addresses.\n");
	fprintf(out, "\n");
	fprintf(out, "  --caller-stack-offsets        Print the offsets of stack variables relative to\n");
	fprintf(out, "                                to value of the stack pointer register in the\n");
	fprintf(out, "                                caller rather than the value of the stack\n");
	fprintf(out, "                                pointer in the current function. These offsets\n");
	fprintf(out, "                                will be printed out as \"0xN(caller sp)\" instead\n");
	fprintf(out, "                                of \"0xN(sp)\". This option does not affect the\n");
	fprintf(out, "                                JSON output.\n");
	fprintf(out, "\n");
	fprintf(out, "Importer Options:\n");
	print_importer_flags_help(out);
	printf("\n");
	printf("The GNU demangler is used, which contains source files licensed under the GPL\n");
	printf("and the LGPL. RapidJSON is used under the MIT license. The GoogleTest library is\n");
	printf("used by the test suite under the 3-Clause BSD license.\n");
}

extern const char* git_tag;

static const char* get_version()
{
	return (git_tag && strlen(git_tag) > 0) ? git_tag : "development version";
}
