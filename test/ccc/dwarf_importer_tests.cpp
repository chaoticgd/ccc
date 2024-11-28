// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "ccc/dwarf_forge.h"
#include "ccc/dwarf_importer.h"
#include "ccc/dwarf_printer.h"
#include "ccc/importer_flags.h"

using namespace ccc;
using namespace ccc::dwarf;

//#define VERBOSE_DWARF_TESTING

static Result<SymbolDatabase> import_test_dwarf_symbol_table(Forge& forge)
{
	std::vector<u8> debug = forge.finish();
	std::vector<u8> line;
	
	SectionReader reader(debug, line, STRICT_PARSING);
	
#ifdef VERBOSE_DWARF_TESTING
		SymbolPrinter printer(reader);
		Result<void> print_result = printer.print_dies(stdout, *reader.first_die(), 0);
		CCC_RETURN_IF_ERROR(print_result);
#endif
	
	SymbolDatabase database;
	DemanglerFunctions demangler;
	SymbolTableImporter importer(database, reader, STRICT_PARSING, demangler, nullptr);
	
	Result<SymbolSource*> source = database.symbol_sources.create_symbol("Test Source", SymbolSourceHandle(), nullptr);
	CCC_RETURN_IF_ERROR(source);
	
	SymbolGroup group;
	group.source = (*source)->handle();
	
	Result<void> import_result = importer.import_symbol_table(group);
	CCC_RETURN_IF_ERROR(import_result);
	
	return database;
}

#define DWARF_IMPORTER_TEST(name, recipe) \
	static void dwarf_importer_test_##name(SymbolDatabase& database); \
	TEST(CCCDwarf, name) \
	{ \
		Forge forge; \
		recipe(); \
		Result<SymbolDatabase> database = import_test_dwarf_symbol_table(forge); \
		CCC_GTEST_FAIL_IF_ERROR(database); \
		dwarf_importer_test_##name(*database); \
	} \
	static void dwarf_importer_test_##name(SymbolDatabase& database)

DWARF_IMPORTER_TEST(Test,
	([&]() {
		forge.begin_die("source1", TAG_compile_unit);
		forge.string(AT_name, "gold.c");
		forge.end_die();
		forge.begin_children();
			forge.begin_die("func", TAG_global_subroutine);
			forge.end_die();
		forge.end_children();
		
		forge.begin_die("source2", TAG_compile_unit);
		forge.string(AT_name, "sapphire.c");
		forge.end_die();
	}))
{
	EXPECT_EQ(database.source_files.size(), 2);
}
