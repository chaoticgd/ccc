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
// template struct DodgyTypeName<Namespace::A>;
STABS_TEST(DodgyTypeName, "DodgyTypeName<Namespace::A>:T(1,1)=s1;") {
	ASSERT_EQ(symbol.name, "DodgyTypeName<Namespace::A>");
}

// Synthetic example. Something like:
// namespace Namespace { struct A; }
// template <typename T> struct DodgyVtablePointer { virtual ~DodgyVtablePointer(); };
// template struct DodgyVtablePointer<Namespace::A>;
STABS_TEST(DodgyVtablePointer, "DodgyVtablePointer<Namespace::A>:T(1,2)=s4_vptr$DodgyVtablePointer<Namespace::A>:(1,1),0,32;;") {
	StabsStructType& struct_type = symbol.type->as<StabsStructType>();
	ASSERT_EQ(struct_type.fields.at(0).name, "_vptr$DodgyVtablePointer<Namespace::A>");
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

// template <int num> struct VirtualFunctionStruct { virtual void func() {} };
// template struct VirtualFunctionStruct<1>;
STABS_TEST(VirtualFunctionStruct, "VirtualFunctionStruct<1>:T(1,1)=s4_vptr.VirtualFunctionStruct:(1,2)=*(0,25),0,32;;") {
	StabsStructType& struct_type = symbol.type->as<StabsStructType>();
	ASSERT_EQ(struct_type.fields.at(0).name, "_vptr.VirtualFunctionStruct");
}

// union Union { int i; float f; };
STABS_TEST(Union, "Union:T(1,1)=u4i:(0,1),0,32;f:(0,14),0,32;;") {
	StabsUnionType& union_type = symbol.type->as<StabsUnionType>();
	ASSERT_EQ(union_type.size, 4);
	ASSERT_EQ(union_type.base_classes.size(), 0);
	ASSERT_EQ(union_type.fields.size(), 2);
	ASSERT_EQ(union_type.member_functions.size(), 0);
}

// struct NestedStructsAndUnions {
// 	union { struct { int a; } b; } c;
// 	struct { int d; } e;
// };
STABS_TEST(NestedStructsAndUnions, "NestedStructsAndUnions:T(1,1)=s8c:(1,2)=u4b:(1,3)=s4a:(0,1),0,32;;,0,32;;,0,32;e:(1,4)=s4d:(0,1),0,32;;,32,32;;") {
	StabsStructType& struct_type = symbol.type->as<StabsStructType>();
	StabsStructOrUnionType::Field& c = struct_type.fields.at(0);
	ASSERT_EQ(c.name, "c");
	StabsUnionType& c_type = c.type->as<StabsUnionType>();
	StabsStructOrUnionType::Field& b = c_type.fields.at(0);
	ASSERT_EQ(c_type.fields.at(0).name, "b");
	StabsStructOrUnionType::Field& a = b.type->as<StabsStructType>().fields.at(0);
	ASSERT_EQ(a.name, "a");
	StabsStructOrUnionType::Field& e = struct_type.fields.at(1);
	ASSERT_EQ(e.name, "e");
	StabsStructOrUnionType::Field& d = e.type->as<StabsStructType>().fields.at(0);
	ASSERT_EQ(d.name, "d");
}

// struct ForwardDeclared;
// typedef ForwardDeclared* ForwardDeclaredPtr;
STABS_TEST(CrossReference, "ForwardDeclaredPtr:t(1,1)=(1,2)=*(1,3)=xsForwardDeclared:") {
	StabsTypeReferenceType& type_reference = symbol.type->as<StabsTypeReferenceType>();
	StabsPointerType& pointer = type_reference.type->as<StabsPointerType>();
	StabsCrossReferenceType& cross_reference = pointer.value_type->as<StabsCrossReferenceType>();
	ASSERT_EQ(cross_reference.type, ast::ForwardDeclaredType::STRUCT);
	ASSERT_EQ(cross_reference.identifier, "ForwardDeclared");
}

// Synthetic example. Something like:
// typedef int Struct::*pointer_to_data_member;
STABS_TEST(PointerToDataMember, "pointer_to_data_member:t(1,1)=(1,2)=*(1,3)=@(1,4)=xsStruct:,(0,1)") {
	StabsTypeReferenceType& type_reference = symbol.type->as<StabsTypeReferenceType>();
	StabsPointerType& pointer = type_reference.type->as<StabsPointerType>();
	StabsPointerToDataMemberType& pointer_to_data_member = pointer.value_type->as<StabsPointerToDataMemberType>();
	StabsCrossReferenceType& class_type = pointer_to_data_member.class_type->as<StabsCrossReferenceType>();
	ASSERT_EQ(class_type.identifier, "Struct");
}

