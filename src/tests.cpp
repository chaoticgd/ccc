// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <gtest/gtest.h>

#include "ccc/ccc.h"
#include "platform/file.h"
#define HAVE_DECL_BASENAME 1
#include "demangle.h"

using namespace ccc;

static int main_test(const fs::path& input_directory);

int main(int argc, char** argv)
{
	testing::InitGoogleTest(&argc, argv);
	int result = RUN_ALL_TESTS();
	if (result != 0) {
		return result;
	}
	
	if (argc != 2) {
		return 1;
	}
	
	return main_test(std::string(argv[1]));
}

#ifdef _WIN32
	const char* compressor = "NUL";
#else
	const char* compressor = "/dev/null";
#endif

static int main_test(const fs::path& input_directory)
{
	CCC_EXIT_IF_FALSE(fs::is_directory(input_directory), "Input path is not a directory.");
	
	for (auto entry : fs::recursive_directory_iterator(input_directory)) {
		if (entry.is_regular_file()) {
			printf("%s ", entry.path().string().c_str());
			fflush(stdout);
			
			Result<std::vector<u8>> image = platform::read_binary_file(entry.path());
			CCC_EXIT_IF_ERROR(image);
			
			Result<std::unique_ptr<SymbolFile>> symbol_file = parse_symbol_file(*image, entry.path().filename().string());
			if (symbol_file.success()) {
				SymbolDatabase database;
				
				Result<std::vector<std::unique_ptr<SymbolTable>>> symbol_tables = (*symbol_file)->get_all_symbol_tables();
				CCC_EXIT_IF_ERROR(symbol_tables);
				
				DemanglerFunctions demangler;
				demangler.cplus_demangle = cplus_demangle;
				demangler.cplus_demangle_opname = cplus_demangle_opname;
				
				// STRICT_PARSING makes it so we treat more types of errors as
				// fatal. The other two flags make it so that we can test
				// removing undesirable symbols.
				u32 importer_flags = NO_OPTIMIZED_OUT_FUNCTIONS | STRICT_PARSING | UNIQUE_FUNCTIONS;
				
				// Test the importers.
				Result<ModuleHandle> handle = import_symbol_tables(
					database, (*symbol_file)->name(), *symbol_tables, importer_flags, demangler, nullptr);
				CCC_EXIT_IF_ERROR(handle);
				
				// Test the C++ printing code.
				FILE* black_hole = fopen(compressor, "w");
				CppPrinterConfig printer_config;
				CppPrinter printer(black_hole, printer_config);
				for (const DataType& data_type : database.data_types) {
					printer.data_type(data_type, database);
				}
				for (const Function& function : database.functions) {
					printer.function(function, database, nullptr);
				}
				for (const GlobalVariable& global_variable : database.global_variables) {
					printer.global_variable(global_variable, database, nullptr);
				}
				fclose(black_hole);
				
				// Test the JSON writing code.
				rapidjson::StringBuffer buffer;
				JsonWriter writer(buffer);
				write_json(writer, database, "test");
			} else {
				printf("%s", symbol_file.error().message.c_str());
			}
			
			printf("\n");
		}
	}
	
	return 0;
}
