#include "ccc/ccc.h"
#define HAVE_DECL_BASENAME 1
#include "demanglegnu/demangle.h"

using namespace ccc;

struct FunctionsFile {
	std::string contents;
	std::map<s32, std::span<char>> functions;
};

static std::vector<std::string> parse_sources_file(const fs::path& path);
static FunctionsFile parse_functions_file(const fs::path& path);
static std::span<char> eat_line(std::span<char>& input);
static std::string eat_identifier(std::string_view& input);
static void skip_whitespace(std::string_view& input);
static bool should_overwrite_file(const fs::path& path);
static void demangle_all(HighSymbolTable& high);
static void write_c_cpp_file(const fs::path& path, const fs::path& header_path, const HighSymbolTable& high, const std::vector<s32>& file_indices, const FunctionsFile& functions_file);
static void write_h_file(const fs::path& path, std::string relative_path, const HighSymbolTable& high, const std::vector<s32>& file_indices);
static bool needs_lost_and_found_file(const HighSymbolTable& high);
static void write_lost_and_found_file(const fs::path& path, const HighSymbolTable& high);
static void print_help(int argc, char** argv);

int main(int argc, char** argv) {
	if(argc != 3) {
		print_help(argc, argv);
		return 1;
	}
	
	fs::path elf_path = std::string(argv[1]);
	fs::path output_path = std::string(argv[2]);
	
	verify(fs::is_directory(output_path), "Output path needs to be a directory!");
	fs::path sources_file_path = output_path/"SOURCES.txt";
	fs::path functions_file_path = output_path/"FUNCTIONS.txt";
	
	std::vector<std::string> source_paths = parse_sources_file(sources_file_path);
	FunctionsFile functions_file;
	if(fs::exists(functions_file_path)) {
		functions_file = parse_functions_file(functions_file_path);
	}
	
	Module mod;
	mdebug::SymbolTable symbol_table = read_symbol_table(mod, elf_path);
	HighSymbolTable high = analyse(symbol_table, DEDUPLICATE_TYPES | STRIP_GENERATED_FUNCTIONS);
	map_types_to_files_based_on_this_pointers(high);
	map_types_to_files_based_on_reference_count(high);
	demangle_all(high);
	
	// Fish out the values of global variables (and static locals).
	std::vector<Module*> modules{&mod};
	refine_variables(high, modules);
	
	fill_in_pointers_to_member_function_definitions(high);
	
	// Group duplicate source file entries, filter out files not referenced in
	// the SOURCES.txt file.
	std::map<std::string, std::vector<s32>> path_to_source_file;
	for(size_t path_index = 0, source_index = 0; path_index < source_paths.size() && source_index < high.source_files.size(); path_index++, source_index++) {
		// Find the next file referenced in the SOURCES.txt file.
		std::string source_name = extract_file_name(source_paths[path_index]);
		while(source_index < high.source_files.size()) {
			std::string symbol_name = extract_file_name(high.source_files[source_index]->full_path);
			if(symbol_name == source_name) {
				break;
			}
			printf("Skipping %s (not referenced, expected %s next)\n", symbol_name.c_str(), source_name.c_str());
			source_index++;
		}
		if(source_index >= high.source_files.size()) {
			break;
		}
		// Add the file.
		path_to_source_file[source_paths[path_index]].emplace_back((s32) source_index);
	}
	
	// Write out all the source files.
	for(auto& [relative_path, sources] : path_to_source_file) {
		fs::path path = output_path/fs::path(relative_path);
		fs::create_directories(path.parent_path());
		if(path.extension() == ".c" || path.extension() == ".cpp") {
			fs::path relative_header_path = relative_path;
			relative_header_path.replace_extension(".h");
			// Write .c/.cpp file.
			if(should_overwrite_file(path)) {
				write_c_cpp_file(path, relative_header_path, high, sources, functions_file);
			} else {
				printf(ANSI_COLOUR_GRAY "Skipping " ANSI_COLOUR_OFF " %s\n", path.string().c_str());
			}
			// Write .h file.
			fs::path header_path = path.replace_extension(".h");
			if(should_overwrite_file(header_path)) {
				write_h_file(header_path, relative_header_path.string(), high, sources);
			} else {
				printf(ANSI_COLOUR_GRAY "Skipping " ANSI_COLOUR_OFF " %s\n", header_path.string().c_str());
			}
		} else {
			printf("Skipping assembly file %s\n", path.string().c_str());
		}
	}
	
	// Write out a lost+found file for types that can't be mapped to a specific
	// source file if we need it.
	if(needs_lost_and_found_file(high)) {
		write_lost_and_found_file(output_path/"lost+found.h", high);
	}
}

static std::vector<std::string> parse_sources_file(const fs::path& path) {
	std::optional<std::string> file = read_text_file(path);
	verify(file.has_value(), "Failed to open file '%s'", path.string().c_str());
	std::string_view input(*file);
	std::vector<std::string> sources;
	while(skip_whitespace(input), input.size() > 0) {
		sources.emplace_back(eat_identifier(input));
	}
	return sources;
}

static FunctionsFile parse_functions_file(const fs::path& path) {
	FunctionsFile result;
	
	std::optional<std::string> file = read_text_file(path);
	verify(file.has_value(), "Failed to open file '%s'", path.string().c_str());
	result.contents = std::move(*file);
	
	// Parse the file.
	std::span<char> input(result.contents);
	std::span<char>* function = nullptr;
	for(std::span<char> line = eat_line(input); line.data() != nullptr; line = eat_line(input)) {
		if(line.size() >= 9 && memcmp(line.data(), "@function", 9) == 0) {
			verify(line.size() > 10, "Bad @function directive in FUNCTIONS.txt file.");
			char* end = nullptr;
			s32 address = (s32) strtol(line.data() + 10, &end, 16);
			verify(end != line.data() + 10, "Bad @function directive in FUNCTIONS.txt file.");
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
	for(s64 i = 0; i < input.size(); i++) {
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
	std::optional<std::string> file = read_text_file(path);
	return !file || file->empty() || file->starts_with("// STATUS: NOT STARTED");
}

static void demangle_all(HighSymbolTable& high) {
	for(std::unique_ptr<ast::SourceFile>& source : high.source_files) {
		for(std::unique_ptr<ast::Node>& function : source->functions) {
			if(!function->name.empty()) {
				const char* demangled = cplus_demangle(function->name.c_str(), 0);
				if(demangled) {
					function->name = std::string(demangled);
					free((void*) demangled);
				}
			}
		}
		for(std::unique_ptr<ast::Node>& global : source->globals) {
			if(!global->name.empty()) {
				const char* demangled = cplus_demangle(global->name.c_str(), 0);
				if(demangled) {
					global->name = std::string(demangled);
					free((void*) demangled);
				}
			}
		}
	}
}

static void write_c_cpp_file(const fs::path& path, const fs::path& header_path, const HighSymbolTable& high, const std::vector<s32>& file_indices, const FunctionsFile& functions_file) {
	printf("Writing %s\n", path.string().c_str());
	FILE* out = open_file_w(path.c_str());
	verify(out, "Failed to open '%s' for writing.", path.string().c_str());
	fprintf(out, "// STATUS: NOT STARTED\n\n");
	
	// Configure printing.
	CppPrinter printer(out);
	printer.print_offsets_and_sizes = false;
	printer.print_storage_information = false;
	printer.print_variable_data = true;
	printer.omit_this_parameter = true;
	printer.substitute_parameter_lists = true;
	printer.function_bodies = &functions_file.functions;
	
	printer.include_directive(header_path.filename().string().c_str());
	
	// Print types.
	for(s32 file_index : file_indices) {
		const ast::SourceFile& file = *high.source_files[file_index].get();
		for(size_t i = 0; i < high.deduplicated_types.size(); i++) {
			ast::Node* node = high.deduplicated_types[i].get();
			assert(node);
			if(node->probably_defined_in_cpp_file && node->files.size() == 1 && node->files[0] == file_index) {
				printer.top_level_type(*node, i == high.deduplicated_types.size() - 1);
			}
		}
	}
	
	// Print globals.
	for(s32 file_index : file_indices) {
		const ast::SourceFile& file = *high.source_files[file_index].get();
		for(const std::unique_ptr<ast::Node>& node : file.globals) {
			VariableName dummy{};
			printer.ast_node(*node.get(), dummy, 0);
			fprintf(out, ";\n");
		}
	}
	
	// Print functions.
	for(s32 file_index : file_indices) {
		const ast::SourceFile& file = *high.source_files[file_index].get();
		for(const std::unique_ptr<ast::Node>& node : file.functions) {
			fprintf(out, "\n");
			VariableName dummy{};
			printer.ast_node(*node.get(), dummy, 0);
			fprintf(out, "\n");
		}
	}
	
	fclose(out);
}

static void write_h_file(const fs::path& path, std::string relative_path, const HighSymbolTable& high, const std::vector<s32>& file_indices) {
	printf("Writing %s\n", path.string().c_str());
	FILE* out = open_file_w(path.c_str());
	fprintf(out, "// STATUS: NOT STARTED\n\n");
	
	for(char& c : relative_path) {
		c = toupper(c);
		if(!isalnum(c)) {
			c = '_';
		}
	}
	fprintf(out, "#ifndef %s\n", relative_path.c_str());
	fprintf(out, "#define %s\n\n", relative_path.c_str());
	
	// Print types.
	for(s32 file_index : file_indices) {
		const ast::SourceFile& file = *high.source_files[file_index].get();
		CppPrinter printer(out);
		printer.print_offsets_and_sizes = false;
		printer.print_storage_information = false;
		printer.omit_this_parameter = true;
		printer.substitute_parameter_lists = true;
		for(size_t i = 0; i < high.deduplicated_types.size(); i++) {
			ast::Node* node = high.deduplicated_types[i].get();
			assert(node);
			if(!node->probably_defined_in_cpp_file && node->files.size() == 1 && node->files[0] == file_index) {
				printer.top_level_type(*node, i == high.deduplicated_types.size() - 1);
			}
		}
	}
	
	// Print globals.
	bool has_global = false;
	for(s32 file_index : file_indices) {
		const ast::SourceFile& file = *high.source_files[file_index].get();
		for(const std::unique_ptr<ast::Node>& node : file.globals) {
			VariableName dummy{};
			CppPrinter printer(out);
			printer.force_extern = true;
			printer.skip_statics = true;
			printer.print_storage_information = false;
			if(printer.ast_node(*node.get(), dummy, 0)) {
				fprintf(out, ";\n");
			}
			has_global = true;
		}
	}
	if(has_global) {
		fprintf(out, "\n");
	}
	
	// Print functions.
	for(s32 file_index : file_indices) {
		const ast::SourceFile& file = *high.source_files[file_index].get();
		for(const std::unique_ptr<ast::Node>& node : file.functions) {
			VariableName dummy{};
			CppPrinter printer(out);
			printer.skip_statics = true;
			printer.print_function_bodies = false;
			printer.print_storage_information = false;
			printer.omit_this_parameter = true;
			if(printer.ast_node(*node.get(), dummy, 0)) {
				fprintf(out, "\n");
			}
		}
	}
	
	fprintf(out, "\n#endif // %s\n", relative_path.c_str());
	fclose(out);
}

static bool needs_lost_and_found_file(const HighSymbolTable& high) {
	for(const std::unique_ptr<ast::Node>& node : high.deduplicated_types) {
		if(node->files.size() != 1) {
			return true;
		}
	}
	return false;
}

static void write_lost_and_found_file(const fs::path& path, const HighSymbolTable& high) {
	printf("Writing %s\n", path.string().c_str());
	FILE* out = open_file_w(path.c_str());
	CppPrinter printer(out);
	printer.print_offsets_and_sizes = false;
	printer.omit_this_parameter = true;
	printer.substitute_parameter_lists = true;
	s32 nodes_printed = 0;
	for(size_t i = 0; i < high.deduplicated_types.size(); i++) {
		ast::Node* node = high.deduplicated_types[i].get();
		assert(node);
		if(node->files.size() != 1) {
			if(printer.top_level_type(*node, i == high.deduplicated_types.size() - 1)) {
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
