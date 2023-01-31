# Chaos Compiler Collection [![](https://github.com/chaoticgd/ccc/actions/workflows/cmake.yml/badge.svg?branch=main)](https://github.com/chaoticgd/ccc/actions/workflows/cmake.yml)

A set of tools for reverse engineering PS2 games.

## Tools

### stdump

Mdebug/STABS symbol table parser and dumper. It can extract the following information:

- Functions including parameters and local variables
- Global variables
- Types (structs, unions, enums, etc)

The following output formats are supported:

- C++
- JSON

The JSON output can be fed into the included `ImportStdumpSymbolsIntoGhidra.java` script, which will import functions, global variables and data types into [Ghidra](https://github.com/NationalSecurityAgency/ghidra). **Ghidra 10.1 or later is required.** The script is intended to be used alongside the [ghidra-emotionengine](https://github.com/beardypig/ghidra-emotionengine) extension. The procedure for using this script on a fresh file is as follows:

1. Run the script **before** running auto analysis.
2. Run auto analysis, making sure to enable the "Use Deprecated Demangler" setting for the `Demangler GNU` analyser if the program you're analysing uses the old GNU mangling. We also recommend running the `MIPS-R5900 Constant Reference` analyzer included with ghidra-emotionengine, as well as `Variadic Function Signature Override`.

Note that the Ghidra script currently has some trouble with certain games.

### objdump

Half-working EE core MIPS disassembler. Probably not too interesting.

## Building

	cmake -B bin/
	cmake --build bin/

## Project Structure

	ImportStdumpSymbolsIntoGhidra.java: See above.
	stdump.cpp: See above.
	objdump.cpp: See above.
	ccc/analysis.cpp: Runs all the different analysis passes.
	ccc/ast.cpp: Converts parsed STABS types to a C++ AST structure.
	ccc/elf.cpp: Parses ELF files.
	ccc/insn.cpp: Parses EE core MIPS instructions.
	ccc/mdebug.cpp: Read the .mdebug symbol table section.
	ccc/module.cpp: Provides data structures for representing programs.
	ccc/opcodes.h: Enums for different types of EE core MIPS opcodes.
	ccc/print_cpp.cpp: Prints out AST nodes as C++ code.
	ccc/print_json.cpp: Prints out AST nodes as JSON.
	ccc/registers.cpp: Enums for EE core MIPS registers.
	ccc/stabs.cpp: Parses STABS types.
	ccc/symbols.cpp: Parses the STABS and non-STABS symbols.
	ccc/tables.cpp: Table of EE core MIPS instructions.
	ccc/util.cpp: Miscellaneous utilities.

## Resources

- [MIPS Mdebug Debugging Information (David Anderson, 1996)](https://web.archive.org/web/20170305060746/https://www.prevanders.net/Mdebug.ps)
- MIPS Assembly Language Programmer's Guide, Symbol Table Chapter (Silicon Graphics, 1992)
- [The "stabs" representation of debugging information (Julia Menapace, Jim Kingdon, and David MacKenzie, 1992-???)](https://sourceware.org/gdb/onlinedocs/stabs.html)
- `stabs.c` from binutils (reading), `dbxout.c` from gcc (writing) and `stab.def` from gcc (symbol codes).

## JSON Format

### Version History

| Version | Changes |
| - | - |
| 7 | Base classes are now no longer doubly nested inside two JSON objects. Added 'acccess_specifier' property. |
| 6 | Removed 'order' property. |
| 5 | Added 'pointer_to_data_member' node type. Added optional is_volatile property to all nodes. Added is_by_reference property to variable storage objects. |
| 4 | Added optional is_const property to all nodes. Added 'anonymous_reference' type names, where the type name is not valid but the type number is. |
| 3 | Added optional relative_path property to function definition nodes. |
| 2 | Added vtable_index property to function type nodes. |
| 1 | First version. |
