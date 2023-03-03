#ifndef _CCC_DEPENDENCY_H
#define _CCC_DEPENDENCY_H

#include "analysis.h"

namespace ccc {

struct TypeIndex {
	s32 index;
	TypeIndex(s32 i) : index(i) {}
	operator s32() const { return index; }
	friend auto operator<=>(const TypeIndex& lhs, const TypeIndex& rhs) = default;
};

using TypeDependencyAdjacencyList = std::vector<std::set<TypeIndex>>;

struct FileIndex {
	s32 index;
	FileIndex(s32 i) : index(i) {}
	operator s32() const { return index; }
	friend auto operator<=>(const FileIndex& lhs, const FileIndex& rhs) = default;
};

using FileDependencyAdjacencyList = std::vector<std::set<FileIndex>>;

void map_types_to_files_based_on_this_pointers(HighSymbolTable& high);
void map_types_to_files_based_on_reference_count(HighSymbolTable& high);
TypeDependencyAdjacencyList build_type_dependency_graph(const HighSymbolTable& high);
FileDependencyAdjacencyList build_file_dependency_graph(const HighSymbolTable& high, const TypeDependencyAdjacencyList& type_graph);

}

#endif
