// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "ast_json.h"
#include "symbol_database.h"

namespace ccc {

void write_json(JsonWriter& json, const SymbolDatabase& database, const std::set<SymbolSourceHandle>* sources = nullptr);

}
