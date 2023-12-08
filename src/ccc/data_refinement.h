// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include "elf.h"
#include "symbol_database.h"

namespace ccc {

struct RefinedData {
	std::string field_name;
	std::variant<std::string, std::vector<RefinedData>> value;
};

using ReadVirtualFunc = std::function<Result<void>(u8* dest, u32 address, u32 size)>;

struct VariableToRefine {
	Address address;
	const GlobalStorage* storage = nullptr;
	const ast::Node* type = nullptr;
};

bool can_refine_variable(const VariableToRefine& variable);
Result<RefinedData> refine_variable(const VariableToRefine& variable, const SymbolDatabase& database, const ReadVirtualFunc& read_virtual);

}
