// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "elf.h"

#include "importer_flags.h"

namespace ccc {

Result<ElfFile> ElfFile::parse(std::vector<u8> image)
{
	ElfFile elf;
	elf.image = std::move(image);
	
	const ElfIdentHeader* ident = get_packed<ElfIdentHeader>(elf.image, 0);
	CCC_CHECK(ident, "ELF ident header out of range.");
	CCC_CHECK(ident->magic == CCC_FOURCC("\x7f\x45\x4c\x46"), "Not an ELF file.");
	CCC_CHECK(ident->e_class == ElfIdentClass::B32, "Wrong ELF class (not 32 bit).");
	
	const ElfFileHeader* header = get_packed<ElfFileHeader>(elf.image, sizeof(ElfIdentHeader));
	CCC_CHECK(header, "ELF file header out of range.");
	elf.file_header = *header;
	
	const ElfSectionHeader* shstr_section_header = get_packed<ElfSectionHeader>(elf.image, header->shoff + header->shstrndx * sizeof(ElfSectionHeader));
	CCC_CHECK(shstr_section_header, "ELF section name header out of range.");
	
	for (u32 i = 0; i < header->shnum; i++) {
		u64 header_offset = header->shoff + i * sizeof(ElfSectionHeader);
		const ElfSectionHeader* section_header = get_packed<ElfSectionHeader>(elf.image, header_offset);
		CCC_CHECK(section_header, "ELF section header out of range.");
		
		const char* name = get_string(elf.image, shstr_section_header->offset + section_header->name);
		CCC_CHECK(section_header, "ELF section name out of range.");
		
		ElfSection& section = elf.sections.emplace_back();
		section.name = name;
		section.header = *section_header;
	}
	
	for (u32 i = 0; i < header->phnum; i++) {
		u64 header_offset = header->phoff + i * sizeof(ElfProgramHeader);
		const ElfProgramHeader* program_header = get_packed<ElfProgramHeader>(elf.image, header_offset);
		CCC_CHECK(program_header, "ELF program header out of range.");
		
		elf.segments.emplace_back(*program_header);
	}
	
	return elf;
}

Result<void> ElfFile::import_section_headers(
	SymbolDatabase& database, const SymbolGroup& group, u32 importer_flags, DemanglerFunctions demangler) const
{
	for (const ElfSection& section : sections) {
		Address address = Address::non_zero(section.header.addr);
		
		Result<Section*> symbol = database.sections.create_symbol(
			section.name, address, group.source, group.module_symbol);
		CCC_RETURN_IF_ERROR(symbol);
		
		(*symbol)->set_size(section.header.size);
	}
	
	// Parse .gnu.linkonce.* section names. These are be generated as part of
	// vague linking e.g. for template instantiations, and can be left in the
	// final binary if the linker script doesn't have proper support for them.
	if ((importer_flags & NO_LINKONCE_SYMBOLS) == 0) {
		for (const ElfSection& section : sections) {
			std::optional<ElfLinkOnceSection> link_once = parse_link_once_section_name(section.name);
			if (!link_once.has_value()) {
				continue;
			}
			
			Address address = Address::non_zero(section.header.addr);
			
			if (link_once->is_text) {
				if (database.functions.first_handle_from_starting_address(address).valid()) {
					continue;
				}
				
				Result<Function*> function = database.functions.create_symbol(
					std::move(link_once->symbol_name), group.source, group.module_symbol, address, importer_flags, demangler);
				CCC_RETURN_IF_ERROR(function);
			} else {
				if (database.global_variables.first_handle_from_starting_address(address).valid()) {
					continue;
				}
				
				Result<GlobalVariable*> global_variable = database.global_variables.create_symbol(
					std::move(link_once->symbol_name), group.source, group.module_symbol, address, importer_flags, demangler);
				CCC_RETURN_IF_ERROR(global_variable);
				
				(*global_variable)->storage.location = link_once->location;
			}
		}
	}
	
	return Result<void>();
}

std::optional<ElfLinkOnceSection> ElfFile::parse_link_once_section_name(const std::string& section_name)
{
	if (!section_name.starts_with(".gnu.linkonce.") || section_name.size() < 17) {
		return std::nullopt;
	}
	
	ElfLinkOnceSection result;
	
	if (section_name[15] == '.') {
		char type = section_name[14];
		switch (type) {
			case 'b': { // .bss
				result.location = GlobalStorageLocation::BSS;
				result.symbol_name = section_name.substr(16);
				break;
			}
			case 'd': { // .data
				result.location = GlobalStorageLocation::DATA;
				result.symbol_name = section_name.substr(16);
				break;
			}
			case 's': { // .sdata
				result.location = GlobalStorageLocation::SDATA;
				result.symbol_name = section_name.substr(16);
				break;
			}
			case 't': { // .text
				result.is_text = true;
				result.symbol_name = section_name.substr(16);
			}
		}
	} else if (section_name[14] == 's' && section_name[15] == 'b' && section_name[16] == '.') { // .sbss
		result.location = GlobalStorageLocation::SBSS;
		result.symbol_name = section_name.substr(17);
	}
	
	if (result.symbol_name.empty()) {
		return std::nullopt;
	}
	
	return result;
}

const ElfSection* ElfFile::lookup_section(const char* name) const
{
	for (const ElfSection& section : sections) {
		if (section.name == name) {
			return &section;
		}
	}
	return nullptr;
}

std::optional<u32> ElfFile::file_offset_to_virtual_address(u32 file_offset) const
{
	for (const ElfProgramHeader& segment : segments) {
		if (file_offset >= segment.offset && file_offset < segment.offset + segment.filesz) {
			return segment.vaddr + file_offset - segment.offset;
		}
	}
	return std::nullopt;
}

const ElfProgramHeader* ElfFile::entry_point_segment() const
{
	const ccc::ElfProgramHeader* entry_segment = nullptr;
	for (const ccc::ElfProgramHeader& segment : segments) {
		if (file_header.entry >= segment.vaddr && file_header.entry < segment.vaddr + segment.filesz) {
			entry_segment = &segment;
		}
	}
	return entry_segment;
}

Result<std::span<const u8>> ElfFile::get_virtual(u32 address, u32 size) const
{
	u32 end_address = address + size;
	
	if (end_address >= address) {
		for (const ElfProgramHeader& segment : segments) {
			if (address >= segment.vaddr && end_address <= segment.vaddr + segment.filesz) {
				size_t begin_offset = segment.offset + (address - segment.vaddr);
				size_t end_offset = begin_offset + size;
				if (begin_offset <= image.size() && end_offset <= image.size()) {
					return std::span<const u8>(image.data() + begin_offset, image.data() + end_offset);
				}
			}
		}
	}
	
	return CCC_FAILURE("No ELF segment for address range 0x%x to 0x%x.", address, end_address);
}

Result<void> ElfFile::copy_virtual(u8* dest, u32 address, u32 size) const
{
	Result<std::span<const u8>> block = get_virtual(address, size);
	CCC_RETURN_IF_ERROR(block);
	
	memcpy(dest, block->data(), size);
	
	return Result<void>();
}

}
