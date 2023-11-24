// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "ccc/ccc.h"
#include "platform/file.h"
#define HAVE_DECL_BASENAME 1
#include "demanglegnu/demangle.h"

using namespace ccc;

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
static void write_c_cpp_file(const fs::path& path, const fs::path& header_path, const SymbolDatabase& database, const std::vector<SourceFileHandle>& files, const FunctionsFile& functions_file);
static void write_h_file(const fs::path& path, std::string relative_path, const SymbolDatabase& database, const std::vector<SourceFileHandle>& files);
static bool needs_lost_and_found_file(const SymbolDatabase& database);
static void write_lost_and_found_file(const fs::path& path, const SymbolDatabase& database);
static void print_help(int argc, char** argv);

int main(int argc, char** argv) {
	if(argc != 3) {
		print_help(argc, argv);
		return 1;
	}
	
	fs::path elf_path = std::string(argv[1]);
	fs::path output_path = std::string(argv[2]);
	
	CCC_CHECK_FATAL(fs::is_directory(output_path), "Output path needs to be a directory!");
	fs::path sources_file_path = output_path/"SOURCES.txt";
	fs::path functions_file_path = output_path/"FUNCTIONS.txt";
	
	std::vector<std::string> source_paths = parse_sources_file(sources_file_path);
	FunctionsFile functions_file;
	if(fs::exists(functions_file_path)) {
		functions_file = parse_functions_file(functions_file_path);
	}
	
	Result<std::vector<u8>> image = platform::read_binary_file(elf_path);
	CCC_EXIT_IF_ERROR(image);
	
	SymbolDatabase database;
	Result<SymbolSourceHandle> symbol_source = parse_symbol_table(database, std::move(*image), NO_PARSER_FLAGS, cplus_demangle);
	CCC_EXIT_IF_ERROR(symbol_source);
	
	map_types_to_files_based_on_this_pointers(database);
	map_types_to_files_based_on_reference_count(database);
	
	// Fish out the values of global variables (and static locals).
	//std::vector<ElfFile*> modules{&(*elf)};
	//refine_variables(*symbol_table, modules);
	
	fill_in_pointers_to_member_function_definitions(database);
	
	// Group duplicate source file entries, filter out files not referenced in
	// the SOURCES.txt file.
	std::map<std::string, std::vector<SourceFileHandle>> path_to_source_file;
	size_t path_index = 0;
	for(SourceFile& source_file : database.source_files) {
		if(path_index >= source_paths.size()) {
			break;
		}
		std::string source_name = extract_file_name(source_file.full_path());
		std::string path_name = extract_file_name(source_paths.at(path_index));
		if(source_name == path_name) {
			path_to_source_file[source_paths[path_index++]].emplace_back(source_file.handle());
		}
	}
	
	// Write out all the source files.
	for(auto& [relative_path, sources] : path_to_source_file) {
		fs::path relative_header_path = relative_path;
		relative_header_path.replace_extension(".h");
		
		fs::path path = output_path/fs::path(relative_path);
		fs::path header_path = output_path/relative_header_path;
		
		fs::create_directories(path.parent_path());
		if(path.extension() == ".c" || path.extension() == ".cpp") {
			// Write .c/.cpp file.
			if(should_overwrite_file(path)) {
				write_c_cpp_file(path, relative_header_path, database, sources, functions_file);
			} else {
				printf(CCC_ANSI_COLOUR_GRAY "Skipping " CCC_ANSI_COLOUR_OFF " %s\n", path.string().c_str());
			}
			// Write .h file.
			if(should_overwrite_file(header_path)) {
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
	if(needs_lost_and_found_file(database)) {
		write_lost_and_found_file(output_path/"lost+found.h", database);
	}
}

static std::vector<std::string> parse_sources_file(const fs::path& path) {
	std::optional<std::string> file = platform::read_text_file(path);
	CCC_CHECK_FATAL(file.has_value(), "Failed to open file '%s'", path.string().c_str());
	std::string_view input(*file);
	std::vector<std::string> sources;
	while(skip_whitespace(input), input.size() > 0) {
		sources.emplace_back(eat_identifier(input));
	}
	return sources;
}

static FunctionsFile parse_functions_file(const fs::path& path) {
	FunctionsFile result;
	
	std::optional<std::string> file = platform::read_text_file(path);
	CCC_CHECK_FATAL(file.has_value(), "Failed to open file '%s'", path.string().c_str());
	result.contents = std::move(*file);
	
	// Parse the file.
	std::span<char> input(result.contents);
	std::span<char>* function = nullptr;
	for(std::span<char> line = eat_line(input); line.data() != nullptr; line = eat_line(input)) {
		if(line.size() >= 9 && memcmp(line.data(), "@function", 9) == 0) {
			CCC_CHECK_FATAL(line.size() > 10, "Bad @function directive in FUNCTIONS.txt file.");
			char* end = nullptr;
			u32 address = (u32) strtol(line.data() + 10, &end, 16);
			CCC_CHECK_FATAL(end != line.data() + 10, "Bad @function directive in FUNCTIONS.txt file.");
			function = &result.functions[address];
			*function = input.subspan(1);
		} else if(function) {
			*function = std::span<char>(function->data(), line.data() + line.size());
		}
	}
	
	for(auto& [address, code] : result.functions) {
		// Remove everything before the function body.
		for(size_t i = 0; i + 1 < code.size(); i++) {
			if(code[i] == '{' && code[i + 1] == '\n') {
				code = code.subspan(i + 2);
				break;
			}
		}
		
		// Remove everything after the function body.
		for(size_t i = code.size(); i > 1; i--) {
			if(code[i - 2] == '}' && code[i - 1] == '\n') {
				code = code.subspan(0, i - 2);
				break;
			}
		}
	}
	
	return result;
}

static std::span<char> eat_line(std::span<char>& input) {
	for(size_t i = 0; i < input.size(); i++) {
		if(input[i] == '\n') {
			std::span<char> result = input.subspan(0, i);
			input = input.subspan(i + 1);
			return result;
		}
	}
	return {};
}

static std::string eat_identifier(std::string_view& input) {
	skip_whitespace(input);
	std::string string;
	size_t i;
	for(i = 0; i < input.size() && !isspace(input[i]); i++) {
		string += input[i];
	}
	input = input.substr(i);
	return string;
}

static void skip_whitespace(std::string_view& input) {
	while(input.size() > 0 && isspace(input[0])) {
		input = input.substr(1);
	}
}

static bool should_overwrite_file(const fs::path& path) {
	std::optional<std::string> file = platform::read_text_file(path);
	return !file || file->empty() || file->starts_with("// STATUS: NOT STARTED");
}

static void write_c_cpp_file(const fs::path& path, const fs::path& header_path, const SymbolDatabase& database, const std::vector<SourceFileHandle>& files, const FunctionsFile& functions_file) {
	printf("Writing %s\n", path.string().c_str());
	FILE* out = fopen(path.string().c_str(), "w");
	CCC_CHECK_FATAL(out, "Failed to open '%s' for writing.", path.string().c_str());
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
	for(SourceFileHandle file_handle : files) {
		for(const DataType& data_type : database.data_types) {
			if(data_type.probably_defined_in_cpp_file && data_type.files.size() == 1 && data_type.files[0] == file_handle) {
				printer.data_type(data_type);
			}
		}
	}
	
	// Print globals.
	for(SourceFileHandle file_handle : files) {
		const SourceFile* source_file = database.source_files[file_handle];
		CCC_ASSERT(source_file);
		GlobalVariableRange global_variables = source_file->globals_variables();
		for(const GlobalVariable& global_variable : database.global_variables.span(global_variables)) {
			printer.global_variable(global_variable);
		}
	}
	
	// Print functions.
	for(SourceFileHandle file_handle : files) {
		const SourceFile* source_file = database.source_files[file_handle];
		CCC_ASSERT(source_file);
		FunctionRange functions = source_file->functions();
		for(const Function& function : database.functions.span(functions)) {
			printer.function(function, database);
		}
	}
	
	fclose(out);
}

static void write_h_file(const fs::path& path, std::string relative_path, const SymbolDatabase& database, const std::vector<SourceFileHandle>& files) {
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
	
	for(char& c : relative_path) {
		c = toupper(c);
		if(!isalnum(c)) {
			c = '_';
		}
	}
	printer.begin_include_guard(relative_path.c_str());
	
	// Print types.
	for(SourceFileHandle file_handle : files) {
		for(const DataType& data_type : database.data_types) {
			if(!data_type.probably_defined_in_cpp_file && data_type.files.size() == 1 && data_type.files[0] == file_handle) {
				printer.data_type(data_type);
			}
		}
	}
	
	// Print globals.
	bool has_global = false;
	for(SourceFileHandle file_handle : files) {
		const SourceFile* source_file = database.source_files[file_handle];
		CCC_ASSERT(source_file);
		GlobalVariableRange global_variables = source_file->globals_variables();
		for(const GlobalVariable& global_variable : database.global_variables.span(global_variables)) {
			printer.global_variable(global_variable);
			has_global = true;
		}
	}
	if(has_global) {
		fprintf(out, "\n");
	}
	
	// Print functions.
	for(SourceFileHandle file_handle : files) {
		const SourceFile* source_file = database.source_files[file_handle];
		CCC_ASSERT(source_file);
		FunctionRange functions = source_file->functions();
		for(const Function& function : database.functions.span(functions)) {
			printer.function(function, database);
		}
	}
	
	printer.end_include_guard(relative_path.c_str());
	
	fclose(out);
}

static bool needs_lost_and_found_file(const SymbolDatabase& database) {
	for(const DataType& data_type : database.data_types) {
		if(data_type.files.size() != 1) {
			return true;
		}
	}
	return false;
}

static void write_lost_and_found_file(const fs::path& path, const SymbolDatabase& database) {
	printf("Writing %s\n", path.string().c_str());
	FILE* out = fopen(path.string().c_str(), "w");
	CppPrinterConfig config;
	config.print_offsets_and_sizes = false;
	config.omit_this_parameter = true;
	config.substitute_parameter_lists = true;
	CppPrinter printer(out, config);
	s32 nodes_printed = 0;
	for(const DataType& data_type : database.data_types) {
		if(data_type.files.size() != 1) {
			if(printer.data_type(data_type)) {
				nodes_printed++;
			}
		}
	}
	printf("%d types printed to lost and found file\n", nodes_printed);
	fclose(out);
}

const char* git_tag();

static void print_help(int argc, char** argv) {
	const char* tag = git_tag();
	printf("uncc %s -- https://github.com/chaoticgd/ccc\n",
		(strlen(tag) > 0) ? tag : "development version");
	printf("\n");
	printf("usage: %s <input elf> <output directory>\n", (argc > 0) ? argv[0] : "uncc");
	printf("\n");
	printf("The demangler library used is licensed under the LGPL, the rest is MIT licensed.\n");
	printf("See the License.txt and DemanglerLicense.txt files for more information.\n");
}
