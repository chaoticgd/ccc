#include "dependency.h"

namespace ccc {

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
	
	void node(const char* name);
	void edge(const char* out_name, const char* in_name);
	
	void new_line();
};

TypeDependencyAdjacencyList build_type_dependency_graph(const AnalysisResults& program) {
	TypeDependencyAdjacencyList graph;
	for(const std::unique_ptr<ast::Node>& type : program.deduplicated_types) {
		std::set<TypeIndex>& dependencies = graph.emplace_back();
		ast::for_each_node(*type.get(), [&](const ast::Node& node) {
			if(node.descriptor == ast::TYPE_NAME) {
				const ast::TypeName& type_name = node.as<ast::TypeName>();
				// Filter out forward declarations.
				if(type_name.source == ast::TypeNameSource::REFERENCE
						&& type_name.referenced_file_index > -1
						&& type_name.referenced_stabs_type_number > -1) {
					const ast::SourceFile& source_file = *program.source_files[type_name.referenced_file_index].get();
					auto type_index = source_file.stabs_type_number_to_deduplicated_type_index.find(type_name.referenced_stabs_type_number);
					if(type_index != source_file.stabs_type_number_to_deduplicated_type_index.end()) {
						dependencies.emplace(type_index->second);
					}
				}
			}
			return ast::EXPLORE_CHILDREN;
		});
	}
	return graph;
}

void print_type_dependency_graph(FILE* out, const AnalysisResults& program, const TypeDependencyAdjacencyList& graph) {
	GraphPrinter printer(out);
	printer.begin_graph("D", DIRECTED);
	for(size_t i = 0; i < program.deduplicated_types.size(); i++) {
		const std::unique_ptr<ast::Node>& type = program.deduplicated_types[i];
		if(!type->name.empty()) {
			printer.node(type->name.c_str());
		}
	}
	for(size_t i = 0; i < program.deduplicated_types.size(); i++) {
		const std::unique_ptr<ast::Node>& out_node = program.deduplicated_types[i];
		if(!out_node->name.empty()) {
			for(TypeIndex in : graph.at(i)) {
				const std::unique_ptr<ast::Node>& in_node = program.deduplicated_types[in.index];
				if(!in_node->name.empty()) {
					printer.edge(out_node->name.c_str(), in_node->name.c_str());
				}
			}
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

void GraphPrinter::node(const char* name) {
	new_line();
	fprintf(out, "%s [label=\"%s\"]", name, name);
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

}
