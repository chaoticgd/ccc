// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "dwarf_section.h"
#include "symbol_database.h"

namespace ccc::dwarf {

class SymbolTableImporter {
public:
	SymbolTableImporter(
		SymbolDatabase& database,
		const SectionReader& dwarf,
		u32 importer_flags,
		const DemanglerFunctions& demangler,
		const std::atomic_bool* interrupt);
	
	// Import a DWARF symbol table into the symbol database, excluding
	// compilation units associated with an overlay.
	Result<void> import_symbol_table(SymbolGroup group);
	
	// Import a DWARF symbol table into the symbol database, but only including
	// compilation units associated with the specified overlay.
	Result<void> import_overlay(u32 overlay_id, SymbolGroup group);
	
protected:
	Result<void> import_compile_units(std::optional<u32> overlay_id, SymbolGroup group);
	Result<void> import_compile_unit(const DIE& die);
	Result<void> import_data_type(const DIE& die);
	Result<void> import_subroutine(const DIE& die);
	
	SymbolDatabase& m_database;
	const SectionReader& m_dwarf;
	u32 m_importer_flags;
	const DemanglerFunctions& m_demangler;
	const std::atomic_bool* m_interrupt;
	
	SymbolGroup m_group;
	SourceFile* m_source_file = nullptr;
};

struct OverlayInfo {
	u32 id;
	std::string name;
};

// Enumerate all the overlays defined in the symbol table. The ID values
// provided can then be fed into the import_overlay function above.
Result<std::vector<OverlayInfo>> enumerate_overlays(const SectionReader& dwarf);

}
