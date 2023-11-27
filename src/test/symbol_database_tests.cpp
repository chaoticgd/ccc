// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <ccc/symbol_database.h>

using namespace ccc;

TEST(CCCSymbolDatabase, Span) {
	struct SpanTestCase {
		std::vector<char> symbols;
		char first;
		char last;
		std::vector<char> to_destroy;
		std::vector<char> expected_output;
	};
	
	static const SpanTestCase test_cases[] = {
		// Single element at the beginning.
		{{'A', 'B', 'C'}, 'A', 'A', {}, {'A'}},
		// Single element in the middle.
		{{'A', 'B', 'C'}, 'B', 'B', {}, {'B'}},
		// Single element at the end.
		{{'A', 'B', 'C'}, 'C', 'C', {}, {'C'}},
		// Iterate over entire range.
		{{'A', 'B', 'C'}, 'A', 'C', {}, {'A', 'B', 'C'}},
		// Symbol at the beginning deleted.
		{{'A', 'B', 'C'}, 'A', 'C', {'A'}, {'B', 'C'}},
		// Symbol in the middle deleted.
		{{'A', 'B', 'C'}, 'A', 'C', {'B'}, {'A', 'C'}},
		// Symbol at the end deleted.
		{{'A', 'B', 'C'}, 'A', 'C', {'C'}, {'A', 'B'}},
		// Entire range deleted.
		{{'A', 'B', 'C'}, 'A', 'C', {'A', 'B', 'C'}, {}}
	};
	
	for(const SpanTestCase& test_case : test_cases) {
		SymbolList<SymbolSource> list;
	
		std::array<SymbolSourceHandle, 256> handles;
		for(char c : test_case.symbols) {
			Result<SymbolSource*> symbol = list.create_symbol(std::string() + c, SymbolSourceHandle());
			CCC_GTEST_FAIL_IF_ERROR(symbol);
			handles[c] = (*symbol)->handle();
		}
		
		for(const char destroy : test_case.to_destroy) {
			EXPECT_TRUE(list.destroy_symbol(handles.at(destroy)));
		}
		
		std::vector<char> names;
		for(SymbolSource& symbol : list.span({handles.at(test_case.first), handles.at(test_case.last)})) {
			names.emplace_back(symbol.name().at(0));
		}
		
		EXPECT_EQ(test_case.expected_output, names);
	}
}

TEST(CCCSymbolDatabase, DestroySymbolsFromSource) {
	SymbolDatabase database;
	
	Result<SymbolSource*> symbol_table_source = database.symbol_sources.create_symbol("Big Symbol Table", SymbolSourceHandle());
	CCC_GTEST_FAIL_IF_ERROR(symbol_table_source);
	SymbolSourceHandle symbol_table_handle = (*symbol_table_source)->handle();
	
	Result<SymbolSource*> user_defined_source = database.symbol_sources.create_symbol("User Defined", SymbolSourceHandle());
	CCC_GTEST_FAIL_IF_ERROR(user_defined_source);
	SymbolSourceHandle user_defined_handle = (*user_defined_source)->handle();
	
	for(s32 i = 0; i < 5; i++) {
		database.data_types.create_symbol("SymbolTableType", symbol_table_handle);
	}
	
	for(s32 i = 0; i < 5; i++) {
		database.data_types.create_symbol("UserDefinedType", user_defined_handle);
	}
	
	for(s32 i = 0; i < 5; i++) {
		database.data_types.create_symbol("SymbolTableType", symbol_table_handle);
	}
	
	for(s32 i = 0; i < 5; i++) {
		database.data_types.create_symbol("UserDefinedType", user_defined_handle);
	}
	
	// Simulate freeing a symbol table while retaining user-defined symbols.
	database.destroy_symbols_from_source(symbol_table_handle);
	
	s32 user_symbols_remaining = 0;
	for(const DataType& data_type : database.data_types) {
		ASSERT_TRUE(data_type.source() == user_defined_handle);
		user_symbols_remaining++;
	}
	
	EXPECT_TRUE(user_symbols_remaining == 10);
}
