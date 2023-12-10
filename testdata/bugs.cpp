// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

// This file is meant to produce a .mdebug section that will be hard to parse
// because it triggers various GCC bugs and other such edge cases.

// You can build this file with the following command:
//   ee-g++ bugs.cpp -o bugs.elf -gstabs -nostdlib

// All the offsets stored in the .mdebug section are relative to the beginning
// of the ELF file, not the beginning of the section. This means that if we move
// the .mdebug section without updating its contents it gets corrupted. One way
// to do this is like so:
//   ee-objcopy bugs.elf --only-section .mdebug

// This will generate a stab with a C++ type name containing a colon, even
// though the type name field is supposed to be terminated with a colon.
namespace Namespace { struct A; }
template <typename T> struct DodgyTypeName {};
template struct DodgyTypeName<Namespace::A>;

// This will generate a truncated stab string since the 0 passed as the template
// argument to ThisStabWillGetTruncated will act as a null terminator.
template <char c>
struct ThisStabWillGetTruncated { int* pointer; };
template struct ThisStabWillGetTruncated<0>;

// This struct would normally be emitted correctly, however because the stab
// above got truncated, and that stab is the first to define a pointer to an
// int, this stab will try to reference that one so will also be invalid.
struct Lies {
	int* faulty_pointer;
};
