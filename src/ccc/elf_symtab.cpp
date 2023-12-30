// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "elf_symtab.h"

namespace ccc::elf {

enum class SymbolBind : u8 {
	LOCAL = 0,
	GLOBAL = 1,
	WEAK = 2,
	NUM = 3,
	GNU_UNIQUE = 10
};

enum class SymbolType : u8 {
	NOTYPE = 0,
	OBJECT = 1,
	FUNC = 2,
	SECTION = 3,
	FILE = 4,
	COMMON = 5,
	TLS = 6,
	NUM = 7,
	GNU_IFUNC = 10
};

enum class SymbolVisibility {
	DEFAULT = 0,
	INTERNAL = 1,
	HIDDEN = 2,
	PROTECTED = 3
};

CCC_PACKED_STRUCT(Symbol,
	/* 0x0 */ u32 name;
	/* 0x4 */ u32 value;
	/* 0x8 */ u32 size;
	/* 0xc */ u8 info;
	/* 0xd */ u8 other;
	/* 0xe */ u16 shndx;
	
	SymbolType type() const { return (SymbolType) (info & 0xf); }
	SymbolBind bind() const { return (SymbolBind) (info >> 4); }
	SymbolVisibility visibility() const { return (SymbolVisibility) (other & 0x3); }
)

static Result<void> import_symbols(
	SymbolDatabase& database,
	SymbolSourceHandle source,
	const ElfSection& section,
	const ElfFile& elf,
	bool ignore_existing_symbols);

static const char* symbol_bind_to_string(SymbolBind bind);
static const char* symbol_type_to_string(SymbolType type);
static const char* symbol_visibility_to_string(SymbolVisibility visibility);

Result<SymbolSourceHandle> import_symbol_table(
	SymbolDatabase& database, const ElfSection& section, const ElfFile& elf, bool ignore_existing_symbols)
{
	Result<SymbolSource*> source = database.symbol_sources.create_symbol(section.name, SymbolSourceHandle());
	CCC_RETURN_IF_ERROR(source);
	
	Result<void> result = import_symbols(database, (*source)->handle(), section, elf, ignore_existing_symbols);
	if(!result.success()) {
		database.destroy_symbols_from_source((*source)->handle());
		return result;
	}
	
	return (*source)->handle();
}

static Result<void> import_symbols(
	SymbolDatabase& database,
	SymbolSourceHandle source,
	const ElfSection& section,
	const ElfFile& elf,
	bool ignore_existing_symbols)
{
	CCC_CHECK(section.link < elf.sections.size(), "Link field of '%s' section header is out of range.", section.name.c_str());
	
	for(u32 i = 0; i < section.size / sizeof(Symbol); i++) {
		const Symbol* symbol = get_packed<Symbol>(elf.image, section.offset + i * sizeof(Symbol));
		CCC_CHECK(symbol, "Data for '%s' section beyond end of file.", section.name.c_str());
		
		Address address;
		if(symbol->value != 0) {
			address = symbol->value;
		}
		
		if(!address.valid() || symbol->visibility() != SymbolVisibility::DEFAULT) {
			continue;
		}
		
		if(ignore_existing_symbols && database.symbol_exists_with_starting_address(address)) {
			continue;
		}
		
		const char* string = get_string(elf.image, elf.sections[section.link].offset + symbol->name);
		CCC_CHECK(string, "Symbol string out of range.");
		
		switch(symbol->type()) {
			case SymbolType::NOTYPE: {
				Result<Label*> label = database.labels.create_symbol(string, source, address);
				CCC_RETURN_IF_ERROR(label);
				break;
			}
			case SymbolType::OBJECT: {
				Result<GlobalVariable*> global_variable = database.global_variables.create_symbol(string, source, address);
				CCC_RETURN_IF_ERROR(global_variable);
				
				(*global_variable)->set_size(symbol->size);
				
				break;
			}
			case SymbolType::FUNC: {
				Result<Function*> function = database.functions.create_symbol(string, source, address);
				CCC_RETURN_IF_ERROR(function);
				
				(*function)->set_size(symbol->size);
				
				break;
			}
			case SymbolType::FILE: {
				Result<SourceFile*> source_file = database.source_files.create_symbol(string, source);
				CCC_RETURN_IF_ERROR(source_file);
				
				break;
			}
			default: {}
		}
	}
	
	return Result<void>();
}

Result<void> print_symbol_table(FILE* out, const ElfSection& section, const ElfFile& elf)
{
	CCC_CHECK(section.link < elf.sections.size(), "Link field of '%s' section header is out of range.", section.name.c_str());
	
	fprintf(out, "   Num:    Value  Size Type    Bind   Vis      Ndx Name\n");
	
	for(u32 i = 0; i < section.size / sizeof(Symbol); i++) {
		const Symbol* symbol = get_packed<Symbol>(elf.image, section.offset + i * sizeof(Symbol));
		CCC_CHECK(symbol, "Data for '%s' section beyond end of file.", section.name.c_str());
		
		const char* type = symbol_type_to_string(symbol->type());
		const char* bind = symbol_bind_to_string(symbol->bind());
		const char* visibility = symbol_visibility_to_string(symbol->visibility());
		
		const char* string = get_string(elf.image, elf.sections[section.link].offset + symbol->name);
		CCC_CHECK(string, "Symbol string out of range.");
		
		CCC_CHECK(section.link < elf.sections.size(), "Link field of '%s' section header is out of range.", section.name.c_str());
		
		fprintf(out, "%6u: %08x %5u %-7s %-7s %-7s %3u %s\n",
			i, symbol->value, symbol->size, type, bind, visibility, symbol->shndx, string);
		
	}
	
	return Result<void>();
}

static const char* symbol_bind_to_string(SymbolBind bind)
{
	switch(bind) {
		case SymbolBind::LOCAL: return "LOCAL";
		case SymbolBind::GLOBAL: return "GLOBAL";
		case SymbolBind::WEAK: return "WEAK";
		case SymbolBind::NUM: return "NUM";
		case SymbolBind::GNU_UNIQUE: return "GNU_UNIQUE";
	}
	return "ERROR";
}

static const char* symbol_type_to_string(SymbolType type)
{
	switch(type) {
		case SymbolType::NOTYPE: return "NOTYPE";
		case SymbolType::OBJECT: return "OBJECT";
		case SymbolType::FUNC: return "FUNC";
		case SymbolType::SECTION: return "SECTION";
		case SymbolType::FILE: return "FILE";
		case SymbolType::COMMON: return "COMMON";
		case SymbolType::TLS: return "TLS";
		case SymbolType::NUM: return "NUM";
		case SymbolType::GNU_IFUNC: return "GNU_IFUNC";
	}
	return "ERROR";
}

static const char* symbol_visibility_to_string(SymbolVisibility visibility)
{
	switch(visibility) {
		case SymbolVisibility::DEFAULT: return "DEFAULT";
		case SymbolVisibility::INTERNAL: return "INTERNAL";
		case SymbolVisibility::HIDDEN: return "HIDDEN";
		case SymbolVisibility::PROTECTED: return "PROTECTED";
	}
	return "ERROR";
}

}
