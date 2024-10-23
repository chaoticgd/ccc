// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#define RAPIDJSON_HAS_STDSTRING 1
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

#include "ast.h"
#include "symbol_database.h"

namespace ccc::ast {

template <typename Writer>
void write_json(Writer& json, const Node* ptr, const SymbolDatabase& database);

}
