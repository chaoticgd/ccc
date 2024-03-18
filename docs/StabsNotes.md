# STABS Notes

STABS is a text-based format for debugging symbols that used to be popular on
UNIX systems, and that hence was adopted by GCC/GDB and was used for PS2 games.

It seemingly has no proper specification, so the best resource for learning
about it is the GNU documentation:

- [The "stabs" representation of debugging information (Julia Menapace, Jim Kingdon, and David MacKenzie)](https://sourceware.org/gdb/onlinedocs/stabs.html)

The rest of this file documents a number of issues I've found with it and its
implementation in GCC.

## Compiler Bugs & Quirks

### Array Bounds Wrapping

The upper bound stored in the STABS type for arrays is one less than the number
written in the array subscript in the C/C++ source code. For some compiler
versions this can cause the number to wrap around to the maximum value of an
unsigned 32-bit integer. For example:

```
dwarf_cie:T127=s12length:123,0,32;CIE_id:122,32,32;version:126,64,8;augmentation:128=ar31;0;4294967295;2,72,0;;
```

### Unescaped Type Names

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

### Recursively Emitted Structures (and Unions)

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

### Void and __builtin_va_list

Sometimes a `__builtin_va_list` symbol will be emitted and then all symbols
after that one that have a `void` type will reference that one instead of an
actual `void` symbol.

I believe the correct way to identify a void type in GCC's dialect of STABS is
to detect a self-reference e.g. (1,0)=(1,0) rather than relying on the type name
stored in the symbol table.

### Vtable Pointer Type

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
