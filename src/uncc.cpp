// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "ccc/ccc.h"
#include "platform/file.h"
#define HAVE_DECL_BASENAME 1
#include "demangle.h"

using namespace ccc;

struct Options {
	fs::path elf_path;
	fs::path output_path;
	u32 importer_flags = NO_IMPORTER_FLAGS;
};

struct FunctionsFile {
	std::string contents;
	std::map<u32, std::span<char>> functions;
};

static std::vector<std::string> parse_sources_file(const fs::path& path);
static FunctionsFile parse_functions_file(const fs::path& path);
static std::span<char> eat_line(std::span<char>& input);
static std::string eat_identifier(std::string_view& input);
static void skip_whitespace(std::string_view& input);
static bool should_overwrite_file(const fs::path& path);
static void write_c_cpp_file(
	const fs::path& path,
	const fs::path& header_path,
	const SymbolDatabase& database,
	const std::vector<SourceFileHandle>& files,
	const FunctionsFile& functions_file,
	const ElfFile& elf);
static void write_h_file(
	const fs::path& path,
	std::string relative_path,
	const SymbolDatabase& database,
	const std::vector<SourceFileHandle>& files);
static bool needs_lost_and_found_file(const SymbolDatabase& database);
static void write_lost_and_found_file(const fs::path& path, const SymbolDatabase& database);
static Options parse_command_line_arguments(int argc, char** argv);
static void print_help(int argc, char** argv);

int main(int argc, char** argv)
{
	Options options = parse_command_line_arguments(argc, argv);
	if (options.elf_path.empty()) {
		return 1;
	}
	
	CCC_EXIT_IF_FALSE(fs::is_directory(options.output_path), "Output path needs to be a directory!");
	fs::path sources_file_path = options.output_path/"SOURCES.txt";
	fs::path functions_file_path = options.output_path/"FUNCTIONS.txt";
	
	std::vector<std::string> source_paths = parse_sources_file(sources_file_path);
	FunctionsFile functions_file;
	if (fs::exists(functions_file_path)) {
		functions_file = parse_functions_file(functions_file_path);
	}
	
	Result<std::vector<u8>> image = platform::read_binary_file(options.elf_path);
	CCC_EXIT_IF_ERROR(image);
	
	Result<ElfFile> elf_result = ElfFile::parse(std::move(*image));
	CCC_EXIT_IF_ERROR(elf_result);
	
	Result<std::unique_ptr<ElfSymbolFile>> symbol_file = std::make_unique<ElfSymbolFile>(
		std::move(*elf_result), options.elf_path.filename().string());
	CCC_EXIT_IF_ERROR(symbol_file);
	
	DemanglerFunctions demangler;
	demangler.cplus_demangle = cplus_demangle;
	demangler.cplus_demangle_opname = cplus_demangle_opname;
	
	Result<std::vector<std::unique_ptr<SymbolTable>>> symbol_tables = (*symbol_file)->get_all_symbol_tables();
	CCC_EXIT_IF_ERROR(symbol_tables);
	
	SymbolDatabase database;
	Result<ModuleHandle> module_handle = import_symbol_tables(
		database, (*symbol_file)->name(), *symbol_tables, options.importer_flags, demangler, nullptr);
	CCC_EXIT_IF_ERROR(module_handle);
	
	map_types_to_files_based_on_this_pointers(database);
	map_types_to_files_based_on_reference_count(database);
	
	mdebug::fill_in_pointers_to_member_function_definitions(database);
	
	// Group duplicate source file entries, filter out files not referenced in
	// the SOURCES.txt file.
	std::map<std::string, std::vector<SourceFileHandle>> path_to_source_file;
	size_t path_index = 0;
	for (SourceFile& source_file : database.source_files) {
		if (path_index >= source_paths.size()) {
			break;
		}
		std::string source_name = extract_file_name(source_file.full_path());
		std::string path_name = extract_file_name(source_paths.at(path_index));
		if (source_name == path_name) {
			path_to_source_file[source_paths[path_index++]].emplace_back(source_file.handle());
		}
	}
	
	// Write out all the source files.
	for (auto& [relative_path, sources] : path_to_source_file) {
		fs::path relative_header_path = relative_path;
		relative_header_path.replace_extension(".h");
		
		fs::path path = options.output_path/fs::path(relative_path);
		fs::path header_path = options.output_path/relative_header_path;
		
		fs::create_directories(path.parent_path());
		if (path.extension() == ".c" || path.extension() == ".cpp") {
			// Write .c/.cpp file.
			if (should_overwrite_file(path)) {
				write_c_cpp_file(path, relative_header_path, database, sources, functions_file, (*symbol_file)->elf());
			} else {
				printf(CCC_ANSI_COLOUR_GRAY "Skipping " CCC_ANSI_COLOUR_OFF " %s\n", path.string().c_str());
			}
			// Write .h file.
			if (should_overwrite_file(header_path)) {
				write_h_file(header_path, relative_header_path.string(), database, sources);
			} else {
				printf(CCC_ANSI_COLOUR_GRAY "Skipping " CCC_ANSI_COLOUR_OFF " %s\n", header_path.string().c_str());
			}
		} else {
			printf("Skipping assembly file %s\n", path.string().c_str());
		}
	}
	
	// Write out a lost+found file for types that can't be mapped to a specific
	// source file if we need it.
	if (needs_lost_and_found_file(database)) {
		write_lost_and_found_file(options.output_path/"lost+found.h", database);
	}
}

static std::vector<std::string> parse_sources_file(const fs::path& path)
{
	std::optional<std::string> file = platform::read_text_file(path);
	CCC_EXIT_IF_FALSE(file.has_value(), "Failed to open file '%s'", path.string().c_str());
	std::string_view input(*file);
	std::vector<std::string> sources;
	while (skip_whitespace(input), input.size() > 0) {
		sources.emplace_back(eat_identifier(input));
	}
	return sources;
}

static FunctionsFile parse_functions_file(const fs::path& path)
{
	FunctionsFile result;
	
	std::optional<std::string> file = platform::read_text_file(path);
	CCC_EXIT_IF_FALSE(file.has_value(), "Failed to open file '%s'", path.string().c_str());
	result.contents = std::move(*file);
	
	// Parse the file.
	std::span<char> input(result.contents);
	std::span<char>* function = nullptr;
	for (std::span<char> line = eat_line(input); line.data() != nullptr; line = eat_line(input)) {
		if (line.size() >= 9 && memcmp(line.data(), "@function", 9) == 0) {
			CCC_EXIT_IF_FALSE(line.size() > 10, "Bad @function directive in FUNCTIONS.txt file.");
			char* end = nullptr;
			u32 address = (u32) strtol(line.data() + 10, &end, 16);
			CCC_EXIT_IF_FALSE(end != line.data() + 10, "Bad @function directive in FUNCTIONS.txt file.");
			function = &result.functions[address];
			*function = input.subspan(1);
		} else if (function) {
			*function = std::span<char>(function->data(), line.data() + line.size());
		}
	}
	
	for (auto& [address, code] : result.functions) {
		// Remove everything before the function body.
		for (size_t i = 0; i + 1 < code.size(); i++) {
			if (code[i] == '{' && code[i + 1] == '\n') {
				code = code.subspan(i + 2);
				break;
			}
		}
		
		// Remove everything after the function body.
		for (size_t i = code.size(); i > 1; i--) {
			if (code[i - 2] == '}' && code[i - 1] == '\n') {
				code = code.subspan(0, i - 2);
				break;
			}
		}
	}
	
	return result;
}

static std::span<char> eat_line(std::span<char>& input)
{
	for (size_t i = 0; i < input.size(); i++) {
		if (input[i] == '\n') {
			std::span<char> result = input.subspan(0, i);
			input = input.subspan(i + 1);
			return result;
		}
	}
	return {};
}

static std::string eat_identifier(std::string_view& input)
{
	skip_whitespace(input);
	std::string string;
	size_t i;
	for (i = 0; i < input.size() && !isspace(input[i]); i++) {
		string += input[i];
	}
	input = input.substr(i);
	return string;
}

static void skip_whitespace(std::string_view& input)
{
	while (input.size() > 0 && isspace(input[0])) {
		input = input.substr(1);
	}
}

static bool should_overwrite_file(const fs::path& path)
{
	std::optional<std::string> file = platform::read_text_file(path);
	return !file || file->empty() || file->starts_with("// STATUS: NOT STARTED");
}

static void write_c_cpp_file(
	const fs::path& path,
	const fs::path& header_path,
	const SymbolDatabase& database,
	const std::vector<SourceFileHandle>& files,
	const FunctionsFile& functions_file,
	const ElfFile& elf)
{
	printf("Writing %s\n", path.string().c_str());
	FILE* out = fopen(path.string().c_str(), "w");
	CCC_EXIT_IF_FALSE(out, "Failed to open '%s' for writing.", path.string().c_str());
	fprintf(out, "// STATUS: NOT STARTED\n\n");
	
	// Configure printing.
	CppPrinterConfig config;
	config.print_offsets_and_sizes = false;
	config.print_storage_information = false;
	config.print_variable_data = true;
	config.omit_this_parameter = true;
	config.substitute_parameter_lists = true;
	CppPrinter printer(out, config);
	printer.function_bodies = &functions_file.functions;
	
	printer.include_directive(header_path.filename().string().c_str());
	
	// Print types.
	for (SourceFileHandle file_handle : files) {
		for (const DataType& data_type : database.data_types) {
			if (data_type.only_defined_in_single_translation_unit && data_type.files.size() == 1 && data_type.files[0] == file_handle) {
				printer.data_type(data_type, database);
			}
		}
	}
	
	// Print globals.
	for (SourceFileHandle file_handle : files) {
		const SourceFile* source_file = database.source_files.symbol_from_handle(file_handle);
		CCC_ASSERT(source_file);
		
		const std::vector<GlobalVariableHandle>& global_variables = source_file->global_variables();
		for (const GlobalVariable* global_variable : database.global_variables.symbols_from_handles(global_variables)) {
			printer.global_variable(*global_variable, database, &elf);
		}
	}
	
	// Print functions.
	for (SourceFileHandle file_handle : files) {
		const SourceFile* source_file = database.source_files.symbol_from_handle(file_handle);
		CCC_ASSERT(source_file);
		
		const std::vector<FunctionHandle>& functions = source_file->functions();
		for (const Function* function : database.functions.symbols_from_handles(functions)) {
			printer.function(*function, database, &elf);
		}
	}
	
	fclose(out);
}

static void write_h_file(
	const fs::path& path,
	std::string relative_path,
	const SymbolDatabase& database,
	const std::vector<SourceFileHandle>& files)
{
	printf("Writing %s\n", path.string().c_str());
	FILE* out = fopen(path.string().c_str(), "w");
	fprintf(out, "// STATUS: NOT STARTED\n\n");
	
	// Configure printing.
	CppPrinterConfig config;
	config.make_globals_extern = true;
	config.skip_statics = true;
	config.print_offsets_and_sizes = false;
	config.print_function_bodies = false;
	config.print_storage_information = false;
	config.omit_this_parameter = true;
	config.substitute_parameter_lists = true;
	config.skip_member_functions_outside_types = true;
	CppPrinter printer(out, config);
	
	for (char& c : relative_path) {
		c = toupper(c);
		if (!isalnum(c)) {
			c = '_';
		}
	}
	printer.begin_include_guard(relative_path.c_str());
	
	// Print types.
	for (SourceFileHandle file_handle : files) {
		for (const DataType& data_type : database.data_types) {
			if (!data_type.only_defined_in_single_translation_unit && data_type.files.size() == 1 && data_type.files[0] == file_handle) {
				printer.data_type(data_type, database);
			}
		}
	}
	
	// Print globals.
	bool has_global = false;
	for (SourceFileHandle file_handle : files) {
		const SourceFile* source_file = database.source_files.symbol_from_handle(file_handle);
		CCC_ASSERT(source_file);
		
		const std::vector<GlobalVariableHandle>& global_variables = source_file->global_variables();
		for (const GlobalVariable* global_variable : database.global_variables.symbols_from_handles(global_variables)) {
			printer.global_variable(*global_variable, database, nullptr);
			has_global = true;
		}
	}
	
	if (has_global) {
		fprintf(out, "\n");
	}
	
	// Print functions.
	for (SourceFileHandle file_handle : files) {
		const SourceFile* source_file = database.source_files.symbol_from_handle(file_handle);
		CCC_ASSERT(source_file);
		
		const std::vector<FunctionHandle>& functions = source_file->functions();
		for (const Function* function : database.functions.symbols_from_handles(functions)) {
			printer.function(*function, database, nullptr);
		}
	}
	
	printer.end_include_guard(relative_path.c_str());
	
	fclose(out);
}

static bool needs_lost_and_found_file(const SymbolDatabase& database)
{
	for (const DataType& data_type : database.data_types) {
		if (data_type.files.size() != 1) {
			return true;
		}
	}
	return false;
}

static void write_lost_and_found_file(const fs::path& path, const SymbolDatabase& database)
{
	printf("Writing %s\n", path.string().c_str());
	
	FILE* out = fopen(path.string().c_str(), "w");
	
	CppPrinterConfig config;
	config.print_offsets_and_sizes = false;
	config.omit_this_parameter = true;
	config.substitute_parameter_lists = true;
	CppPrinter printer(out, config);
	
	s32 nodes_printed = 0;
	for (const DataType& data_type : database.data_types) {
		if (data_type.files.size() != 1) {
			if (printer.data_type(data_type, database)) {
				nodes_printed++;
			}
		}
	}
	
	printf("%d types printed to lost and found file\n", nodes_printed);
	
	fclose(out);
}

static Options parse_command_line_arguments(int argc, char** argv)
{
	Options options;
	s32 positional = 0;
	for (s32 i = 1; i < argc; i++) {
		u32 importer_flag = parse_importer_flag(argv[i]);
		if (importer_flag != NO_IMPORTER_FLAGS) {
			options.importer_flags |= importer_flag;
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			print_help(argc, argv);
			return Options();
		} else if (positional == 0) {
			options.elf_path = argv[i];
			positional++;
		} else if (positional == 1) {
			options.output_path = argv[i];
			positional++;
		} else {
			CCC_EXIT("Too many arguments.");
		}
	}
	
	if (options.elf_path.empty() || options.output_path.empty()) {
		print_help(argc, argv);
		return Options();
	}
	
	return options;
}

extern const char* git_tag;

static void print_help(int argc, char** argv)
{
	printf("uncc %s -- https://github.com/chaoticgd/ccc\n",
		(strlen(git_tag) > 0) ? git_tag : "development version");
	printf("\n");
	printf("usage: %s [options] <input elf> <output directory>\n", (argc > 0) ? argv[0] : "uncc");
	printf( "\n");
	printf("Importer Options:\n");
	print_importer_flags_help(stdout);
	printf("\n");
	printf("The GNU demangler is used, which contains source files licensed under the GPL\n");
	printf("and the LGPL. RapidJSON is used under the MIT license. The GoogleTest library is\n");
	printf("used by the test suite under the 3-Clause BSD license.\n");
}
