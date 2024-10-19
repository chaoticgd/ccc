// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

// ee-g++ bitfields.cpp -o bitfields.elf -gstabs+ -nostdlib

struct LeftToRightAllocation {
	int j : 5;
	int k : 6;
	int m : 7;
};

struct BoundaryAlignment {
	short s : 9;
	int j : 9;
	char c;
	short t : 9;
	short u : 9;
	char d;
};

struct StorageUnitSharing {
	char c;
	short s : 8;
};

union UnionAllocation {
	char c;
	short s : 8;
};

struct UnnamedBitFields {
	char c;
	int : 0;
	char d;
	short : 9;
	char e;
	char : 0;
};

struct Unsigned {
	unsigned int a : 3;
	unsigned int b : 8;
	unsigned int c : 12;
};

typedef unsigned long long u128 __attribute__((mode(TI)));

struct UnsignedGarFields {
	u128 a : 127;
	u128 b : 1;
};

typedef long long s128 __attribute__((mode(TI)));

struct SignedGarFields {
	s128 a : 127;
	s128 b : 1;
};

LeftToRightAllocation left_to_right_allocations = {
	9,
	-32,
	-5
};

BoundaryAlignment boundary_alignment = {
	-256,
	255,
	0,
	5,
	-2,
	100
};

StorageUnitSharing storage_unit_sharing = {
	8,
	127
};

UnionAllocation union_allocation = {
	123
};

UnnamedBitFields unnamed_bitfields = {
	123,
	45,
	67
};

Unsigned a = {
	1,
	2,
	3
};

UnsignedGarFields unsigned_garfields = {
	0x0fffffffffffffffffffffffffffffff, 1
};

SignedGarFields signed_garfields = {
	0x0fffffffffffffffffffffffffffffff, -1
};

volatile int loop = 1;

extern "C" void _start()
{
	while(loop);
}
