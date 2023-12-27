// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#define RAPIDJSON_HAS_STDSTRING 1
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

#include "ast.h"

namespace ccc {

class SymbolDatabase;

using JsonWriter = rapidjson::PrettyWriter<rapidjson::StringBuffer>;

namespace ast {

void write_json(JsonWriter& json, const Node* ptr, const SymbolDatabase& database);

}
}
