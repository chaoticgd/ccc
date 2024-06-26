# Compiler Bugs

This file documents a number of issues I've found with the .mdebug binary
format, the STABS format, and GCC. These include both design defects,
implementation bugs, and other quirks.

## Absolute File Offsets

File offsets stored in the .mdebug section are relative to the beginning of the
file, not the beginning of the section. This means that if a .mdebug section is
moved without updating its contents, all the file offsets stored inside will be
incorrect by a constant amount.

In CCC this is handled by the `get_corruption_fixing_fudge_offset` function in
`mdebug_section.cpp`.

## Array Bounds Wrapping

The upper bound stored in the STABS type for arrays is one less than the number
written in the array subscript in the C/C++ source code. For some compiler
versions this can cause the number to wrap around to the maximum value of an
unsigned 32-bit integer. For example:

```
dwarf_cie:T127=s12length:123,0,32;CIE_id:122,32,32;version:126,64,8;augmentation:128=ar31;0;4294967295;2,72,0;;
```

## Fake Functions

In some games multiple function symbols are emitted for the same address, and
obviously only one of these functions is valid.

This even happens within the same translation unit, which I think rules out
overlays as a potential cause. There may be cases where symbols from multiple
overlays have been mashed together, but that would be a separate case.

These files do however seem to lack any function symbols with a sentinel address
of 0xffffffff. My current theory is hence that some versions of the linker would
fail to correctly replace the addresses of functions that were optimised out.

Luckily the external .mdebug symbol table (and the ELF symbol table) only
contains a symbol for the correct function in these cases, so you can use that
information to determine which function is the real one.

Affected games:
- Jet X2O
- Orange Pocket: Root

## Multiple Symbols For The Same Function

Multiple translation units can contain function symbols with the same name and
address. These are duplicates, so make sure to deduplicate them if that's
important for your use case.

An affected game:
- Jet X2O

## Recursively Emitted Structures (and Unions)

Some compiler versions will emit the definition for a struct or union twice if
it is written using a typedef.

They will do this once normally and another time inside the first definition,
when the parameters of a generated member function tries to reference the
enclosing type.

Below is a synthetic example:

```
PeculiarParameter:t(1,1)=
	s1;some_generated_func::#(1,1),(0,1),(1,2)=&(1,3)=
		s1;some_generated_func::#(1,1),(0,1),
			(1,2)
		;:RC17PeculiarParameter;2A.;;
	;:RC17PeculiarParameter;2A.;;
```

In CCC this is handled by the `fix_recursively_emitted_structures` function.

An affected game:
- Sega Soccer Slam

## Relative Addresses for Static Functions

In most cases the addresses stored in the value field of a symbol in the .mdebug
symbol will be absolute, however in the case of MTV Music Generator 2 the PROC
symbols are relative for static functions only.

The FUN symbols still have absolute addresses though, so it makes sense to use
those instead. Note that this doesn't fix the issue in cases where there is no
FUN symbol (e.g. assembly files, or otherwise source files without full
debugging symbols).

## Unescaped Type Names

Type names for instantiated template types are unescaped, meaning that type
names in STABS symbols can contain colons even if the identifier field is
terminated with a colon. For example:

```
ColonInTypeName<Namespace::A>:T(1,1)=s1;
```

In addition, the raw contents of character literals are appended to the symbol
string, which can lead to strange output and even truncated symbols if a null
character is inserted.

This affects the type name at the beginning of symbols as well as type names
embedded inside cross reference, structure and union types.

More examples of this bug are present in the `bugs.cpp` file included as test
data with CCC and the `stabs_tests.cpp` unit test file.

## Void and __builtin_va_list

Sometimes a `__builtin_va_list` symbol will be emitted and then all symbols
after that one that have a `void` type will reference that one instead of an
actual `void` symbol.

I believe the correct way to identify a void type in GCC's dialect of STABS is
to detect a self-reference e.g. (1,0)=(1,0) rather than relying on the type name
stored in the symbol table.

## Vtable Pointer Type

For some games each vtable entry will just be a single pointer. In others it
will be this structure:

```
typedef struct { // 0x8
	/* 0x0 */ short int __delta;
	/* 0x2 */ short int __index;
	/* 0x4 */ void *__pfn;
	/* 0x4 */ short int __delta2;
} __vtbl_ptr_type;
```

You may notice that that last two fields in the struct, `__pfn` and `__delta2`
are at the same offset. And so they are. I'm not sure why the type information
gets emitted like this, but it does.
