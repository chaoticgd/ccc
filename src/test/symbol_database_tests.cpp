// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <ccc/symbol_database.h>

using namespace ccc;

TEST(CCCSymbolDatabase, Span) {
	struct SpanTestCase {
		s32 symbol_count;
		s32 first;
		s32 last;
		std::vector<s32> to_destroy;
		std::vector<std::string> expected_output;
	};
	
	static const SpanTestCase test_cases[] = {
		// Single element at the beginning.
		{3, 1, 1, {}, {"1"}},
		// Single element in the middle.
		{3, 2, 2, {}, {"2"}},
		// Single element at the end.
		{3, 3, 3, {}, {"3"}},
		// Iterate over entire range.
		{3, 1, 3, {}, {"1", "2", "3"}},
		// Symbol at the beginning deleted.
		{3, 1, 3, {1}, {"2", "3"}},
		// Symbol in the middle deleted.
		{3, 1, 3, {2}, {"1", "3"}},
		// Symbol at the end deleted.
		{3, 1, 3, {3}, {"2", "3"}},
		// Entire range deleted.
		{3, 1, 3, {1, 2, 3}, {}}
	};
	
	for(const SpanTestCase& test_case : test_cases) {
		SymbolDatabase database;
	
		std::vector<SymbolSourceHandle> handles(test_case.symbol_count);
		for(s32 i = 0; i < test_case.symbol_count; i++) {
			Result<SymbolSource*> symbol = database.symbol_sources.create_symbol(std::to_string(i + 1), SymbolSourceHandle());
			CCC_GTEST_FAIL_IF_ERROR(symbol);
			handles[i] = (*symbol)->handle();
		}
		
		for(s32 destroy : test_case.to_destroy) {
			database.symbol_sources.destroy_symbol(handles.at(destroy - 1));
		}
		
		std::vector<std::string> names;
		for(SymbolSource& symbol : database.symbol_sources.span({handles.at(test_case.first - 1), handles.at(test_case.last - 1)})) {
			names.emplace_back(symbol.name());
		}
		
		EXPECT_EQ(test_case.expected_output, names);
	}
}
