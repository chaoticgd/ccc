#include "dependency.h"

namespace ccc {

// A group is a set of files with identical type information.
struct GroupIndex {
	s32 index;
	GroupIndex(s32 i) : index(i) {}
	operator s32() const { return index; }
	friend auto operator<=>(const GroupIndex& lhs, const GroupIndex& rhs) = default;
};

using GroupDependencyAdjacencyList = std::vector<std::set<GroupIndex>>;

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

FileDependencyAdjacencyList build_file_dependency_graph(const HighSymbolTable& high) {
	// Build sets of type indices for each file.
	std::vector<std::set<TypeIndex>> type_indices_by_file;
	for(const std::unique_ptr<ast::SourceFile>& file : high.source_files) {
		std::set<TypeIndex>& type_indices = type_indices_by_file.emplace_back();
		for(auto [stabs_type_number, type_index] : file->stabs_type_number_to_deduplicated_type_index) {
			type_indices.emplace(type_index);
		}
	}
	
	// Sort a list of file indices such that we can iterate over files with
	// identical type information consecutively.
	std::vector<FileIndex> sorted_files(high.source_files.size(), 0);
	for(s32 i = 0; i < (s32) high.source_files.size(); i++) {
		sorted_files[i] = i;
	}
	std::sort(BEGIN_END(sorted_files), [&](FileIndex lhs, FileIndex rhs)
		{ return type_indices_by_file[lhs] < type_indices_by_file[rhs]; });
	
	static const std::set<std::string> standard_library_types = {
		"__gnuc_va_list",
		"tm",
		"_glue",
		"_Bigint",
		"_atexit",
		"__sbuf",
		"_fpos_t",
		"__sFILE",
		"_reent"
	};
	
	// Exclude files with no user-defined types.
	sorted_files.erase(std::remove_if(BEGIN_END(sorted_files), [&](FileIndex file) {
		for(auto [stabs_type_number, type_index] : high.source_files[file]->stabs_type_number_to_deduplicated_type_index) {
			const ast::Node& type = *high.deduplicated_types[type_index].get();
			if(type.descriptor != ast::BUILTIN && !standard_library_types.contains(type.name)) {
				return false;
			}
		}
		return true;
	}));
	
	// Sort the files into a set of groups where each file within a given group
	// has identical type information.
	std::vector<std::vector<FileIndex>> groups;
	if(!high.source_files.empty()) {
		std::vector<FileIndex>& first_group = groups.emplace_back();
		first_group.emplace_back(sorted_files[0]);
	}
	for(s32 i = 1; i < (s32) high.source_files.size(); i++) {
		std::set<TypeIndex>& last = type_indices_by_file[sorted_files[i - 1]];
		std::set<TypeIndex>& current = type_indices_by_file[sorted_files[i]];
		if(current != last) {
			groups.emplace_back();
		}
		groups.back().emplace_back(sorted_files[i]);
	}
	
	// Build a dependency graph by assuming that if the types in file A are a
	// subset of the types in file B that B depends on A. This creates an overly
	// dense graph where if A->B->C then A->C (which is not really desirable).
	GroupDependencyAdjacencyList dense;
	dense.reserve(high.source_files.size());
	for(size_t i = 0; i < groups.size(); i++) {
		std::set<GroupIndex>& dependencies = dense.emplace_back();
		for(size_t j = 0; j < groups.size(); j++) {
			if(i != j && std::includes(BEGIN_END(type_indices_by_file[groups[i][0]]), BEGIN_END(type_indices_by_file[groups[j][0]]))) {
				dependencies.emplace(j);
			}
		}
	}
	
	// Trim the graph to produce a tree. This is done by running depth first
	// search over the graph for each node and only keeping edges that produce a
	// depth value indicating they have not been visited or that they have been
	// visited directly via one of the outgoing edges of the current node e.g.
	// A->B->C would be kept and A->C would be discarded.
	GroupDependencyAdjacencyList sparse;
	std::vector<GroupIndex> dfs_stack;
	dfs_stack.reserve(groups.size());
	std::vector<s32> depths(groups.size(), -1);
	for(s32 i = 0; i < (s32) groups.size(); i++) {
		dfs_stack.clear();
		dfs_stack.emplace_back(i);
		
		s32 current_depth = 0;
		for(s32& depth : depths) depth = -1;
		
		// Run depth first search.
		while(!dfs_stack.empty()) {
			GroupIndex current_node = dfs_stack.back();
			if(depths[current_node] == -1) {
				depths[current_node] = current_depth;
			}
			bool backtrack = true;
			for(GroupIndex possible_next_node : dense[current_node]) {
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
		std::set<GroupIndex>& dest = sparse.emplace_back();
		for(const GroupIndex& in_node : dense[i]) {
			if(depths[in_node] < 2) {
				dest.emplace(in_node);
			}
		}
	}
	
	// Convert the group dependency graph into a file dependency graph. Assume
	// that all the types in a group are defined in a single file. Guess which
	// file this is by counting how many references to these types there are in
	// each of the files.
	std::vector<FileIndex> primary_file_by_group;
	primary_file_by_group.reserve(groups.size());
	for(s32 i = 0; i < (s32) groups.size(); i++) {
		FileIndex most_referenced_file = {-1};
		s32 most_references = -1;
		if(groups[i].size() > 1) {
			for(FileIndex file : groups[i]) {
				s32 reference_count = 0;
				auto count_references = [&](const ast::Node& node) {
					if(node.descriptor == ast::TYPE_NAME) {
						const ast::TypeName& type_name = node.as<ast::TypeName>();
						if(type_name.referenced_file_index > -1 && type_name.referenced_stabs_type_number > -1) {
							const std::unique_ptr<ast::SourceFile>& source_file = high.source_files.at(type_name.referenced_file_index);
							auto type_index = source_file->stabs_type_number_to_deduplicated_type_index.find(type_name.referenced_stabs_type_number);
							if(type_index != source_file->stabs_type_number_to_deduplicated_type_index.end()) {
								const std::unique_ptr<ast::Node>& type = high.deduplicated_types[type_index->second];
								for(s32 f : source_file->files) {
									if(f == file.index) {
										reference_count++;
										break;
									}
								}
							}
						}
					}
					return ast::EXPLORE_CHILDREN;
				};
				for(std::unique_ptr<ast::Node>& node : high.source_files[file]->functions) {
					ast::for_each_node(*node.get(), count_references);
				}
				for(std::unique_ptr<ast::Node>& node : high.source_files[file]->globals) {
					ast::for_each_node(*node.get(), count_references);
				}
				if(reference_count > most_references) {
					most_referenced_file = file;
					most_references = reference_count;
				}
			}
		}
		if(most_referenced_file.index == -1) {
			most_referenced_file = groups[i][0];
		}
		primary_file_by_group.emplace_back(most_referenced_file);
	}
	
	FileDependencyAdjacencyList graph;
	graph.resize(high.source_files.size());
	for(s32 i = 0; i < (s32) groups.size(); i++) {
		// Construct the edges between primary files.
		FileIndex out_file = primary_file_by_group[i];
		std::set<FileIndex>& dependencies = graph[out_file];
		for(GroupIndex in_group : sparse[i]) {
			dependencies.emplace(primary_file_by_group[in_group]);
		}
		
		// Add the rest of the edges.
		for(FileIndex file : groups[i]) {
			if(file != out_file) {
				graph[file].emplace(out_file);
			}
		}
	}
	
	return graph;
}

void map_types_to_files(HighSymbolTable& high) {
	map_types_to_files_based_on_this_pointers(high);
}

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
							if(type_index != foreign_file.stabs_type_number_to_deduplicated_type_index.end()) {
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

}
