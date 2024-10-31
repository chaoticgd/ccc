// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "ccc/ccc.h"

using namespace ccc;

extern "C" int LLVMFuzzerTestOneInput(const u8* data, size_t size)
{
	std::vector<u8> image(data, data + size);
	Result<std::unique_ptr<SymbolFile>> symbol_file =
		parse_symbol_file(std::move(image), "totallyrealvideogame.elf");
	if (!symbol_file.success()) {
		return 0;
	}
	
	Result<std::vector<std::unique_ptr<SymbolTable>>> symbol_tables =
		(*symbol_file)->get_all_symbol_tables();
	if (!symbol_tables.success()) {
		return 0;
	}
	
	SymbolDatabase database;
	
	DemanglerFunctions demangler; // Don't fuzz the demangler.
	
	Result<ModuleHandle> module_handle = import_symbol_tables(
		database,
		(*symbol_file)->name(),
		*symbol_tables,
		NO_IMPORTER_FLAGS,
		demangler,
		nullptr);
	
	return 0;
}
