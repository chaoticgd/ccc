// This file is part of the Chaos Compiler Collection.
//
// SPDX-License-Identifier: MIT

#pragma once

#include "symbol_database.h"

namespace ccc {

using TypeDependencyAdjacencyList = std::vector<std::set<DataTypeHandle>>;
using FileDependencyAdjacencyList = std::vector<std::set<SourceFileHandle>>;

void map_types_to_files_based_on_this_pointers(SymbolDatabase& database);
void map_types_to_files_based_on_reference_count(SymbolDatabase& database);
TypeDependencyAdjacencyList build_type_dependency_graph(const SymbolDatabase& database);
FileDependencyAdjacencyList build_file_dependency_graph(const SymbolDatabase& database, const TypeDependencyAdjacencyList& type_graph);
void print_type_dependency_graph(FILE* out, const SymbolDatabase& database, const TypeDependencyAdjacencyList& graph);
void print_file_dependency_graph(FILE* out, const SymbolDatabase& database, const FileDependencyAdjacencyList& graph);

}
