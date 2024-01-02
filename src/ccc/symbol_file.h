// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "elf.h"
#include "sndll.h"
#include "symbol_table.h"

namespace ccc {

struct SymbolTableLocation {
	std::string section_name;
	SymbolTableFormat format;
};

class SymbolFile {
public:
	virtual ~SymbolFile() {}
	
	virtual Result<std::vector<std::unique_ptr<SymbolTable>>> get_all_symbol_tables() const = 0;
	virtual Result<std::vector<std::unique_ptr<SymbolTable>>> get_symbol_tables_from_sections(
		const std::vector<SymbolTableLocation>& sections) const = 0;
};

// Determine the type of the input file and parse it.
Result<std::unique_ptr<SymbolFile>> parse_symbol_file(std::vector<u8> image);

class ElfSymbolFile : public SymbolFile {
public:
	ElfSymbolFile(ElfFile elf);
	
	Result<std::vector<std::unique_ptr<SymbolTable>>> get_all_symbol_tables() const override;
	Result<std::vector<std::unique_ptr<SymbolTable>>> get_symbol_tables_from_sections(
		const std::vector<SymbolTableLocation>& sections) const override;
	
	ElfFile m_elf;
};

class SNDLLSymbolFile : public SymbolFile {
public:
	SNDLLSymbolFile(SNDLLFile sndll);
	
	Result<std::vector<std::unique_ptr<SymbolTable>>> get_all_symbol_tables() const override;
	Result<std::vector<std::unique_ptr<SymbolTable>>> get_symbol_tables_from_sections(
		const std::vector<SymbolTableLocation>& sections) const override;
	
protected:
	SNDLLFile m_sndll;
};

}
