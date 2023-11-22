// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include <variant>
#include <unordered_map>

#include "ast.h"
#include "elf.h"

namespace ccc {

// Define an X macro for all the symbol types.

#define CCC_FOR_EACH_SYMBOL_TYPE_DO_X \
	CCC_X(DataType, data_types) \
	CCC_X(Function, functions) \
	CCC_X(GlobalVariable, global_variables) \
	CCC_X(Label, labels) \
	CCC_X(LocalVariable, local_variables) \
	CCC_X(ParameterVariable, parameter_variables) \
	CCC_X(SourceFile, source_files) \
	CCC_X(SymbolSource, symbol_sources)

// Forward declare all the different types of symbol table objects.

#define CCC_X(SymbolType, symbol_list) class SymbolType;
CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X

// Define strongly typed handles for all of the symbol table objects.

template <typename SymbolType>
struct SymbolHandle {
	u32 value = (u32) -1;
	
	SymbolHandle() {}
	SymbolHandle(u32 v) : value(v) {}
	
	bool valid() const { return value != (u32) -1; }
	
	friend auto operator<=>(const SymbolHandle& lhs, const SymbolHandle& rhs) = default;
};

#define CCC_X(SymbolType, symbol_list) using SymbolType##Handle = SymbolHandle<SymbolType>;
CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X

struct SymbolDatabase;

// Define range types for all of the symbol table objects. Note that the last
// member actually points to the last real element in the range. To represent an
// empty range, both symbol handles should be invalid.

template <typename SymbolType>
struct SymbolRange {
	SymbolHandle<SymbolType> first;
	SymbolHandle<SymbolType> last;
	
	bool empty() const {
		return !first.valid() || !last.valid();
	}
	
	bool single() const {
		return !empty() && first == last;
	}
	
	void expand_to_include(SymbolHandle<SymbolType> handle) {
		if(!first.valid()) {
			first = handle;
		}
		CCC_ASSERT(!last.valid() || last.value < handle.value);
		last = handle;
	}
	
	void clear() {
		first = SymbolHandle<SymbolType>();
		last = SymbolHandle<SymbolType>();
	}
	
	friend auto operator<=>(const SymbolRange& lhs, const SymbolRange& rhs) = default;
};

#define CCC_X(SymbolType, symbol_list) using SymbolType##Range = SymbolRange<SymbolType>;
CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X

// A container base class for symbols of a given type that maintains maps of
// their names.

enum SymbolListFlags {
	NO_LIST_FLAGS = 0,
	WITH_ADDRESS_MAP = 1 << 0,
	WITH_NAME_MAP = 1 << 1
};

template <typename SymbolType>
class SymbolList {
public:
	// Lookup symbols from their handles using binary search.
	SymbolType* operator[](SymbolHandle<SymbolType> handle);
	const SymbolType* operator[](SymbolHandle<SymbolType> handle) const;
	SymbolType& at(SymbolHandle<SymbolType> handle);
	const SymbolType& at(SymbolHandle<SymbolType> handle) const;
	
	using Iterator = typename std::vector<SymbolType>::iterator;
	using ConstIterator = typename std::vector<SymbolType>::const_iterator;
	
	// For iterating over all the symbols with a range-based for loop.
	Iterator begin();
	ConstIterator begin() const;
	Iterator end();
	ConstIterator end() const;
	
	// For iterating over a subset of the symbols with a range-based for loop.
	std::span<SymbolType> span(SymbolRange<SymbolType> range);
	std::span<const SymbolType> span(SymbolRange<SymbolType> range) const;
	
	bool empty() const;
	
	Result<SymbolType*> create_symbol(std::string name, SymbolSourceHandle source, Address address = Address());
	bool destroy_symbol(SymbolHandle<SymbolType> handle);
	u32 destroy_symbols(SymbolRange<SymbolType> range);
	
	using AddressToHandleMap = std::unordered_map<u32, SymbolHandle<SymbolType>>;
	using NameToHandleMap = std::unordered_multimap<std::string, SymbolHandle<SymbolType>>;
	
	// This lets us use range-based for loops to iterate over all symbols with
	// a given name.
	struct NameMapIterators {
		typename NameToHandleMap::const_iterator begin_iterator;
		typename NameToHandleMap::const_iterator end_iterator;
		typename NameToHandleMap::const_iterator begin() const { return begin_iterator; }
		typename NameToHandleMap::const_iterator end() const { return end_iterator; }
	};
	
	SymbolHandle<SymbolType> handle_from_address(Address address) const;
	NameMapIterators handles_from_name(const char* name) const;
	
protected:
	
	// Do a binary search for a handle, and return either its index, or the
	// index where it could be inserted.
	size_t binary_search(SymbolHandle<SymbolType> handle) const;
	
	std::vector<SymbolType> m_symbols;
	u32 m_next_handle = 0;
	AddressToHandleMap m_address_to_handle;
	NameToHandleMap m_name_to_handle;
};

enum ShouldDeleteOldSymbols {
	DONT_DELETE_OLD_SYMBOLS,
	DELETE_OLD_SYMBOLS
};

// Base class for all the symbols.

class Symbol {
	template <typename SymbolType>
	friend class SymbolList;
public:
	SymbolSourceHandle source() const { return m_source; }
	const std::string& name() const { return m_name; }
	
	ast::Node& type() { CCC_ASSERT(m_type.get()); return *m_type; }
	const ast::Node& type() const { CCC_ASSERT(m_type.get()); return *m_type; }
	ast::Node* type_ptr() { return m_type.get(); }
	const ast::Node* type_ptr() const { return m_type.get(); }
	
	void set_type(std::unique_ptr<ast::Node> type) {
		CCC_ASSERT(!m_type.get());
		m_type = std::move(type);
	}
	
	void set_type_and_invalidate_node_handles(std::unique_ptr<ast::Node> type) {
		m_type = std::move(type);
	}
	
protected:
	u32 m_handle = (u32) -1;
	SymbolSourceHandle m_source;
	std::string m_name;
	std::unique_ptr<ast::Node> m_type;
};

// Base class for variable symbols.

class Variable : public Symbol {
public:
	enum Class {
		GLOBAL,
		LOCAL,
		PARAMETER
	};
	
	struct GlobalStorage {
		enum Location {
			NIL,
			DATA,
			BSS,
			ABS,
			SDATA,
			SBSS,
			RDATA,
			COMMON,
			SCOMMON
		};
		
		Location location = Location::NIL;
		Address address;
		
		static const char* location_to_string(Location location);
		
		GlobalStorage() {}
		friend auto operator<=>(const GlobalStorage& lhs, const GlobalStorage& rhs) = default;
	};

	struct RegisterStorage {
		s32 dbx_register_number = -1;
		bool is_by_reference;
		
		RegisterStorage() {}
		friend auto operator<=>(const RegisterStorage& lhs, const RegisterStorage& rhs) = default;
	};

	struct StackStorage {
		s32 stack_pointer_offset = -1;
		
		StackStorage() {}
		friend auto operator<=>(const StackStorage& lhs, const StackStorage& rhs) = default;
	};
	
	using Storage = std::variant<GlobalStorage, RegisterStorage, StackStorage>;
	
	Class variable_class;
	Storage storage;
	AddressRange live_range;
	std::unique_ptr<ast::Node> data;
};

// All the different types of symbol objects.

class DataType : public Symbol {
	friend SymbolList<DataType>;
public:
	DataTypeHandle handle() const { return m_handle; }
	
	std::vector<SourceFileHandle> files; // List of files for which a given top-level type is present.
	const char* compare_fail_reason = "";
	
	bool probably_defined_in_cpp_file : 1 = false;
	bool conflict : 1 = false;
	
	static constexpr const char* SYMBOL_TYPE_NAME = "data type";
	static constexpr const u32 LIST_FLAGS = WITH_NAME_MAP;
};

class Function : public Symbol {
	friend SourceFile;
	friend SymbolList<Function>;
public:
	FunctionHandle handle() const { return m_handle; }
	SourceFileHandle source_file() const { return m_source_file; }
	
	ParameterVariableRange parameter_variables() const { return m_parameter_variables; }
	void set_parameter_variables(ParameterVariableRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database);
	LocalVariableRange local_variables() const { return m_local_variables; }
	void set_local_variables(LocalVariableRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database);
	
	Address address() const { return m_address; }
	
	struct Parameter {
		std::string name;
		Variable variable;
	};
	
	struct Local {
		std::string name;
		Variable variable;
	};
	
	struct LineNumberPair {
		Address address;
		s32 line_number;
	};

	struct SubSourceFile {
		Address address;
		std::string relative_path;
	};
	
	u32 size = 0;
	std::string relative_path;
	ast::StorageClass storage_class;
	std::vector<LineNumberPair> line_numbers;
	std::vector<SubSourceFile> sub_source_files;
	
	static constexpr const char* SYMBOL_TYPE_NAME = "function";
	static constexpr const u32 LIST_FLAGS = WITH_ADDRESS_MAP;
	
protected:
	SourceFileHandle m_source_file;
	ParameterVariableRange m_parameter_variables;
	LocalVariableRange m_local_variables;
	
	Address m_address;
};

class GlobalVariable : public Variable {
	friend SourceFile;
	friend SymbolList<GlobalVariable>;
public:
	GlobalVariableHandle handle() const { return m_handle; }
	Address address() const { return m_address; }
	SourceFileHandle source_file() const { return m_source_file; };
	
	static constexpr const char* SYMBOL_TYPE_NAME = "global variable";
	static constexpr u32 LIST_FLAGS = WITH_ADDRESS_MAP;
	
protected:
	Address m_address;
	SourceFileHandle m_source_file;
};

class Label : public Symbol {
	friend SymbolList<Label>;
public:
	LabelHandle handle() const { return m_handle; }
	Address address() const { return m_address; }
	
	static constexpr const char* SYMBOL_TYPE_NAME = "label";
	static constexpr u32 LIST_FLAGS = WITH_ADDRESS_MAP;
	
protected:
	Address m_address;
};

class LocalVariable : public Variable {
	friend Function;
	friend SymbolList<LocalVariable>;
public:
	LocalVariableHandle handle() const { return m_handle; }
	FunctionHandle function() const { return m_function; };
	
	static constexpr const char* SYMBOL_TYPE_NAME = "local variable";
	static constexpr u32 LIST_FLAGS = NO_LIST_FLAGS;
	
protected:
	FunctionHandle m_function;
};

class ParameterVariable : public Variable {
	friend Function;
	friend SymbolList<ParameterVariable>;
public:
	ParameterVariableHandle handle() const { return m_handle; }
	FunctionHandle function() const { return m_function; };
	
	static constexpr const char* SYMBOL_TYPE_NAME = "parameter variable";
	static constexpr u32 LIST_FLAGS = NO_LIST_FLAGS;
	
protected:
	FunctionHandle m_function;
};

class SourceFile : public Symbol {
	friend SymbolList<SourceFile>;
public:
	SourceFileHandle handle() const { return m_handle; }
	const std::string& full_path() const { return name(); }
	FunctionRange functions() const { return m_functions; }
	GlobalVariableRange globals_variables() const { return m_globals_variables; }
	
	void set_functions(FunctionRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database);
	void set_globals_variables(GlobalVariableRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database);
	
	std::string relative_path;
	Address text_address = 0;
	std::map<StabsTypeNumber, DataTypeHandle> stabs_type_number_to_handle;
	std::set<std::string> toolchain_version_info;
	
	static constexpr const char* SYMBOL_TYPE_NAME = "source file";
	static constexpr u32 LIST_FLAGS = NO_LIST_FLAGS;
	
protected:
	FunctionRange m_functions;
	GlobalVariableRange m_globals_variables;
};

class SymbolSource : public Symbol {
	friend SymbolList<SymbolSource>;
public:
	SymbolSourceHandle handle() const { return m_handle; }
	
	enum Type {
		ANALYSIS,
		SYMBOL_TABLE,
		USER_DEFINED
	};
	
	Type source_type;
	
	static constexpr const char* SYMBOL_TYPE_NAME = "symbol source";
	static constexpr u32 LIST_FLAGS = NO_LIST_FLAGS;
};

struct SymbolDatabase {
	SymbolList<DataType> data_types;
	SymbolList<Function> functions;
	SymbolList<GlobalVariable> global_variables;
	SymbolList<Label> labels;
	SymbolList<LocalVariable> local_variables;
	SymbolList<ParameterVariable> parameter_variables;
	SymbolList<SourceFile> source_files;
	SymbolList<SymbolSource> symbol_sources;
	
	// Lookup a type by its STABS type number. If that fails, optionally try to
	// lookup the type by its name. On success return a handle to the type,
	// otherwise return an invalid handle.
	DataTypeHandle lookup_type(const ast::TypeName& type_name, bool fallback_on_name_lookup) const;
	
	[[nodiscard]] Result<DataType*> create_data_type_if_unique(std::unique_ptr<ast::Node> node, const char* name, SourceFile& source_file, SymbolSourceHandle source);
};

}
