#include "ccc/ccc.h"

using namespace ccc;

enum OutputMode {
	OUTMODE_TYPES,
	OUTMODE_FILES,
	OUTMODE_HELP
};

static void print_type_dependency_graph(FILE* out, const HighSymbolTable& high, const TypeDependencyAdjacencyList& graph);
static void print_file_dependency_graph(FILE* out, const HighSymbolTable& high, const FileDependencyAdjacencyList& graph);

enum GraphType {
	UNDIRECTED,
	DIRECTED
};

struct GraphPrinter {
	FILE* out;
	s32 indent_level = 0;
	bool no_lines_printed = true;
	
	GraphPrinter(FILE* o) : out(o) {}
	
	void begin_graph(const char* name, GraphType type);
	void end_graph();
	
	void node(const char* name, const char* label);
	void edge(const char* out_name, const char* in_name);
	
	void new_line();
};

static void print_help(FILE* out);

static const cli::OptionsInfo DEPGRAPH_OPTIONS = {
	{
		{OUTMODE_TYPES, cli::MF_REQUIRE_INPUT_PATH, "types"},
		{OUTMODE_FILES, cli::MF_REQUIRE_INPUT_PATH, "files"},
		{OUTMODE_HELP, cli::MF_NO_FLAGS, "help", "--help", "-h"}
	},
	{
	}
};

int main(int argc, char** argv) {
	cli::Options options = cli::parse_arguments(argc, argv, DEPGRAPH_OPTIONS);
	FILE* out = cli::get_output_file(options);
	switch(options.mode) {
		case OUTMODE_TYPES: {
			Module mod;
			fprintf(stderr, "Reading symbol table...\n");
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			fprintf(stderr, "Analysing symbol table...\n");
			HighSymbolTable high = analyse(symbol_table, DEDUPLICATE_TYPES | STRIP_GENERATED_FUNCTIONS);
			fprintf(stderr, "Building type dependency graph...\n");
			TypeDependencyAdjacencyList graph = build_type_dependency_graph(high);
			fprintf(stderr, "Printing type dependency graph...\n");
			print_type_dependency_graph(out, high, graph);
			break;
		}
		case OUTMODE_FILES: {
			Module mod;
			fprintf(stderr, "Reading symbol table...\n");
			mdebug::SymbolTable symbol_table = read_symbol_table(mod, options.input_file);
			fprintf(stderr, "Analysing symbol table...\n");
			HighSymbolTable high = analyse(symbol_table, DEDUPLICATE_TYPES | STRIP_GENERATED_FUNCTIONS);
			fprintf(stderr, "Building file dependency graph...\n");
			map_types_to_files_based_on_this_pointers(high);
			map_types_to_files_based_on_reference_count(high);
			TypeDependencyAdjacencyList type_graph = build_type_dependency_graph(high);
			FileDependencyAdjacencyList file_graph = build_file_dependency_graph(high, type_graph);
			fprintf(stderr, "Printing file dependency graph...\n");
			print_file_dependency_graph(out, high, file_graph);
			break;
		}
		case OUTMODE_HELP: {
			print_help(out);
			return 1;
		}
	}
	return 0;
}

static void print_type_dependency_graph(FILE* out, const HighSymbolTable& high, const TypeDependencyAdjacencyList& graph) {
	GraphPrinter printer(out);
	printer.begin_graph("type_dependencies", DIRECTED);
	for(size_t i = 0; i < high.deduplicated_types.size(); i++) {
		const std::unique_ptr<ast::Node>& node = high.deduplicated_types[i];
		if(!node->name.empty() && node->descriptor != ast::BUILTIN && node->name != "void") {
			printer.node(node->name.c_str(), node->name.c_str());
		}
	}
	for(size_t i = 0; i < high.deduplicated_types.size(); i++) {
		const std::unique_ptr<ast::Node>& out_node = high.deduplicated_types[i];
		if(!out_node->name.empty() && out_node->descriptor != ast::BUILTIN && out_node->name != "void") {
			for(TypeIndex in : graph.at(i)) {
				const std::unique_ptr<ast::Node>& in_node = high.deduplicated_types[in.index];
				if(!in_node->name.empty() && in_node->descriptor != ast::BUILTIN && in_node->name != "void") {
					printer.edge(out_node->name.c_str(), in_node->name.c_str());
				}
			}
		}
	}
	printer.end_graph();
}

static void print_file_dependency_graph(FILE* out, const HighSymbolTable& high, const FileDependencyAdjacencyList& graph) {
	GraphPrinter printer(out);
	printer.begin_graph("file_dependencies", DIRECTED);
	for(size_t i = 0; i < high.source_files.size(); i++) {
		const std::unique_ptr<ast::SourceFile>& file = high.source_files[i];
		std::string name = "f" + std::to_string(i);
		printer.node(name.c_str(), extract_file_name(file->full_path).c_str());
	}
	for(size_t i = 0; i < high.source_files.size(); i++) {
		const std::unique_ptr<ast::SourceFile>& out_node = high.source_files[i];
		std::string out_name = "f" + std::to_string(i);
		for(FileIndex in : graph.at(i)) {
			const std::unique_ptr<ast::SourceFile>& in_node = high.source_files[in.index];
			std::string in_name = "f" + std::to_string(in.index);
			printer.edge(out_name.c_str(), in_name.c_str());
		}
	}
	printer.end_graph();
}

void GraphPrinter::begin_graph(const char* name, GraphType type) {
	new_line();
	fprintf(out, "%s %s {", type == DIRECTED ? "digraph" : "graph", name);
	indent_level++;
}

void GraphPrinter::end_graph() {
	indent_level--;
	new_line();
	fprintf(out, "}");
}

void GraphPrinter::node(const char* name, const char* label) {
	new_line();
	fprintf(out, "%s [label=\"%s\"]", name, label);
}

void GraphPrinter::edge(const char* out_name, const char* in_name) {
	new_line();
	fprintf(out, "%s -> %s;", out_name, in_name);
}

void GraphPrinter::new_line() {
	if(!no_lines_printed) {
		fprintf(out, "\n");
	}
	for(s32 i = 0; i < indent_level; i++) {
		fprintf(out, "\t");
	}
	no_lines_printed = false;
}

const char* git_tag();

static void print_help(FILE* out) {
	const char* tag = git_tag();
	fprintf(out, "depgraph %s -- https://github.com/chaoticgd/ccc\n",
		(strlen(tag) > 0) ? tag : "development version");
	fprintf(out, "\n");
	fprintf(out, "  types <input path>\n");
}
