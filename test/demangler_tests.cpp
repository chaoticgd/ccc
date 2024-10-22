// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#define HAVE_DECL_BASENAME 1
#include "demangle.h"

#define DEMANGLER_OPNAME_TEST(name, mangled, expected_demangled) \
	TEST(GNUDemangler, name) { \
		char* demangled = cplus_demangle_opname(mangled, 0); \
		const char* expected = expected_demangled; \
		if (demangled) { \
			ASSERT_TRUE(expected && strcmp(demangled, expected) == 0); \
			free((void*) demangled); \
		} else { \
			ASSERT_EQ(expected_demangled, nullptr); \
		} \
	}

DEMANGLER_OPNAME_TEST(NonMangledName, "NonMangled", nullptr);
DEMANGLER_OPNAME_TEST(OpConversionOperator, "__op4Type", "operator Type");
DEMANGLER_OPNAME_TEST(TwoLetterOperator, "__nw", "operator new");
DEMANGLER_OPNAME_TEST(ThreeLetterOperator, "__apl", "operator+=");
DEMANGLER_OPNAME_TEST(OpAssignmentExpression, "op$assign_plus", "operator+=");
DEMANGLER_OPNAME_TEST(OpExpression, "op$plus", "operator+")
DEMANGLER_OPNAME_TEST(Type, "type$4Type", "operator Type");
