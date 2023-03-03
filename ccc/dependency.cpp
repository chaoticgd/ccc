#include "dependency.h"

namespace ccc {

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
						ast::TypeName& class_type = class_node.as<ast::TypeName>();
						if(class_type.referenced_stabs_type_number > -1) {
							const ast::SourceFile& foreign_file = *high.source_files.at(class_type.referenced_file_index).get();
							// Lookup the type pointed to by the this pointer.
							auto type_index = foreign_file.stabs_type_number_to_deduplicated_type_index.find(class_type.referenced_stabs_type_number);
							if(type_index != foreign_file.stabs_type_number_to_deduplicated_type_index.end()
									&& high.deduplicated_types[type_index->second]->files.size() != 1) {
								// Assume the type belongs to the file the function is from.
								high.deduplicated_types[type_index->second]->files = {(s32) i};
							}
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
				if(node.descriptor == ast::VARIABLE && node.as<ast::Variable>().variable_class == ast::VariableClass::LOCAL) {
					return ast::DONT_EXPLORE_CHILDREN;
				} else if(node.descriptor == ast::TYPE_NAME) {
					const ast::TypeName& type_name = node.as<ast::TypeName>();
					if(type_name.referenced_file_index > -1 && type_name.referenced_stabs_type_number > -1) {
						const std::unique_ptr<ast::SourceFile>& source_file = high.source_files.at(type_name.referenced_file_index);
						auto type_index = source_file->stabs_type_number_to_deduplicated_type_index.find(type_name.referenced_stabs_type_number);
						if(type_index != source_file->stabs_type_number_to_deduplicated_type_index.end()
							&& type_index->second == i) {
							reference_count++;
						}
					}
				}
				return ast::EXPLORE_CHILDREN;
			};
			if(do_types) {
				for(std::unique_ptr<ast::Node>& node : high.deduplicated_types) {
					if(node->files.size() == 1 && node->files[0] == file) {
						ast::for_each_node(*node.get(), count_references);
					}
				}
			} else {
				for(std::unique_ptr<ast::Node>& node : high.source_files[file]->functions) {
					if(node->storage_class != ast::SC_STATIC) {
						ast::for_each_node(*node.get(), count_references);
					}
				}
				for(std::unique_ptr<ast::Node>& node : high.source_files[file]->globals) {
					if(node->storage_class != ast::SC_STATIC) {
						ast::for_each_node(*node.get(), count_references);
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
	for(const std::unique_ptr<ast::Node>& type : high.deduplicated_types) {
		std::set<TypeIndex>& dependencies = graph.emplace_back();
		ast::for_each_node(*type.get(), [&](const ast::Node& node) {
			if(node.descriptor == ast::TYPE_NAME) {
				const ast::TypeName& type_name = node.as<ast::TypeName>();
				// Filter out forward declarations.
				if(type_name.source == ast::TypeNameSource::REFERENCE
						&& type_name.referenced_file_index > -1
						&& type_name.referenced_stabs_type_number > -1) {
					const ast::SourceFile& source_file = *high.source_files[type_name.referenced_file_index].get();
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

}
