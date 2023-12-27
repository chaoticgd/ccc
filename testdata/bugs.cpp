// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

// This file is meant to produce a .mdebug section that will be hard to parse
// because it triggers various GCC bugs and other such edge cases. So join me,
// as I try to write software to parse corrupted garbage!

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
template <typename T> struct ColonInTypeName {};
template struct ColonInTypeName<Namespace::A>;

// A naive way to fix the above issue is to count the number of less than and
// greater than characters in the type name, and only check for the terminating
// colon if these counts are equal. This doesn't work in the case where the type
// name contains a less than or greater than sign in a character literal.
template <char c> struct LessThanCharacterLiteralInTypeName {};
template struct LessThanCharacterLiteralInTypeName<'<'>;

// And for good measure:
template <char c> struct GreaterThanCharacterLiteralInTypeName {};
template struct GreaterThanCharacterLiteralInTypeName<'>'>;

// Okay, so we can count the number of single quotes in the string, and only
// check for the template argument list delimiters if the count is even. Ahh!
// But what about the case where the type name contains a literal single quote?
template <char c> struct SingleQuoteCharacterLiteralInTypeName {};
template struct SingleQuoteCharacterLiteralInTypeName<'\''>;

// While we're at it, we can even get it to produce non-printable output:
template <char c> struct NonPrintableCharacterLiteralInTypeName {};
template struct NonPrintableCharacterLiteralInTypeName<'\xff'>;

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
