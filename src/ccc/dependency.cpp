// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

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

static void map_types_to_files_based_on_reference_count_single_pass(SymbolDatabase& database, bool do_types);

void map_types_to_files_based_on_this_pointers(SymbolDatabase& database) {
	for(const Function& function : database.functions) {
		std::span<const ParameterVariable> parameter_variables = database.parameter_variables.span(function.parameter_variables());
		if(parameter_variables.empty()) {
			continue;
		}
		
		const ParameterVariable& parameter_variable = parameter_variables[0];
		const ast::Node* parameter_type = parameter_variable.type();
		if(!parameter_type) {
			continue;
		}
		
		// Check if the first argument is a this pointer.
		bool is_pointer = parameter_type->descriptor == ast::POINTER_OR_REFERENCE
			&& parameter_type->as<ast::PointerOrReference>().is_pointer;
		if(parameter_variable.name() != "this" || !is_pointer) {
			continue;
		}
		
		const ast::Node& class_node = *parameter_type->as<ast::PointerOrReference>().value_type.get();
		if(class_node.descriptor != ast::TYPE_NAME) {
			continue;
		}
		const ast::TypeName& type_name = class_node.as<ast::TypeName>();
		
		// Lookup the type pointed to by the this pointer.
		const DataTypeHandle class_type_handle = database.lookup_type(type_name, false);
		DataType* class_type = database.data_types.symbol_from_handle(class_type_handle);
		if(!class_type) {
			continue;
		}
		
		// Assume the type belongs to the file the function is from.
		class_type->files = {function.source_file()};
	}
}

void map_types_to_files_based_on_reference_count(SymbolDatabase& database) {
	map_types_to_files_based_on_reference_count_single_pass(database, false);
	map_types_to_files_based_on_reference_count_single_pass(database, true);
}

static void map_types_to_files_based_on_reference_count_single_pass(SymbolDatabase& database, bool do_types) {
	for(DataType& type : database.data_types) {
		if(type.files.size() == 1) {
			continue;
		}
		
		SourceFileHandle most_referenced_file;
		s32 most_references = 0;
		for(SourceFileHandle file_handle : type.files) {
			SourceFile* file = database.source_files.symbol_from_handle(file_handle);
			if(!file) {
				continue;
			}
			
			s32 reference_count = 0;
			auto count_references = [&](const ast::Node& node) {
				if(node.descriptor == ast::TYPE_NAME) {
					const ast::TypeName& type_name = node.as<ast::TypeName>();
					// Passsing false here to filter out forward declarations.
					DataTypeHandle type_nadle = database.lookup_type(type_name, false);
					if(type_nadle == type.handle()) {
						reference_count++;
					}
				}
				return ast::EXPLORE_CHILDREN;
			};
			
			if(do_types) {
				for(const DataType& data_type : database.data_types) {
					if(data_type.files.size() == 1 && data_type.files[0] == file_handle) {
						CCC_ASSERT(data_type.type());
						ast::for_each_node(*data_type.type(), ast::PREORDER_TRAVERSAL, count_references);
					}
				}
			} else {
				for(const Function& function : database.functions.span(file->functions())) {
					if(function.storage_class != ast::SC_STATIC) {
						if(function.type()) {
							ast::for_each_node(*function.type(), ast::PREORDER_TRAVERSAL, count_references);
						}
						for(const ParameterVariable& parameter_variable : database.parameter_variables.span(function.parameter_variables())) {
							if(parameter_variable.type()) {
								ast::for_each_node(*parameter_variable.type(), ast::PREORDER_TRAVERSAL, count_references);
							}
						}
					}
				}
				for(const GlobalVariable& global_variable : database.global_variables.span(file->globals_variables())) {
					if(global_variable.storage_class != ast::SC_STATIC) {
						if(global_variable.type()) {
							ast::for_each_node(*global_variable.type(), ast::PREORDER_TRAVERSAL, count_references);
						}
					}
				}
			}
			if(reference_count > most_references) {
				most_referenced_file = file_handle;
				most_references = reference_count;
			}
		}
		if(most_referenced_file.valid()) {
			type.files = {most_referenced_file};
		}
	}
}

TypeDependencyAdjacencyList build_type_dependency_graph(const SymbolDatabase& database) {
	TypeDependencyAdjacencyList graph;
	for(const DataType& data_type : database.data_types) {
		std::set<DataTypeHandle> dependencies;
		CCC_ASSERT(data_type.type());
		ast::for_each_node(*data_type.type(), ast::PREORDER_TRAVERSAL, [&](const ast::Node& node) {
			if(node.descriptor == ast::TYPE_NAME) {
				const ast::TypeName& type_name = node.as<ast::TypeName>();
				if(type_name.source == ast::TypeNameSource::REFERENCE) {
					DataTypeHandle dependency_handle = database.lookup_type(type_name, false);
					if(dependency_handle.valid()) {
						dependencies.emplace(dependency_handle);
					}
				}
			}
			return ast::EXPLORE_CHILDREN;
		});
		graph.emplace_back(data_type.handle(), dependencies);
	}
	return graph;
}

void print_type_dependency_graph(FILE* out, const SymbolDatabase& database, const TypeDependencyAdjacencyList& graph) {
	GraphPrinter printer(out);
	printer.begin_graph("type_dependencies", DIRECTED);
	for(const DataType& data_type : database.data_types) {
		CCC_ASSERT(data_type.type());
		if(!data_type.name().empty() && data_type.type()->descriptor != ast::BUILTIN && data_type.name() != "void") {
			printer.node(data_type.name().c_str(), data_type.name().c_str());
		}
	}
	for(const auto& [handle, dependencies] : graph) {
		const DataType* out_node = database.data_types.symbol_from_handle(handle);
		if(!out_node) {
			continue;
		}
		
		CCC_ASSERT(out_node->type());
		if(!out_node->name().empty() && out_node->type()->descriptor != ast::BUILTIN && out_node->name() != "void") {
			for(DataTypeHandle in : dependencies) {
				const DataType* in_node = database.data_types.symbol_from_handle(in);
				CCC_ASSERT(in_node->type());
				if(!in_node->name().empty() && in_node->type()->descriptor != ast::BUILTIN && in_node->name() != "void") {
					printer.edge(out_node->name().c_str(), in_node->name().c_str());
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
