// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "util.h"

namespace ccc {

enum ImporterFlags {
	NO_IMPORTER_FLAGS = 0,
	DONT_DEDUPLICATE_SYMBOLS = (1 << 0),
	DONT_DEDUPLICATE_TYPES = (1 << 1),
	DONT_DEMANGLE_NAMES = (1 << 2),
	INCLUDE_GENERATED_MEMBER_FUNCTIONS = (1 << 3),
	NO_ACCESS_SPECIFIERS = (1 << 4),
	NO_MEMBER_FUNCTIONS = (1 << 5),
	STRICT_PARSING = (1 << 6),
	TYPEDEF_ALL_ENUMS = (1 << 7),
	TYPEDEF_ALL_STRUCTS = (1 << 8),
	TYPEDEF_ALL_UNIONS = (1 << 9)
};

struct ImporterFlagInfo {
	ImporterFlags flag;
	const char* argument;
	std::vector<const char*> help_text;
};

extern const std::vector<ImporterFlagInfo> IMPORTER_FLAGS;

u32 parse_importer_flag(const char* argument);
void print_importer_flags_help(FILE* out);

}
