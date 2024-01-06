// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "symbol_table.h"

#include "elf.h"
#include "elf_symtab.h"
#include "mdebug_importer.h"
#include "mdebug_section.h"
#include "sndll.h"

namespace ccc {

const std::vector<SymbolTableFormatInfo> SYMBOL_TABLE_FORMATS = {
	{MDEBUG, "mdebug", ".mdebug"},
	{SYMTAB, "symtab", ".symtab"},
	{SNDLL, "sndll", ".sndata"}
};

const SymbolTableFormatInfo* symbol_table_format_from_enum(SymbolTableFormat format)
{
	for(size_t i = 0; i < SYMBOL_TABLE_FORMATS.size(); i++) {
		if(SYMBOL_TABLE_FORMATS[i].format == format) {
			return &SYMBOL_TABLE_FORMATS[i];
		}
	}
	return nullptr;
}

const SymbolTableFormatInfo* symbol_table_format_from_name(const char* format_name)
{
	for(size_t i = 0; i < SYMBOL_TABLE_FORMATS.size(); i++) {
		if(strcmp(SYMBOL_TABLE_FORMATS[i].format_name, format_name) == 0) {
			return &SYMBOL_TABLE_FORMATS[i];
		}
	}
	return nullptr;
}

const SymbolTableFormatInfo* symbol_table_format_from_section(const char* section_name)
{
	for(size_t i = 0; i < SYMBOL_TABLE_FORMATS.size(); i++) {
		if(strcmp(SYMBOL_TABLE_FORMATS[i].section_name, section_name) == 0) {
			return &SYMBOL_TABLE_FORMATS[i];
		}
	}
	return nullptr;
}

// *****************************************************************************

Result<std::unique_ptr<SymbolTable>> create_elf_symbol_table(
	const ElfSection& section, const ElfFile& elf, SymbolTableFormat format)
{
	std::unique_ptr<SymbolTable> symbol_table;
	switch(format) {
		case MDEBUG: {
			symbol_table = std::make_unique<MdebugSymbolTable>(elf.image, (s32) section.offset, section.name);
			break;
		}
		case SYMTAB: {
			CCC_CHECK(section.offset + section.size <= elf.image.size(),
				"Section '%s' out of range.", section.name.c_str());
			std::span<const u8> data = std::span(elf.image).subspan(section.offset, section.size);
			
			CCC_CHECK(section.link != 0, "Section '%s' has no linked string table.", section.name.c_str());
			CCC_CHECK(section.link < elf.sections.size(),
				"Section '%s' has out of range link field.", section.name.c_str());
			const ElfSection& linked_section = elf.sections[section.link];
			
			CCC_CHECK(linked_section.offset + linked_section.size <= elf.image.size(),
				"Linked section '%s' out of range.", linked_section.name.c_str());
			std::span<const u8> linked_data = std::span(elf.image).subspan(linked_section.offset, linked_section.size);
			
			symbol_table = std::make_unique<SymtabSymbolTable>(data, linked_data, section.name);
			break;
		}
		case SNDLL: {
			CCC_CHECK(section.offset + section.size <= elf.image.size(),
				"Section '%s' out of range.", section.name.c_str());
			std::span<const u8> data = std::span(elf.image).subspan(section.offset, section.size);
			
			if(data.size() >= 4 && data[0] != '\0') {
				Result<SNDLLFile> file = parse_sndll_file(data, section.address);
				CCC_RETURN_IF_ERROR(file);
				
				symbol_table = std::make_unique<SNDLLSymbolTable>(
					std::make_shared<SNDLLFile>(std::move(*file)), section.name);
			} else {
				CCC_WARN("Invalid SNDLL section.");
			}
			
			break;
		}
	}
	
	return symbol_table;
}

Result<SymbolSourceRange> import_symbol_tables(
	SymbolDatabase& database,
	const std::vector<std::unique_ptr<SymbolTable>>& symbol_tables,
	u32 importer_flags,
	DemanglerFunctions demangler)
{
	SymbolSourceRange symbol_sources;
	
	for(const std::unique_ptr<SymbolTable>& symbol_table : symbol_tables) {
		Result<SymbolSource*> source = database.symbol_sources.create_symbol(symbol_table->name(), SymbolSourceHandle());
		if(!source.success()) {
			database.destroy_symbols_from_sources(symbol_sources);
			return source;
		}
		
		SymbolSourceHandle source_handle = (*source)->handle();
		symbol_sources.expand_to_include(source_handle);
		
		Result<void> result = symbol_table->import(database, source_handle, importer_flags, demangler);
		if(!result.success()) {
			database.destroy_symbols_from_sources(symbol_sources);
			return result;
		}
	}
	
	return symbol_sources;
}

// *****************************************************************************

MdebugSymbolTable::MdebugSymbolTable(std::span<const u8> image, s32 section_offset, std::string section_name)
	: m_image(image), m_section_offset(section_offset), m_section_name(std::move(section_name)) {}

std::string MdebugSymbolTable::name() const
{
	return m_section_name;
}

Result<void> MdebugSymbolTable::import(
	SymbolDatabase& database,
	SymbolSourceHandle source,
	u32 importer_flags,
	DemanglerFunctions demangler) const
{
	return mdebug::import_symbol_table(
		database, m_image, m_section_offset, source, importer_flags | DONT_DEDUPLICATE_SYMBOLS, demangler);
}

Result<void> MdebugSymbolTable::print_headers(FILE* out) const
{
	mdebug::SymbolTableReader reader;
	
	Result<void> reader_result = reader.init(m_image, m_section_offset);
	CCC_RETURN_IF_ERROR(reader_result);
	
	reader.print_header(out);
	
	return Result<void>();
}

Result<void> MdebugSymbolTable::print_symbols(FILE* out, bool print_locals, bool print_externals) const
{
	mdebug::SymbolTableReader reader;
	Result<void> reader_result = reader.init(m_image, m_section_offset);
	CCC_RETURN_IF_ERROR(reader_result);
	
	Result<void> print_result = reader.print_symbols(out, print_locals, print_externals);
	CCC_RETURN_IF_ERROR(print_result);
	
	return Result<void>();
}

// *****************************************************************************

SymtabSymbolTable::SymtabSymbolTable(std::span<const u8> symtab, std::span<const u8> strtab, std::string section_name)
	: m_symtab(symtab), m_strtab(strtab), m_section_name(std::move(section_name)) {}

std::string SymtabSymbolTable::name() const
{
	return m_section_name;
}

Result<void> SymtabSymbolTable::import(
	SymbolDatabase& database,
	SymbolSourceHandle source,
	u32 importer_flags,
	DemanglerFunctions demangler) const
{
	return elf::import_symbols(database, source, m_symtab, m_strtab, importer_flags, demangler);
}

Result<void> SymtabSymbolTable::print_headers(FILE* out) const
{
	return Result<void>();
}

Result<void> SymtabSymbolTable::print_symbols(FILE* out, bool print_locals, bool print_externals) const
{
	Result<void> symbtab_result = elf::print_symbol_table(out, m_symtab, m_strtab);
	CCC_RETURN_IF_ERROR(symbtab_result);
	
	return Result<void>();
}

// *****************************************************************************

SNDLLSymbolTable::SNDLLSymbolTable(std::shared_ptr<SNDLLFile> sndll, std::string fallback_name)
	: m_sndll(std::move(sndll)), m_fallback_name(std::move(fallback_name)) {}

std::string SNDLLSymbolTable::name() const
{
	if(!m_sndll->elf_path.empty()) {
		return "SNDLL: " + m_sndll->elf_path;
	} else if(!m_fallback_name.empty()) {
		return "SNDLL: " + m_fallback_name;
	} else {
		return "SNDLL";
	}
}

Result<void> SNDLLSymbolTable::import(
	SymbolDatabase& database,
	SymbolSourceHandle source,
	u32 importer_flags,
	DemanglerFunctions demangler) const
{
	return import_sndll_symbols(database, *m_sndll, source, importer_flags, demangler);
}

Result<void> SNDLLSymbolTable::print_headers(FILE* out) const
{
	return Result<void>();
}

Result<void> SNDLLSymbolTable::print_symbols(FILE* out, bool print_locals, bool print_externals) const
{
	print_sndll_symbols(out, *m_sndll);
	
	return Result<void>();
}

// *****************************************************************************

ElfSectionHeadersSymbolTable::ElfSectionHeadersSymbolTable(const ElfFile& elf)
	: m_elf(elf) {}

std::string ElfSectionHeadersSymbolTable::name() const
{
	return "ELF Section Headers";
}

Result<void> ElfSectionHeadersSymbolTable::import(
	SymbolDatabase& database,
	SymbolSourceHandle source,
	u32 importer_flags,
	DemanglerFunctions demangler) const
{
	return import_elf_section_headers(database, m_elf, source);
}

Result<void> ElfSectionHeadersSymbolTable::print_headers(FILE* out) const
{
	return Result<void>();
}

Result<void> ElfSectionHeadersSymbolTable::print_symbols(FILE* out, bool print_locals, bool print_externals) const
{
	return Result<void>();
}

}
