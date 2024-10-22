// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "ccc/ccc.h"
#define HAVE_DECL_BASENAME 1
#include "demangle.h"

using namespace ccc;

extern const char* git_tag;

int main(int argc, char** argv)
{
	if (argc == 2 && !(strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
		const char* demangled = cplus_demangle(argv[1], DMGL_PARAMS | DMGL_RET_POSTFIX);
		CCC_EXIT_IF_FALSE(demangled, "Cannot demangle input!");
		printf("%s\n", demangled);
		return 0;
	} else {
		printf("demangle %s -- https://github.com/chaoticgd/ccc\n",
			(strlen(git_tag) > 0) ? git_tag : "development version");
		printf("\n");
		printf("usage: %s <mangled symbol>\n", (argc > 0) ? argv[0] : "demangle");
		printf("\n");
		printf("The demangler library used is licensed under the LGPL, the rest is MIT licensed.\n");
		printf("See the LICENSE and DEMANGLERLICENSE files for more information.\n");
		return 1;
	}
}
