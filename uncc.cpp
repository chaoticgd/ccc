#include "ccc/ccc.h"
#define HAVE_DECL_BASENAME 1
#include "demanglegnu/demangle.h"

using namespace ccc;

static std::vector<std::string> parse_sources_list(const fs::path& path);
static std::string eat_identifier(std::string_view& input);
static void skip_whitespace(std::string_view& input);
static bool should_overwrite_file(const fs::path& path);
static void demangle_all(AnalysisResults& program);
static void write_c_cpp_file(const fs::path& path, const std::vector<ast::SourceFile*>& sources);
static void write_h_file(const fs::path& path, std::string relative_path, const std::vector<ast::SourceFile*>& sources);

int main(int argc, char** argv) {
	if(argc != 3) {
		fprintf(stderr, "usage: %s <input elf> <output directory>\n", (argc > 0) ? argv[0] : "uncc");
		return 1;
	}
	
	fs::path elf_path = std::string(argv[1]);
	fs::path output_path = std::string(argv[2]);
	
	fs::path sources_list_path;
	fs::path output_directory;
	if(fs::is_directory(output_path)) {
		sources_list_path = output_path/"SOURCES.txt";
		output_directory = output_path;
	} else if(fs::is_regular_file(output_path)) {
		sources_list_path = output_path;
		output_directory = output_path.parent_path();
	}
	
	std::vector<std::string> source_paths = parse_sources_list(sources_list_path);
	
	Module elf = loaders::read_elf_file(elf_path);
	mdebug::SymbolTable symbol_table = read_symbol_table({&elf});
	AnalysisResults program = analyse(symbol_table, NO_ANALYSIS_FLAGS);
	verify(program.source_files.size() == source_paths.size(), "Source file count mismatch!");
	
	demangle_all(program);
	
	// Group duplicate source file entries.
	std::map<std::string, std::vector<ast::SourceFile*>> path_to_source_file;
	for(size_t i = 0; i < program.source_files.size(); i++) {
		path_to_source_file[source_paths[i]].emplace_back(program.source_files[i].get());
	}
	
	for(auto& [relative_path, sources] : path_to_source_file) {
		fs::path path = output_path/fs::path(relative_path);
		fs::create_directories(path.parent_path());
		if(path.extension() == ".c" || path.extension() == ".cpp") {
			// Write .c/.cpp file.
			if(should_overwrite_file(path)) {
				write_c_cpp_file(path, sources);
			} else {
				printf(ANSI_COLOUR_GRAY "Skipping " ANSI_COLOUR_OFF " %s\n", path.string().c_str());
			}
			// Write .h file.
			fs::path header_path = path.replace_extension(".h");
			if(should_overwrite_file(header_path)) {
				fs::path relative_header_path = fs::path(relative_path).replace_extension(".h");
				write_h_file(header_path, relative_header_path.string(), sources);
			} else {
				printf(ANSI_COLOUR_GRAY "Skipping " ANSI_COLOUR_OFF " %s\n", header_path.string().c_str());
			}
		} else {
			printf("Skipping assembly file %s\n", path.string().c_str());
		}
	}
}

static std::vector<std::string> parse_sources_list(const fs::path& path) {
	std::optional<std::string> file = read_text_file(path);
	verify(file.has_value(), "Failed to open file '%s'", path.string().c_str());
	std::string_view input(*file);
	std::vector<std::string> sources;
	while(skip_whitespace(input), input.size() > 0) {
		sources.emplace_back(eat_identifier(input));
	}
	return sources;
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

static void demangle_all(AnalysisResults& program) {
	for(std::unique_ptr<ast::SourceFile>& source : program.source_files) {
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

static void write_c_cpp_file(const fs::path& path, const std::vector<ast::SourceFile*>& sources) {
	printf("Writing %s\n", path.string().c_str());
	FILE* out = open_file_w(path.string().c_str());
	verify(out, "Failed to open '%s' for writing.");
	fprintf(out, "// STATUS: NOT STARTED\n\n");
	for(const ast::SourceFile* source : sources) {
		for(const std::unique_ptr<ast::Node>& node : source->globals) {
			VariableName dummy{};
			PrintCppConfig config;
			config.print_storage_information = false;
			print_cpp_ast_node(out, *node.get(), dummy, 0, config);
			fprintf(out, ";\n");
		}
	}
	for(const ast::SourceFile* source : sources) {
		for(const std::unique_ptr<ast::Node>& node : source->functions) {
			fprintf(out, "\n");
			VariableName dummy{};
			PrintCppConfig config;
			config.print_storage_information = false;
			print_cpp_ast_node(out, *node.get(), dummy, 0, config);
			fprintf(out, "\n");
		}
	}
	fclose(out);
}

static void write_h_file(const fs::path& path, std::string relative_path, const std::vector<ast::SourceFile*>& sources) {
	printf("Writing %s\n", path.string().c_str());
	FILE* out = open_file_w(path.string().c_str());
	fprintf(out, "// STATUS: NOT STARTED\n\n");
	
	for(char& c : relative_path) {
		c = toupper(c);
		if(!isalnum(c)) {
			c = '_';
		}
	}
	fprintf(out, "#ifndef %s\n", relative_path.c_str());
	fprintf(out, "#define %s\n\n", relative_path.c_str());
	
	bool has_global = false;
	for(const ast::SourceFile* source : sources) {
		for(const std::unique_ptr<ast::Node>& node : source->globals) {
			VariableName dummy{};
			PrintCppConfig config;
			config.force_extern = true;
			config.skip_statics = true;
			config.print_storage_information = false;
			if(print_cpp_ast_node(out, *node.get(), dummy, 0, config)) {
				fprintf(out, ";\n");
			}
			has_global = true;
		}
	}
	if(has_global) {
		fprintf(out, "\n");
	}
	for(const ast::SourceFile* source : sources) {
		for(const std::unique_ptr<ast::Node>& node : source->functions) {
			VariableName dummy{};
			PrintCppConfig config;
			config.skip_statics = true;
			config.print_function_bodies = false;
			config.print_storage_information = false;
			if(print_cpp_ast_node(out, *node.get(), dummy, 0, config)) {
				fprintf(out, "\n");
			}
		}
	}
	fprintf(out, "\n#endif // %s\n", relative_path.c_str());
	fclose(out);
}
