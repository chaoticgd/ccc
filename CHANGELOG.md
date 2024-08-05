# Changelog

## v2.1

- Fixed an issue where null addresses would be handled incorrectly.
- stdump: Added `--sort-by-address` option.

## v2.0

New features and changes:

- Added support for ELF and SNDLL symbol tables.
- Rewrote core symbol table data structures.
- Overhauled the JSON format.
- Massively reduced memory usage.
- Reworked error handling.
- Overloaded operator function names are now demangled.
- Reworking the command-line interface of both stdump and uncc, adding many new options.
- stdump: Fixed issue where the `headers` option (previously `mdebug`) would display some sizes as counts.
- stdump: Demangling is now performed automatically unless `--dont-demangle-names` is passed.
- uncc: Improve how pointers to global variables in other global variable data are printed.

A bunch of .mdebug symbol table parsing issues have been fixed:

- Virtual inheritance is now detected.
- The logic for determining if a struct was defined using a typedef has been fixed.
- Handled some rare cases where corrupted STABS strings could be generated for certain instantiated template types.
- Some redundant STABS base class information is now parsed. The previous behaviour would very rarely cause some other symbols in the same translation unit to be parsed incorrectly.
- Certain cases where incorrect function symbols would be emitted are now caught and handled.
- A compiler bug where the STABS type for a structure or union would be recursively emitted inside its own definition is now handled.
- A rare case where relative addresses were incorrectly emitted for static functions is now handled better.

## v1.2.1

- Fixed detection of bitfields where the underlying type is a typedef.
- Fixed a rare bug where unsigned 32-bit integer types were incorrectly classified as 64-bit types.
- Improved how floating point literals are printed for global data.

## v1.2

- stdump: A type dependency graph can now be generated.
- stdump: There is now an option to list the source files associated with each ELF section.
- uncc: Data types are now printed. It guesses which types belong to which files based on multiple heuristics.
- uncc: If a `FUNCTIONS.txt` file is provided, code from that file will be included in the output.
- uncc: The initial values of global variables are now printed as initializer lists and literals.
- uncc: The implicit this parameter of member functions will now be omitted from parameter lists.
- uncc: The parameter lists of member function declarations will now not all be empty.

## v1.1

- Added demangle tool.
- Added uncc tool.
- The end of a function in the symbol table is now identified correctly so that extra local variables are not erroneously generated.
- The type deduplication algorithm has been improved.
- stdump: Fixed a bug where global variables would be printed to stdout even if an output file was specified.

## v1.0

- Initial release.
