#ifndef _CCC_DEPENDENCY_H
#define _CCC_DEPENDENCY_H

#include "analysis.h"

namespace ccc {

struct TypeIndex {
	s32 index;
	TypeIndex(s32 i) : index(i) {}
	friend auto operator<=>(const TypeIndex& lhs, const TypeIndex& rhs) = default;
};

struct FileIndex {
	s32 index;
	FileIndex(s32 i) : index(i) {}
	friend auto operator<=>(const FileIndex& lhs, const FileIndex& rhs) = default;
};

using TypeDependencyAdjacencyList = std::vector<std::set<TypeIndex>>;

TypeDependencyAdjacencyList build_type_dependency_graph(const HighSymbolTable& high);
void print_type_dependency_graph(FILE* out, const HighSymbolTable& high, const TypeDependencyAdjacencyList& graph);

}

#endif
