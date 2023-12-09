// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "ccc/stabs.h"

using namespace ccc;

#define STABS_TEST(name, stab) \
	static void stabs_test_##name(const StabsSymbol& symbol); \
	TEST(CCCStabs, name) { \
		const char* input = stab; \
		Result<StabsSymbol> symbol = parse_stabs_symbol(input); \
		CCC_GTEST_FAIL_IF_ERROR(symbol); \
		stabs_test_##name(*symbol); \
	} \
	static void stabs_test_##name(const StabsSymbol& symbol)

// struct SimpleStruct {
// 	int a;
// };
STABS_TEST(SimpleStruct, "SimpleStruct:T(1,1)=s4a:(0,1),0,32;;") {
	ASSERT_FALSE(symbol.type->anonymous);
	ASSERT_EQ(symbol.type->type_number.file, 1);
	ASSERT_EQ(symbol.type->type_number.type, 1);
	ASSERT_TRUE(symbol.type->has_body);
	ASSERT_EQ(symbol.type->descriptor, StabsTypeDescriptor::STRUCT);
	
	StabsStructType& struct_type = symbol.type->as<StabsStructType>();
	ASSERT_EQ(struct_type.size, 4);
	ASSERT_EQ(struct_type.base_classes.size(), 0);
	ASSERT_EQ(struct_type.fields.size(), 1);
	ASSERT_EQ(struct_type.member_functions.size(), 0);
	
	StabsStructOrUnionType::Field& field = struct_type.fields.at(0);
	ASSERT_EQ(field.name, "a");
	ASSERT_EQ(field.offset_bits, 0);
	ASSERT_EQ(field.size_bits, 32);
}
