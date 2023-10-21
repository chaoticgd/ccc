# Changelog

# v1.3

- Improve bitfield detection algorithm.
- Improve how floating point literals are printed for global data.

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
