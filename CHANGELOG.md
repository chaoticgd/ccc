# Changelog

## v2.0

- Added support for ELF and SNDLL symbol tables.
- Rewrote core symbol table data structures.
- Massively reduced memory usage.
- Reworked error handling.
- Fixed a bunch of STABS parsing issues.
- Demangle overloaded operator function names.
- stdump: Added `identify` command.
- stdump: Removed `--verbose` option (no one actually used that, right?).
- stdump: Dropped support for legacy command aliases (e.g. `print_types` instead of `types`).
- stdump: Fixed issue where the `mdebug` option would display some sizes as counts.
- stdump: Demangling is now performed automatically unless `--mangled` is passed.
- uncc: Improve how pointers to global variables in other global variable data is printed.
- Probably lots more I'm forgetting!

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
