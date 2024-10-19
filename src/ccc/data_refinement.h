// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "elf.h"
#include "symbol_database.h"

namespace ccc {

struct RefinedData {
	std::string field_name;
	std::variant<std::string, std::vector<RefinedData>> value;
};

struct VariableToRefine {
	Address address;
	const GlobalStorage* storage = nullptr;
	const ast::Node* type = nullptr;
};

bool can_refine_variable(const VariableToRefine& variable);
Result<RefinedData> refine_variable(
	const VariableToRefine& variable, const SymbolDatabase& database, const ElfFile& elf);

}
