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
	
	void node(const char* name, const char* label);
	void edge(const char* out_name, const char* in_name);
	
	void new_line();
};

static void map_types_to_files_based_on_reference_count_single_pass(HighSymbolTable& high, bool do_types);

void map_types_to_files_based_on_this_pointers(HighSymbolTable& high) {
	// Iterate over all functions in all files.
	for(size_t i = 0; i < high.source_files.size(); i++) {
		const std::unique_ptr<ast::SourceFile>& file = high.source_files[i];
		for(const std::unique_ptr<ast::Node>& node : file->functions) {
			const ast::FunctionDefinition& function = node->as<ast::FunctionDefinition>();
			const ast::FunctionType& type = function.type->as<ast::FunctionType>();
			if(type.parameters.has_value() && !type.parameters->empty()) {
				ast::Variable& parameter = (*type.parameters)[0]->as<ast::Variable>();
				ast::Node& parameter_type = *parameter.type.get();
				// Check if the first argument is a this pointer.
				if(parameter.name == "this" && parameter_type.descriptor == ast::POINTER) {
					ast::Node& class_node = *parameter_type.as<ast::Pointer>().value_type.get();
					if(class_node.descriptor == ast::TYPE_NAME) {
						ast::TypeName& type_name = class_node.as<ast::TypeName>();
						// Lookup the type pointed to by the this pointer.
						s32 class_type_index = lookup_type(type_name, high, nullptr);
						if(class_type_index > -1) {
							// Assume the type belongs to the file the function is from.
							high.deduplicated_types.at(class_type_index)->files = {(s32) i};
						}
					}
				}
			}
		}
	}
}

void map_types_to_files_based_on_reference_count(HighSymbolTable& high) {
	map_types_to_files_based_on_reference_count_single_pass(high, false);
	map_types_to_files_based_on_reference_count_single_pass(high, true);
}

static void map_types_to_files_based_on_reference_count_single_pass(HighSymbolTable& high, bool do_types) {
	for(size_t i = 0; i < high.deduplicated_types.size(); i++) {
		std::unique_ptr<ast::Node>& type = high.deduplicated_types[i];
		if(type->files.size() == 1) {
			continue;
		}
		
		FileIndex most_referenced_file = {-1};
		s32 most_references = 0;
		for(s32 file : type->files) {
			s32 reference_count = 0;
			auto count_references = [&](const ast::Node& node) {
				switch(node.descriptor) {
					case ast::FUNCTION_DEFINITION: {
						const ast::FunctionDefinition& function = node.as<ast::FunctionDefinition>();
						if(function.storage_class == ast::SC_STATIC) {
							return ast::DONT_EXPLORE_CHILDREN;
						}
						break;
					}
					case ast::VARIABLE: {
						const ast::Variable& variable = node.as<ast::Variable>();
						if(variable.variable_class == ast::VariableClass::LOCAL) {
							return ast::DONT_EXPLORE_CHILDREN;
						}
						break;
					}
					case ast::TYPE_NAME: {
						const ast::TypeName& type_name = node.as<ast::TypeName>();
						s32 type_index = lookup_type(type_name, high, nullptr);
						if(type_index == (s32) i) {
							reference_count++;
						}
						break;
					}
					default: {}
				}
				return ast::EXPLORE_CHILDREN;
			};
			if(do_types) {
				for(std::unique_ptr<ast::Node>& node : high.deduplicated_types) {
					if(node->files.size() == 1 && node->files[0] == file) {
						ast::for_each_node(*node.get(), ast::PREORDER_TRAVERSAL, count_references);
					}
				}
			} else {
				for(std::unique_ptr<ast::Node>& node : high.source_files[file]->functions) {
					if(node->storage_class != ast::SC_STATIC) {
						ast::for_each_node(*node.get(), ast::PREORDER_TRAVERSAL, count_references);
					}
				}
				for(std::unique_ptr<ast::Node>& node : high.source_files[file]->globals) {
					if(node->storage_class != ast::SC_STATIC) {
						ast::for_each_node(*node.get(), ast::PREORDER_TRAVERSAL, count_references);
					}
				}
			}
			if(reference_count > most_references) {
				most_referenced_file = file;
				most_references = reference_count;
			}
		}
		if(most_referenced_file.index > -1) {
			type->files = {most_referenced_file};
		}
	}
}

TypeDependencyAdjacencyList build_type_dependency_graph(const HighSymbolTable& high) {
	TypeDependencyAdjacencyList graph;
	for(size_t i = 0; i < high.deduplicated_types.size(); i++) {
		const std::unique_ptr<ast::Node>& type = high.deduplicated_types[i];
		std::set<TypeIndex>& dependencies = graph.emplace_back();
		ast::for_each_node(*type.get(), ast::PREORDER_TRAVERSAL, [&](const ast::Node& node) {
			if(node.descriptor == ast::TYPE_NAME) {
				const ast::TypeName& type_name = node.as<ast::TypeName>();
				if(type_name.source == ast::TypeNameSource::REFERENCE) {
					// Pass nullptr to filter out forward declarations.
					s32 dependency_index = lookup_type(type_name, high, nullptr);
					if(dependency_index > -1) {
						dependencies.emplace(dependency_index);
					}
				}
			}
			return ast::EXPLORE_CHILDREN;
		});
	}
	return graph;
}

// This currently doesn't work very well, so is not user accessible. There are
// the remains of some more experiments like this in the git history.
FileDependencyAdjacencyList build_file_dependency_graph(const HighSymbolTable& high, const TypeDependencyAdjacencyList& type_graph) {
	// Assume that if a type A depends on a type B then the file containing type
	// A depends on the file containing type B. Note that this creates a fairly
	// densely connected graph, which is not what we want in this case.
	FileDependencyAdjacencyList dense;
	dense.resize(high.source_files.size());
	for(TypeIndex out = 0; out.index < (s32) type_graph.size(); out.index++) {
		if(high.deduplicated_types[out]->files.size() == 1) {
			FileIndex out_file = high.deduplicated_types[out]->files[0];
			for(TypeIndex in : type_graph[out]) {
				// Only add a dependency if we think there is a good probability
				// that we know what file it comes from, also exclude builtins
				// since those tend to produce bad results.
				const ast::Node& in_type = *high.deduplicated_types[in].get();
				if(in_type.files.size() == 1 && in_type.descriptor != ast::BUILTIN && in_type.name != "void") {
					FileIndex in_file = high.deduplicated_types[in]->files[0];
					if(in_file != out_file) {
						dense[out_file].emplace(in_file);
					}
				}
			}
		}
	}
	
	// Trim the graph to remove bad edges. This is done by running depth first
	// search over the graph for each node and only keeping edges that produce a
	// depth value indicating they have not been visited or that they have been
	// visited directly via one of the outgoing edges of the current node e.g.
	// A->B->C would be kept and A->C would be discarded.
	FileDependencyAdjacencyList sparse;
	std::vector<FileIndex> dfs_stack;
	dfs_stack.reserve(high.source_files.size());
	std::vector<s32> depths(high.source_files.size(), -1);
	for(s32 i = 0; i < (s32) high.source_files.size(); i++) {
		dfs_stack.clear();
		dfs_stack.emplace_back(i);
		
		s32 current_depth = 0;
		for(s32& depth : depths) depth = -1;
		
		// Run depth first search.
		while(!dfs_stack.empty()) {
			FileIndex current_node = dfs_stack.back();
			if(depths[current_node] == -1) {
				depths[current_node] = current_depth;
			}
			bool backtrack = true;
			for(FileIndex possible_next_node : dense[current_node]) {
				if(depths[possible_next_node] == -1) {
					backtrack = false;
					dfs_stack.emplace_back(possible_next_node);
					current_depth++;
					break;
				}
			}
			if(backtrack) {
				dfs_stack.pop_back();
				current_depth--;
			}
		}
		
		// Filter out unnecessary edges.
		std::set<FileIndex>& dest = sparse.emplace_back();
		for(const FileIndex& in_node : dense[i]) {
			if(depths[in_node] < 2) {
				dest.emplace(in_node);
			}
		}
	}
	
	return sparse;
}

void print_type_dependency_graph(FILE* out, const HighSymbolTable& high, const TypeDependencyAdjacencyList& graph) {
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

void print_file_dependency_graph(FILE* out, const HighSymbolTable& high, const FileDependencyAdjacencyList& graph) {
	GraphPrinter printer(out);
	printer.begin_graph("file_dependencies", DIRECTED);
	for(size_t i = 0; i < high.source_files.size(); i++) {
		const std::unique_ptr<ast::SourceFile>& file = high.source_files[i];
		std::string name = "f" + std::to_string(i);
		printer.node(name.c_str(), extract_file_name(file->full_path).c_str());
	}
	for(size_t i = 0; i < high.source_files.size(); i++) {
		std::string out_name = "f" + std::to_string(i);
		for(FileIndex in : graph.at(i)) {
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

}
