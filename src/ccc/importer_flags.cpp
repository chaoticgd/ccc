// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "importer_flags.h"

namespace ccc {

const std::vector<ImporterFlagInfo> IMPORTER_FLAGS = {
	{DEMANGLE_PARAMETERS, "--demangle-parameters", {
		"Include parameters in demangled function names."
	}},
	{DEMANGLE_RETURN_TYPE, "--demangle-return-type", {
		"Include return types at the end of demangled",
		"function names if they're available."
	}},
	{DONT_DEDUPLICATE_SYMBOLS, "--dont-deduplicate-symbols", {
		"Do not deduplicate matching symbols from",
		"different symbol tables. This options has no",
		"effect on data types."
	}},
	{DONT_DEDUPLICATE_TYPES, "--dont-deduplicate-types", {
		"Do not deduplicate data types from different",
		"translation units."
	}},
	{DONT_DEMANGLE_NAMES, "--dont-demangle-names", {
		"Do not demangle function names, global variable",
		"names, or overloaded operator names."
	}},
	{INCLUDE_GENERATED_MEMBER_FUNCTIONS, "--include-generated-functions", {
		"Output member functions that were likely",
		"automatically generated by the compiler."
	}},
	{NO_ACCESS_SPECIFIERS, "--no-access-specifiers", {
		"Do not print access specifiers."
	}},
	{NO_MEMBER_FUNCTIONS, "--no-member-functions", {
		"Do not print member functions."
	}},
	{STRICT_PARSING, "--strict", {
		"Enable strict parsing, which makes certain types",
		"of errors that are likely to be caused by",
		"compiler bugs fatal."
	}},
	{TYPEDEF_ALL_ENUMS, "--typedef-all-enums", {
		"Force all emitted C++ enums to be defined using",
		"a typedef. With STABS, it is not always possible",
		"to determine if an enum was like this in the",
		"original source code, so this option should be",
		"useful for reverse engineering C projects."
	}},
	{TYPEDEF_ALL_STRUCTS, "--typedef-all-structs", {
		"Force all emitted C++ structure types to be",
		"defined using a typedef."
	}},
	{TYPEDEF_ALL_UNIONS, "--typedef-all-unions", {
		"Force all emitted C++ union types to be defined",
		"using a typedef."
	}}
};

u32 parse_importer_flag(const char* argument)
{
	for(const ImporterFlagInfo& flag : IMPORTER_FLAGS) {
		if(strcmp(flag.argument, argument) == 0) {
			return flag.flag;
		}
	}
	return NO_IMPORTER_FLAGS;
}

void print_importer_flags_help(FILE* out)
{
	for(const ImporterFlagInfo& flag : IMPORTER_FLAGS) {
		fprintf(out, "\n");
		fprintf(out, "  %-29s ", flag.argument);
		for(size_t i = 0; i < flag.help_text.size(); i++) {
			if(i > 0) {
				fprintf(out, "                                ");
			}
			fprintf(out, "%s\n", flag.help_text[i]);
		}
	}
}

}
