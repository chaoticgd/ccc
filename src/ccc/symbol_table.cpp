// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "symbol_table.h"

#include "elf_symtab.h"
#include "mdebug_importer.h"
#include "mdebug_section.h"

namespace ccc {

Result<SymbolSourceHandle> import_elf_symbol_table(
	SymbolDatabase& database, const ElfFile& elf, const SymbolTableConfig& config);
static Result<std::pair<const ElfSection*, SymbolTableFormat>> get_section_and_format(
	const ElfFile& elf, const SymbolTableConfig& config);
static Result<void> check_sndll_config_is_valid(const SymbolTableConfig& config);

const SymbolTableFormatInfo SYMBOL_TABLE_FORMATS[] = {
	{SYMTAB, "symtab", ".symtab", 2},
	{MDEBUG, "mdebug", ".mdebug", 3},
	{DWARF, "dwarf", ".debug", 0},
	{SNDLL, "sndll", ".sndata", 1}
};
const u32 SYMBOL_TABLE_FORMAT_COUNT = CCC_ARRAY_SIZE(SYMBOL_TABLE_FORMATS);

const SymbolTableFormatInfo* symbol_table_format_from_enum(SymbolTableFormat format)
{
	for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
		if(SYMBOL_TABLE_FORMATS[i].format == format) {
			return &SYMBOL_TABLE_FORMATS[i];
		}
	}
	return nullptr;
}

const SymbolTableFormatInfo* symbol_table_format_from_name(const char* format_name)
{
	for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
		if(strcmp(SYMBOL_TABLE_FORMATS[i].format_name, format_name) == 0) {
			return &SYMBOL_TABLE_FORMATS[i];
		}
	}
	return nullptr;
}

const SymbolTableFormatInfo* symbol_table_format_from_section(const char* section_name)
{
	for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
		if(strcmp(SYMBOL_TABLE_FORMATS[i].section_name, section_name) == 0) {
			return &SYMBOL_TABLE_FORMATS[i];
		}
	}
	return nullptr;
}

Result<SymbolSourceHandle> import_symbol_table(
	SymbolDatabase& database, const SymbolFile& file, const SymbolTableConfig& config)
{
	if(const ElfFile* elf = std::get_if<ElfFile>(&file)) {
		return import_elf_symbol_table(database, *elf, config);
	}
	
	if(const SNDLLFile* sndll = std::get_if<SNDLLFile>(&file)) {
		Result<void> result = check_sndll_config_is_valid(config);
		CCC_RETURN_IF_ERROR(result);
		
		return import_sndll_symbol_table(database, *sndll);
	}
	
	return CCC_FAILURE("Invalid symbol file.");
}

Result<SymbolSourceHandle> import_elf_symbol_table(
	SymbolDatabase& database, const ElfFile& elf, const SymbolTableConfig& config)
{
	auto section_and_format = get_section_and_format(elf, config);
	CCC_RETURN_IF_ERROR(section_and_format);
	const ElfSection* section = section_and_format->first;
	SymbolTableFormat format = section_and_format->second;
	
	if(!section) {
		// No symbol table is present.
		return SymbolSourceHandle();
	}
	
	SymbolSourceHandle source;
	
	switch(format) {
		case SYMTAB: {
			Result<SymbolSourceHandle> source_result = elf::import_symbol_table(database, *section, elf, false);
			CCC_RETURN_IF_ERROR(source_result);
			source = *source_result;
			
			break;
		}
		case MDEBUG: {
			Result<SymbolSourceHandle> source_result = mdebug::import_symbol_table(
				database, elf.image, section->offset, config.parser_flags, config.demangler);
			CCC_RETURN_IF_ERROR(source_result);
			source = *source_result;
			
			break;
		}
		case SNDLL: {
			std::span<const u8> section_data = std::span(elf.image).subspan(section->offset, section->size);
			
			const u32* magic = get_packed<u32>(section_data, 0);
			if(!magic || *magic == 0) {
				CCC_WARN("Section '%s' is empty.", section->name.c_str());
				break;
			}
			
			Result<SNDLLFile> sndll = parse_sndll_file(section_data, section->address);
			CCC_RETURN_IF_ERROR(sndll);
			
			Result<SymbolSourceHandle> source_result = import_sndll_symbol_table(database, *sndll);
			CCC_RETURN_IF_ERROR(source_result);
			source = *source_result;
			
			break;
		}
		default: {
			return CCC_FAILURE("The selected symbol table format isn't supported.");
		}
	}
	
	return source;
}

Result<void> print_symbol_table(
	FILE* out, const SymbolFile& file, const SymbolTableConfig& config, bool print_locals, bool print_externals)
{
	if(const ElfFile* elf = std::get_if<ElfFile>(&file)) {
		auto section_and_format = get_section_and_format(*elf, config);
		CCC_RETURN_IF_ERROR(section_and_format);
		const ElfSection* section = section_and_format->first;
		SymbolTableFormat format = section_and_format->second;
		
		if(!section) {
			return CCC_FAILURE("No symbol table present.");
		}
		
		switch(format) {
			case SYMTAB: {
				Result<void> symbtab_result = elf::print_symbol_table(out, *section, *elf);
				CCC_RETURN_IF_ERROR(symbtab_result);
				
				break;
			}
			case MDEBUG: {
				mdebug::SymbolTableReader reader;
				Result<void> reader_result = reader.init(elf->image, section->offset);
				CCC_RETURN_IF_ERROR(reader_result);
				
				Result<void> print_result = reader.print_symbols(out, print_locals, print_externals);
				CCC_RETURN_IF_ERROR(print_result);
				
				break;
			}
			case SNDLL: {
				std::span<const u8> section_data = std::span(elf->image).subspan(section->offset, section->size);
				Result<SNDLLFile> sndll = parse_sndll_file(section_data, section->address);
				CCC_RETURN_IF_ERROR(sndll);
				
				print_sndll_symbols(out, *sndll);
				
				break;
			}
			default: {
				return CCC_FAILURE("The selected symbol table format isn't supported.");
			}
		}
	}
	
	if(const SNDLLFile* sndll = std::get_if<SNDLLFile>(&file)) {
		Result<void> result = check_sndll_config_is_valid(config);
		CCC_RETURN_IF_ERROR(result);
		
		print_sndll_symbols(out, *sndll);
	}
	
	return Result<void>();
}

static Result<std::pair<const ElfSection*, SymbolTableFormat>> get_section_and_format(
	const ElfFile& elf, const SymbolTableConfig& config)
{
	const ElfSection* section = nullptr;
	SymbolTableFormat format = SYMTAB;
	
	if(config.section.has_value()) {
		section = elf.lookup_section(config.section->c_str());
		CCC_CHECK(section, "No '%s' section found.", config.section->c_str());
		
		if(config.format.has_value()) {
			format = *config.format;
		} else {
			const SymbolTableFormatInfo* format_info = symbol_table_format_from_section(section->name.c_str());
			CCC_CHECK(format_info, "Cannot determine symbol table format from section name.");
			
			format = format_info->format;
		}
	} else {
		// Find the most useful symbol table.
		u32 current_utility = 0;
		for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
			const SymbolTableFormatInfo& info = SYMBOL_TABLE_FORMATS[i];
			if((!config.format.has_value() || info.format == *config.format) && info.utility > current_utility) {
				const ElfSection* current_section = elf.lookup_section(info.section_name);
				if(current_section) {
					section = current_section;
					format = info.format;
					current_utility = info.utility;
				}
			}
		}
		
		if(config.format.has_value()) {
			format = *config.format;
		}
	}
	
	return std::make_pair(section, format);
}

static Result<void> check_sndll_config_is_valid(const SymbolTableConfig& config)
{
	CCC_CHECK(!config.section.has_value(), "ELF section specified for SNDLL file.");
	CCC_CHECK(!config.format.has_value() || *config.format == SNDLL,
		"Symbol table format specified for SNDLL file is not 'sndll'.");
	return Result<void>();
}

}
