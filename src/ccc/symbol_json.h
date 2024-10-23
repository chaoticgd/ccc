// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#define RAPIDJSON_HAS_STDSTRING 1
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

#include "symbol_database.h"

namespace ccc {

extern const u32 JSON_FORMAT_VERSION;

template <typename Writer>
void write_json(
	Writer& json,
	const SymbolDatabase& database,
	const char* application_name,
	const std::set<SymbolSourceHandle>* sources = nullptr);

}
