# Chaos Compiler Collection

A library and set of command line tools for parsing debugging symbols from PS2 games. The 1.x series of releases were focused on STABS symbols in .mdebug sections, while the 2.x series of releases can also parse standard ELF symbols and SNDLL linker symbols. DWARF support is planned.

- [Releases](https://github.com/chaoticgd/ccc/releases)
- [Unstable Builds](https://github.com/chaoticgd/ccc/releases/tag/unstable)

## Tools

### demangle

C++ symbol demangler with support for both the new Itanium C++ ABI (GCC 3+) mangling scheme and the old GCC 2 scheme.

### objdump

Half-working EE core MIPS disassembler. Probably not too interesting.

### stdump

Symbol table parser and dumper. It can extract the following information:

- Data types (structs, unions, enums, etc)
- Functions (name, return type, parameters and local variables)
- Global variables

The following output formats are supported:

- C++
- JSON

This is intended to be used with [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded) (>= 2.1.0 or one of the unstable builds) to import all of this information into [Ghidra](https://ghidra-sre.org/). Note that despite the name the STABS analyzer should work for the R3000 (IOP) and possibly other MIPS processors as well.

### uncc

This is similar to stdump except it organizes its output into separate source files, and has a number of extra features designed to try and make said output closer to valid source code. A `SOURCES.txt` file must be provided in the output directory, which can be generated using the `stdump files` command (you should fixup the paths manually so that they're relative to the output directory, and remove the addresses). Additionally, non-empty files that do not start with `// STATUS: NOT STARTED` will not be overwritten.

If a `FUNCTIONS.txt` file is provided in the output directory, as can be generated using the included `CCCDecompileAllFunctions.java` script for Ghidra, the code from that file will be used to populate the function bodies in the output. In this case, the first group of local variable declarations emitted will be those recovered from the symbols, and the second group will be from the code provided in the functions file. Function names are demangled.

Global variable data will be printed in a structured way based on its data type.

Data types will be sorted into their corresponding files. Since this information is not stored in the symbol table, uncc uses heuristics to map types to files. Types will be put in `.c` or `.cpp` files when there is only a single translation unit the type appears in, and `.h` files when there are multiple (and hence when heuristics must be used to determine where to put them).

Use of a code formatter such as `clang-format` or `astyle` on the output is recommended.

## Building

	cmake -B bin/
	cmake --build bin/

## Documentation

### Chaos Compiler Collection

- [Library Overview](docs/LibraryOverview.md)
- [Compiler Bugs](docs/CompilerBugs.md)
- [Error Handling](docs/ErrorHandling.md)
- [JSON Format](docs/JsonFormat.md)
- [Project Structure](docs/ProjectStructure.md)
- [Symbol Database](docs/SymbolDatabase.md)

### DWARF (.debug) Section

- [DWARF Debugging Information Format](https://dwarfstd.org/doc/dwarf_1_1_0.pdf) / [in-repo mirror](docs/mirror/dwarf_1_1_0.pdf) / [archive.org mirror](https://web.archive.org/web/20230702091554/https://dwarfstd.org/doc/dwarf_1_1_0.pdf)

### MIPS ABI

- [MIPS EABI](https://sourceware.org/legacy-ml/binutils/2003-06/msg00436.html) / [in-repo mirror](docs/mirror/mips_eabi.txt) / [archive.org mirror](https://web.archive.org/web/20231222053837/https://sourceware.org/legacy-ml/binutils/2003-06/msg00436.html)
- [System V Application Binary Interface MIPS RISC Processor Supplement](https://refspecs.linuxfoundation.org/elf/mipsabi.pdf) / [in-repo mirror](docs/mirror/mipsabi.pdf) / [archive.org mirror](https://web.archive.org/web/20240724010702/https://refspecs.linuxfoundation.org/elf/mipsabi.pdf)

### MIPS Debug (.mdebug) Section

- [Third Eye Software and the MIPS symbol table (Peter Rowell)](http://datahedron.com/mips.html) / [in-repo mirror](docs/mirror/ThirdEyeSoftware.html) / [archive.org mirror](https://web.archive.org/web/20230605005654/http://datahedron.com/mips.html)
- [MIPS Mdebug Debugging Information (David Anderson, 1996)](https://www.prevanders.net/Mdebug.ps) / [in-repo mirror](docs/mirror/Mdebug.ps) / [archive.org mirror](https://web.archive.org/web/20170305060746/https://www.prevanders.net/Mdebug.ps)
- MIPS Assembly Language Programmer's Guide, Symbol Table Chapter (Silicon Graphics, 1992) / [in-repo mirror](docs/mirror/MIPSProgrammingGuide.pdf)
- Tru64 UNIX Object File and Symbol Table Format Specification, Symbol Table Chapter
	- Version 5.1 (Compaq Computer Corporation, 2000) / [in-repo mirror](docs/mirror/tru64coff.pdf)
	- Version 5.0 (Compaq Computer Corporation, 1999) / [in-repo mirror](docs/mirror/OBJSPEC.PDF)
- `mdebugread.c` from gdb (reading)
- `ecoff.c` from gas (writing)
- `include/coff/sym.h` from binutils (headers)

### STABS

- [The "stabs" representation of debugging information (Julia Menapace, Jim Kingdon, and David MacKenzie, 1992-???)](https://sourceware.org/gdb/onlinedocs/stabs.html) / in-repo mirror ([HTML](docs/mirror/stabs.html), [PDF](docs/mirror/stabs.pdf)) / [archive.org mirror](https://web.archive.org/web/20230328114854/https://sourceware.org/gdb/onlinedocs/stabs.html/)
- `stabs.c` from binutils (reading)
- `stabsread.c` from gdb (reading)
- `dbxread.c` from gdb (reading)
- `dbxout.c` from gcc (writing)
- `stab.def` from gcc (symbol codes)

## License

The source code for the CCC library and associated command line tools is released under the MIT license.

The GNU demangler is used, which contains source files licensed under the GPL and the LGPL. RapidJSON is used under the MIT license. The GoogleTest library is used by the test suite under the 3-Clause BSD license.
