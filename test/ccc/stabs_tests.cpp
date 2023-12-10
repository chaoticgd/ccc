// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "ccc/stabs.h"

using namespace ccc;

#define STABS_TEST(name, stab) \
	static void stabs_test_##name(StabsSymbol& symbol); \
	TEST(CCCStabs, name) { \
		const char* input = stab; \
		Result<StabsSymbol> symbol = parse_stabs_symbol(input); \
		CCC_GTEST_FAIL_IF_ERROR(symbol); \
		stabs_test_##name(*symbol); \
	} \
	static void stabs_test_##name(StabsSymbol& symbol)

// typedef int s32;
STABS_TEST(TypeNumber, "s32:t1=0") {
	ASSERT_FALSE(symbol.type->anonymous);
	ASSERT_EQ(symbol.type->type_number.file, -1);
	ASSERT_EQ(symbol.type->type_number.type, 1);
	ASSERT_TRUE(symbol.type->has_body);
}

// typedef int s32;
STABS_TEST(FancyTypeNumber, "s32:t(1,1)=(0,1)") {
	ASSERT_FALSE(symbol.type->anonymous);
	ASSERT_EQ(symbol.type->type_number.file, 1);
	ASSERT_EQ(symbol.type->type_number.type, 1);
	ASSERT_TRUE(symbol.type->has_body);
}

// namespace Namespace { struct A; }
// template <typename T> struct DodgyTypeName {};
// template class DodgyTypeName<Namespace::A>;
STABS_TEST(DodgyTypeName, "DodgyTypeName<Namespace::A>:T(1,1)=s1;") {
	ASSERT_EQ(symbol.name, "DodgyTypeName<Namespace::A>");
}

// typedef int s32;
STABS_TEST(TypeReference, "s32:t(1,1)=(0,1)") {
	StabsTypeReferenceType& type_reference = symbol.type->as<StabsTypeReferenceType>();
	ASSERT_FALSE(type_reference.type->anonymous);
	ASSERT_EQ(type_reference.type->type_number.file, 0);
	ASSERT_EQ(type_reference.type->type_number.type, 1);
	ASSERT_FALSE(type_reference.type->has_body);
}

// typedef int Array[1][2];
STABS_TEST(MultiDimensionalArray, "Array:t(1,1)=(1,2)=ar(1,3)=r(1,3);0;4294967295;;0;0;(1,4)=ar(1,3);0;1;(1,5)=ar(1,3);0;2;(0,1)") {
	StabsTypeReferenceType& type_reference = symbol.type->as<StabsTypeReferenceType>();
	StabsArrayType& array = type_reference.type->as<StabsArrayType>();
	
}

// enum E { A = 0, B = 1, C = 2147483647, D = -2147483648 };
STABS_TEST(Enum, "E:t(1,1)=eA:0,B:1,C:2147483647,D:-2147483648,;") {
	StabsEnumType& enum_type = symbol.type->as<StabsEnumType>();
	ASSERT_EQ(enum_type.fields.size(), 4);
	ASSERT_EQ(enum_type.fields.at(0).first, 0);
	ASSERT_EQ(enum_type.fields.at(0).second, "A");
	ASSERT_EQ(enum_type.fields.at(1).first, 1);
	ASSERT_EQ(enum_type.fields.at(1).second, "B");
	ASSERT_EQ(enum_type.fields.at(2).first, 2147483647);
	ASSERT_EQ(enum_type.fields.at(2).second, "C");
	ASSERT_EQ(enum_type.fields.at(3).first, -2147483648);
	ASSERT_EQ(enum_type.fields.at(3).second, "D");
}

// typedef int (function)();
STABS_TEST(Function, "function:t(1,1)=(1,2)=f(0,1)") {
	StabsTypeReferenceType& type_reference = symbol.type->as<StabsTypeReferenceType>();
	StabsFunctionType& function = type_reference.type->as<StabsFunctionType>();
	ASSERT_EQ(function.return_type->type_number.file, 0);
	ASSERT_EQ(function.return_type->type_number.type, 1);
}

// int
STABS_TEST(RangeBuiltIn, "int:t(0,1)=r(0,1);-2147483648;2147483647;") {
	StabsRangeType& range = symbol.type->as<StabsRangeType>();
	ASSERT_EQ(range.low, "-2147483648");
	ASSERT_EQ(range.high, "2147483647");
}

// struct SimpleStruct { int a; };
STABS_TEST(SimpleStruct, "SimpleStruct:T(1,1)=s4a:(0,1),0,32;;") {
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

// union Union { int i; float f; };
STABS_TEST(Union, "Union:T(1,1)=u4i:(0,1),0,32;f:(0,14),0,32;;") {
	StabsUnionType& union_type = symbol.type->as<StabsUnionType>();
	ASSERT_EQ(union_type.size, 4);
	ASSERT_EQ(union_type.base_classes.size(), 0);
	ASSERT_EQ(union_type.fields.size(), 2);
	ASSERT_EQ(union_type.member_functions.size(), 0);
}

// struct ForwardDeclared;
// typedef ForwardDeclared* ForwardDeclaredPtr;
STABS_TEST(CrossReference, "ForwardDeclaredPtr:t(1,1)=(1,2)=*(1,3)=xsForwardDeclared:") {
	
}
