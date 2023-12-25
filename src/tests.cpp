// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

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
	if(result != 0) {
		return result;
	}
	
	if(argc != 2) {
		return 1;
	}
	
	return main_test(std::string(argv[1]));
}

static int main_test(const fs::path& input_directory)
{
	CCC_CHECK_FATAL(fs::is_directory(input_directory), "Input path is not a directory.");
	
	for(auto entry : fs::recursive_directory_iterator(input_directory)) {
		if(entry.is_regular_file()) {
			printf("%s ", entry.path().string().c_str());
			
			Result<std::vector<u8>> image = platform::read_binary_file(entry.path());
			CCC_EXIT_IF_ERROR(image);
			
			Result<SymbolFile> symbol_file = parse_symbol_file(*image);
			if(symbol_file.success()) {
				SymbolDatabase database;
				
				SymbolTableConfig importer_config;
				importer_config.importer_flags = STRICT_PARSING;
				importer_config.demangler.cplus_demangle = cplus_demangle;
				importer_config.demangler.cplus_demangle_opname = cplus_demangle_opname;
				
				Result<SymbolSourceHandle> handle = import_symbol_table(database, *symbol_file, importer_config);
				CCC_EXIT_IF_ERROR(handle);
			} else {
				printf("%s", symbol_file.error().message.c_str());
			}
			
			printf("\n");
		}
	}
	
	return 0;
}
