// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <ccc/symbol_database.h>

using namespace ccc;

TEST(CCCSymbolDatabase, SymbolFromHandle) {
	SymbolDatabase database;
	SymbolSourceHandle handles[10];
	
	// Create the symbols.
	for(s32 i = 0; i < 10; i++) {
		Result<SymbolSource*> source = database.symbol_sources.create_symbol(std::to_string(i), SymbolSourceHandle());
		CCC_GTEST_FAIL_IF_ERROR(source);
		handles[i] = (*source)->handle();
	}
	
	// Make sure we can still look them up.
	for(s32 i = 0; i < 10; i++) {
		SymbolSource* source = database.symbol_sources.symbol_from_handle(handles[i]);
		ASSERT_TRUE(source);
		ASSERT_TRUE(source->name() == std::to_string(i));
	}
}

TEST(CCCSymbolDatabase, SymbolListSpan) {
	struct SpanTestCase {
		std::vector<u8> symbols;
		u8 first;
		u8 last;
		std::vector<u8> to_destroy;
		std::vector<u8> expected_output;
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
		SymbolSourceHandle handles[256];
		
		for(u8 c : test_case.symbols) {
			Result<SymbolSource*> symbol = list.create_symbol(std::string() + (char) c, SymbolSourceHandle());
			CCC_GTEST_FAIL_IF_ERROR(symbol);
			handles[c] = (*symbol)->handle();
		}
		
		for(const u8 destroy : test_case.to_destroy) {
			EXPECT_TRUE(list.destroy_symbol(handles[destroy]));
		}
		
		std::vector<u8> names;
		for(SymbolSource& symbol : list.span({handles[test_case.first], handles[test_case.last]})) {
			names.emplace_back((u8) symbol.name().at(0));
		}
		
		EXPECT_EQ(test_case.expected_output, names);
	}
}

TEST(CCCSymbolDatabase, HandleFromAddress) {
	SymbolDatabase database;
	FunctionHandle handles[10];
	Result<SymbolSource*> source = database.symbol_sources.create_symbol("Source", SymbolSourceHandle());
	
	// Create the symbols.
	for(u32 address = 0; address < 10; address++) {
		Result<Function*> function = database.functions.create_symbol("", (*source)->handle(), address);
		CCC_GTEST_FAIL_IF_ERROR(function);
		handles[address] = (*function)->handle();
	}
	
	// Make sure we can look them up by their address.
	for(u32 address = 0; address < 10; address++) {
		ASSERT_TRUE(database.functions.handle_from_address(address) == handles[address]);
	}
}

TEST(CCCSymbolDatabase, HandlesFromName) {
	SymbolDatabase database;
	Result<SymbolSource*> source = database.symbol_sources.create_symbol("Source", SymbolSourceHandle());
	
	// Create the symbols.
	Result<DataType*> a = database.data_types.create_symbol("A", (*source)->handle());
	Result<DataType*> b_1 = database.data_types.create_symbol("B", (*source)->handle());
	Result<DataType*> b_2 = database.data_types.create_symbol("B", (*source)->handle());
	Result<DataType*> c_1 = database.data_types.create_symbol("C", (*source)->handle());
	Result<DataType*> c_2 = database.data_types.create_symbol("C", (*source)->handle());
	Result<DataType*> c_3 = database.data_types.create_symbol("C", (*source)->handle());
	Result<DataType*> d = database.data_types.create_symbol("D", (*source)->handle());
	
	// Destroy D.
	database.data_types.destroy_symbol((*d)->handle());
	
	// Make sure we can look up A, B, and C by their names.
	auto as = database.data_types.handles_from_name("A");
	EXPECT_EQ(++as.begin(), as.end());
	
	auto bs = database.data_types.handles_from_name("B");
	EXPECT_EQ(++(++bs.begin()), bs.end());
	
	auto cs = database.data_types.handles_from_name("C");
	EXPECT_EQ(++(++(++(cs.begin()))), cs.end());
	
	// Make sure we can't look up D anymore.
	auto ds = database.data_types.handles_from_name("D");
	EXPECT_EQ(ds.begin(), ds.end());
}

TEST(CCCSymbolDatabase, DestroySymbolsDanglingHandles) {
	SymbolDatabase database;
	SymbolSourceHandle handles[10];
	
	// Create the symbols.
	for(s32 i = 0; i < 10; i++) {
		Result<SymbolSource*> source = database.symbol_sources.create_symbol(std::to_string(i), SymbolSourceHandle());
		CCC_GTEST_FAIL_IF_ERROR(source);
		handles[i] = (*source)->handle();
	}
	
	// Destroy every other symbol.
	for(s32 i = 0; i < 10; i += 2) {
		database.symbol_sources.destroy_symbol(handles[i]);
	}
	
	// Make sure we can't look them up anymore.
	for(s32 i = 0; i < 10; i += 2) {
		EXPECT_FALSE(database.symbol_sources.symbol_from_handle(handles[i]));
	}
	
	// Make sure we can still lookup the other ones.
	for(s32 i = 1; i < 10; i += 2) {
		EXPECT_TRUE(database.symbol_sources.symbol_from_handle(handles[i]));
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

TEST(CCCSymbolDatabase, NodeHandleLookup) {
	SymbolDatabase database;
	
	Result<SymbolSource*> source = database.symbol_sources.create_symbol("Symbol Table", SymbolSourceHandle());
	CCC_GTEST_FAIL_IF_ERROR(source);
	
	Result<DataType*> data_type = database.data_types.create_symbol("DataType", (*source)->handle());
	CCC_GTEST_FAIL_IF_ERROR(data_type);
	
	std::unique_ptr<ast::BuiltIn> node = std::make_unique<ast::BuiltIn>();
	(*data_type)->set_type_and_invalidate_node_handles(std::move(node));
	
	NodeHandle node_handle((*data_type)->handle(), (*data_type)->type());
	
	// Make sure we can lookup the node from the handle.
	EXPECT_EQ(database.node_handle_to_pointer(node_handle), (*data_type)->type());
	
	// Destroy the symbol.
	database.data_types.destroy_symbol((*data_type)->handle());
	
	// Make sure we can no longer lookup the node from the handle.
	EXPECT_EQ(database.node_handle_to_pointer(node_handle), nullptr);
}
