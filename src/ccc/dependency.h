// This file is part of the Chaos Compiler Collection.
//
// SPDX-License-Identifier: MIT

#pragma once

#include "symbol_table.h"

namespace ccc {

using TypeDependencyAdjacencyList = std::vector<std::set<DataTypeHandle>>;
using FileDependencyAdjacencyList = std::vector<std::set<SourceFileHandle>>;

void map_types_to_files_based_on_this_pointers(SymbolTable& symbol_table);
void map_types_to_files_based_on_reference_count(SymbolTable& symbol_table);
TypeDependencyAdjacencyList build_type_dependency_graph(const SymbolTable& symbol_table);
FileDependencyAdjacencyList build_file_dependency_graph(const SymbolTable& symbol_table, const TypeDependencyAdjacencyList& type_graph);
void print_type_dependency_graph(FILE* out, const SymbolTable& symbol_table, const TypeDependencyAdjacencyList& graph);
void print_file_dependency_graph(FILE* out, const SymbolTable& symbol_table, const FileDependencyAdjacencyList& graph);

}
