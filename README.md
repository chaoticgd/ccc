# Chaos Compiler Collection

A set of tools for reverse engineering PS2 games. This currently includes a STABS symbol table parser that can output C++ types and a half-working EE core MIPS disassembler.

## stdump

Symbol table parser and dumper. Options are provided to print out C++ types, print out the raw STABS symbols, and more. See the help text, displayed when --help is passed, for more details.

## objdump

Half-working EE core MIPS disassembler. Probably not too interesting.

## Building

	cmake -B bin/
	cmake --build bin/

## Resources

- [MIPS Mdebug Debugging Information (David Anderson, 1996)](https://web.archive.org/web/20170305060746/https://www.prevanders.net/Mdebug.ps)
- MIPS Assembly Language Programmer's Guide, Symbol Table Chapter (Silicon Graphics, 1992)
- [The "stabs" representation of debugging information (Julia Menapace, Jim Kingdon, and David MacKenzie, 1992-???)](https://sourceware.org/gdb/onlinedocs/stabs.html)
