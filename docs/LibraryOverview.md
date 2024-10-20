# Library Overview

The CCC library (the code in `src/ccc/`) allows for symbol tables in multiple
different formats to be imported into a single unified data structure in memory.

It is written in C++20 and only nonoptionally depends on the C and C++ standard
libraries. It has an optional dependency on RapidJSON (which can be removed by
simply not building the source files with `json` in the name), and takes in
functions pointers for a demangler library at runtime (which can be null).

## Symbol Files

The `SymbolFile` abstract base class is used to represent files containing
symbol tables. Right now, ELF files and SNDLL (SN Systems Dynamic Link Library)
files are supported.

These classes provides functionality to enumerate the symbol tables in the
symbol file, and to retrieve the name of the file.

## Symbol Tables

The `SymbolTable` abstract base class is used to represent symbol tables
themselves. Right now, ELF (`.symtab`), MIPS Debug (`.mdebug`) and SNDLL symbol
tables are supported as well as ELF section headers.

These classes provides functionality to import the symbol from the symbol table
into a symbol database, to print out the headers and symbols as human-readable
text, and to retrieve the name of the symbol table format.

## Symbol Database

The symbol database provides a unified in-memory and on-disk representation for
debugging symbols.

Main document: [Symbol Database](SymbolDatabase.md)

## Abstract Syntax Tree

Each symbol in the symbol database has a type field which may or may not point
to the root of an AST representing the data type associated with that symbol.
