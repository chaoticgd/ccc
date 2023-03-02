# Chaos Compiler Collection [![](https://github.com/chaoticgd/ccc/actions/workflows/cmake.yml/badge.svg?branch=main)](https://github.com/chaoticgd/ccc/actions/workflows/cmake.yml)

A set of tools for reverse engineering PS2 games.

## Tools

### demangle

Demangler for the old GNU ABI.

### depgraph

Dependency graph and type-to-file mapping generator.

### objdump

Half-working EE core MIPS disassembler. Probably not too interesting.

### stdump

Mdebug/STABS symbol table parser and dumper. It can extract the following information:

- Data types (structs, unions, enums, etc)
- Functions (name, return type, parameters and local variables)
- Global variables

The following output formats are supported:

- C++
- JSON

This is intended to be used with [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) (>= 2.1.0 or one of the unstable builds) to import all of this information into [Ghidra](https://ghidra-sre.org/). Note that despite the name the STABS analyzer should work for the R3000 (IOP) and possibly other MIPS processors as well.

### uncc

This is similar to stdump except it organizes its output into separate source files. A `SOURCES.txt` file must be provided in the output directory, which can be generated using the `stdump files` command (you should fixup the paths manually so that they're relative to the output directory). Additionally, non-empty files that do not start with `// STATUS: NOT STARTED` will not be overwritten.

Data types will be sorted into their corresponding files. Since this information is not stored in the symbol table, uncc uses heuristics to map types to files. Types will be put in `.c` or `.cpp` files when there is only a single translation unit the type appears in, and `.h` files when there are multiple (and hence when heuristics must be used to determine where to put them).

## Building

	cmake -B bin/
	cmake --build bin/

## Project Structure

	demangle.cpp: See above.
	depgraph.cpp: See above.
	objdump.cpp: See above.
	stdump.cpp: See above.
	uncc.cpp: See above.
	ccc/analysis.cpp: Runs all the different analysis passes.
	ccc/ast.cpp: Converts parsed STABS types to a C++ AST structure.
	ccc/dependency.cpp: Try to recover the include graph for a program and map types to individual files.
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
- `dbxread.c` from gdb (reading)
- `stabs.c` from binutils (reading)
- `dbxout.c` from gcc (writing)
- `stab.def` from gcc (symbol codes)

## JSON Format

### Version History

| Format Version | Release | Changes |
| - | - | - |
| 7 | v1.1, v1.0 | Base classes are now no longer doubly nested inside two JSON objects. Added acccess_specifier property. |
| 6 | | Removed order property. |
| 5 | | Added pointer_to_data_member node type. Added optional is_volatile property to all nodes. Added is_by_reference property to variable storage objects. |
| 4 | | Added optional is_const property to all nodes. Added anonymous_reference type names, where the type name is not valid but the type number is. |
| 3 | | Added optional relative_path property to function definition nodes. |
| 2 | | Added vtable_index property to function type nodes. |
| 1 | | First version. |

## License

All the code is MIT licensed, with the exception of the code in the `demanglegpl` directory, which is taken from the GNU libiberty library and is licensed under the LGPL. This license is included in the form of the `demanglegpl/DEMANGLERCOPYING.LIB` file.
