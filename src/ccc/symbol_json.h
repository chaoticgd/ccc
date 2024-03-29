// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "ast_json.h"
#include "symbol_database.h"

namespace ccc {

extern const u32 JSON_FORMAT_VERSION;

void write_json(
	JsonWriter& json,
	const SymbolDatabase& database,
	const char* application_name,
	const std::set<SymbolSourceHandle>* sources = nullptr);

}
