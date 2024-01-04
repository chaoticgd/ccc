// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "symbol_database.h"

namespace ccc {

// Determine which symbol tables are present in a given file.

enum SymbolTableFormat {
	MDEBUG = 0, // The infamous Third Eye symbol table
	SYMTAB = 1, // Standard ELF symbol table
	SNDLL  = 2  // SNDLL section
};

struct SymbolTableFormatInfo {
	SymbolTableFormat format;
	const char* format_name;
	const char* section_name;
};

// All the supported symbol table formats, sorted from best to worst.
extern const SymbolTableFormatInfo SYMBOL_TABLE_FORMATS[];
extern const u32 SYMBOL_TABLE_FORMAT_COUNT;

const SymbolTableFormatInfo* symbol_table_format_from_enum(SymbolTableFormat format);
const SymbolTableFormatInfo* symbol_table_format_from_name(const char* format_name);
const SymbolTableFormatInfo* symbol_table_format_from_section(const char* section_name);

enum ImporterFlags {
	NO_IMPORTER_FLAGS = 0,
	DONT_DEDUPLICATE_SYMBOLS = (1 << 0),
	DONT_DEDUPLICATE_TYPES = (1 << 1),
	DONT_DEMANGLE_NAMES = (1 << 2),
	INCLUDE_GENERATED_MEMBER_FUNCTIONS = (1 << 3),
	NO_ACCESS_SPECIFIERS = (1 << 4),
	NO_MEMBER_FUNCTIONS = (1 << 5),
	STRICT_PARSING = (1 << 6),
	TYPEDEF_ALL_ENUMS = (1 << 7),
	TYPEDEF_ALL_STRUCTS = (1 << 8),
	TYPEDEF_ALL_UNIONS = (1 << 9)
};

struct ImporterFlagInfo {
	ImporterFlags flag;
	const char* argument;
	std::vector<const char*> help_text;
};

extern const std::vector<ImporterFlagInfo> IMPORTER_FLAGS;

u32 parse_importer_flag(const char* argument);
void print_importer_flags_help(FILE* out);

class SymbolTable {
public:
	virtual ~SymbolTable() {}
	
	virtual std::string name() const = 0;
	
	// The main high-level import function for the entire library. This parses a
	// symbol table from the data parameter in the specified format and imports
	// all the symbols into the passed database. The link_data parameter is for
	// linked ELF sections, e.g. the .strtab that goes with a .symtab.
	virtual Result<void> import_symbol_table(
		SymbolDatabase& database,
		SymbolSourceHandle source,
		const u32 importer_flags,
		DemanglerFunctions demangler) const = 0;
	
	// Print out all the field in the header structure if one exists.
	virtual Result<void> print_headers(FILE* out) const = 0;
	
	// Print out all the symbols in the symbol table. For .mdebug symbol tables
	// the symbols are split between those that are local to a specific
	// translation unit and those that are external, which is what the
	// print_locals and print_externals parameters control.
	virtual Result<void> print_symbols(FILE* out, bool print_locals, bool print_externals) const = 0;
};

struct ElfSection;
struct ElfFile;

Result<std::unique_ptr<SymbolTable>> create_elf_symbol_table(
	const ElfSection& section, const ElfFile& elf, SymbolTableFormat format);

// Utility function to call import_symbol_table on all the passed symbol tables.
Result<SymbolSourceRange> import_symbol_tables(
	SymbolDatabase& database,
	const std::vector<std::unique_ptr<SymbolTable>>& symbol_tables,
	u32 importer_flags,
	DemanglerFunctions demangler);

class MdebugSymbolTable : public SymbolTable {
public:
	MdebugSymbolTable(std::span<const u8> image, s32 section_offset, std::string section_name);
	
	std::string name() const override;
	
	Result<void> import_symbol_table(
		SymbolDatabase& database, SymbolSourceHandle source, const u32 importer_flags, DemanglerFunctions demangler) const override;
	Result<void> print_headers(FILE* out) const override;
	Result<void> print_symbols(FILE* out, bool print_locals, bool print_externals) const override;
	
protected:
	std::span<const u8> m_image;
	s32 m_section_offset;
	std::string m_section_name;
};

class SymtabSymbolTable : public SymbolTable {
public:
	SymtabSymbolTable(std::span<const u8> symtab, std::span<const u8> strtab, std::string section_name);
	
	std::string name() const override;
	
	Result<void> import_symbol_table(
		SymbolDatabase& database,
		SymbolSourceHandle source,
		const u32 importer_flags,
		DemanglerFunctions demangler) const override;
	
	Result<void> print_headers(FILE* out) const override;
	Result<void> print_symbols(FILE* out, bool print_locals, bool print_externals) const override;
	
protected:
	std::span<const u8> m_symtab;
	std::span<const u8> m_strtab;
	std::string m_section_name;
};

struct SNDLLFile;

class SNDLLSymbolTable : public SymbolTable {
public:
	SNDLLSymbolTable(std::shared_ptr<SNDLLFile> sndll, std::string fallback_name);
	
	std::string name() const override;
	
	Result<void> import_symbol_table(
		SymbolDatabase& database,
		SymbolSourceHandle source,
		const u32 importer_flags,
		DemanglerFunctions demangler) const override;
	
	Result<void> print_headers(FILE* out) const override;
	Result<void> print_symbols(FILE* out, bool print_locals, bool print_externals) const override;
	
protected:
	std::shared_ptr<SNDLLFile> m_sndll;
	std::string m_fallback_name;
};

}
