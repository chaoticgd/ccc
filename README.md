# Chaos Compiler Collection

A set of tools for reverse engineering PS2 games. This currently includes a STABS symbol table parser that can output C++ types and a half-working EE core MIPS disassembler.

## stdump

Symbol table parser and dumper. Options are provided to print out C++ types, print out the raw STABS symbols, and more. See the help text, displayed when --help is passed, for more details.

## objdump

Half-working EE core MIPS disassembler. Probably not too interesting.

## Building

	cmake -B bin/
	cmake --build bin/

## Project Structure

	objdump.cpp: See above.
	stdump.cpp: See above.
	ccc/analysis.cpp: Runs all the different analysis passes.
	ccc/ast.cpp: Converts parsed STABS types to a C++ AST structure.
	ccc/elf.cpp: Parses ELF files.
	ccc/insn.cpp: Parses EE core MIPS instructions.
	ccc/mdebug.cpp: Read the .mdebug symbol table section.
	ccc/module.cpp: Provides data structures for representing programs.
	ccc/opcodes.h: Enums for different types of EE core MIPS opcodes.
	ccc/print_cpp.cpp: Prints out the data type AST as C++ code.
	ccc/registers.cpp: Enums for EE core MIPS registers.
	ccc/stabs.cpp: Parses STABS types.
	ccc/symbols.cpp: Parses the STABS and non-STABS symbols.
	ccc/tables.cpp: Table of EE core MIPS instructions.
	ccc/util.cpp: Miscellaneous utilities.

## Resources

- [MIPS Mdebug Debugging Information (David Anderson, 1996)](https://web.archive.org/web/20170305060746/https://www.prevanders.net/Mdebug.ps)
- MIPS Assembly Language Programmer's Guide, Symbol Table Chapter (Silicon Graphics, 1992)
- [The "stabs" representation of debugging information (Julia Menapace, Jim Kingdon, and David MacKenzie, 1992-???)](https://sourceware.org/gdb/onlinedocs/stabs.html)
- `stabs.c` from binutils (reading) and `dbxout.c` from gcc (writing).
