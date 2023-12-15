// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include <variant>
#include <unordered_map>

#include "util.h"

namespace ccc {

// These are used to reference STABS types from other types within a single
// translation unit. For most games these will just be a single number, the type
// number. In some cases, for example with the homebrew SDK, type numbers are a
// pair of two numbers surrounded by round brackets e.g. (1,23) where the first
// number is the index of the include file to use (includes are listed for each
// translation unit separately), and the second number is the type number.
struct StabsTypeNumber {
	s32 file = -1;
	s32 type = -1;
	
	friend auto operator<=>(const StabsTypeNumber& lhs, const StabsTypeNumber& rhs) = default;
};

namespace ast {

struct Node;

enum StorageClass {
	SC_NONE = 0,
	SC_TYPEDEF = 1,
	SC_EXTERN = 2,
	SC_STATIC = 3,
	SC_AUTO = 4,
	SC_REGISTER = 5
};
	
};

// Define an X macro for all the symbol types.

#define CCC_FOR_EACH_SYMBOL_TYPE_DO_X \
	CCC_X(DataType, data_types) \
	CCC_X(Function, functions) \
	CCC_X(GlobalVariable, global_variables) \
	CCC_X(Label, labels) \
	CCC_X(LocalVariable, local_variables) \
	CCC_X(ParameterVariable, parameter_variables) \
	CCC_X(Section, sections) \
	CCC_X(SourceFile, source_files) \
	CCC_X(SymbolSource, symbol_sources)

// Define an enum for all the symbol types.

enum class SymbolDescriptor {
	DATA_TYPE,
	FUNCTION,
	GLOBAL_VARIABLE,
	LABEL,
	LOCAL_VARIABLE,
	PARAMETER_VARIABLE,
	SECTION,
	SOURCE_FILE,
	SYMBOL_SOURCE
};

// Forward declare all the different types of symbol table objects.

#define CCC_X(SymbolType, symbol_list) class SymbolType;
CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X

class SymbolDatabase;

// Define strongly typed handles for all of the symbol table objects. These are
// here to solve the problem of dangling references to symbols.

template <typename SymbolType>
struct SymbolHandle {
	u32 value = (u32) -1;
	
	SymbolHandle() {}
	SymbolHandle(u32 v) : value(v) {}
	
	// Check if this symbol handle has been initialised. Note that this doesn't
	// determine whether or not the symbol it points to has been deleted!
	bool valid() const { return value != (u32) -1; }
	
	friend auto operator<=>(const SymbolHandle& lhs, const SymbolHandle& rhs) = default;
};

#define CCC_X(SymbolType, symbol_list) using SymbolType##Handle = SymbolHandle<SymbolType>;
CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X

// Define a strongly typed handle to an AST node.

class NodeHandle {
	friend SymbolDatabase;
public:
	template <typename SymbolType>
	NodeHandle(SymbolHandle<SymbolType> symbol_handle, const ast::Node* node)
		: m_descriptor(SymbolType::DESCRIPTOR)
		, m_symbol_handle(symbol_handle.value)
		, m_node(node) {}
	
	friend auto operator<=>(const NodeHandle& lhs, const NodeHandle& rhs) = default;
	
protected:
	SymbolDescriptor m_descriptor;
	u32 m_symbol_handle;
	const ast::Node* m_node;
};

// Define range types for all of the symbol table objects. Note that the last
// member actually points to the last real element in the range.

template <typename SymbolType>
struct SymbolRange {
	SymbolHandle<SymbolType> first;
	SymbolHandle<SymbolType> last;
	
	SymbolRange() {}
	SymbolRange(SymbolHandle<SymbolType> handle)
		: first(handle), last(handle) {}
	SymbolRange(SymbolHandle<SymbolType> f, SymbolHandle<SymbolType> l)
		: first(f), last(l) {}
	
	bool valid() const {
		return first.valid() && last.valid();
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

enum SymbolFlags {
	NO_SYMBOL_FLAGS = 0,
	WITH_ADDRESS_MAP = 1 << 0,
	WITH_NAME_MAP = 1 << 1
};

template <typename SymbolType>
class SymbolList {
public:
	// Lookup symbols from their handles using binary search.
	SymbolType* symbol_from_handle(SymbolHandle<SymbolType> handle);
	const SymbolType* symbol_from_handle(SymbolHandle<SymbolType> handle) const;
	
	s32 index_from_handle(SymbolHandle<SymbolType> handle) const;
	
	using Iterator = typename std::vector<SymbolType>::iterator;
	using ConstIterator = typename std::vector<SymbolType>::const_iterator;
	
	// For iterating over all the symbols.
	Iterator begin();
	ConstIterator begin() const;
	Iterator end();
	ConstIterator end() const;
	
	// For iterating over a subset of the symbols.
	std::span<SymbolType> span(SymbolRange<SymbolType> range);
	std::span<const SymbolType> span(SymbolRange<SymbolType> range) const;
	std::span<SymbolType> span(std::optional<SymbolRange<SymbolType>> range);
	std::span<const SymbolType> span(std::optional<SymbolRange<SymbolType>> range) const;
	
	using AddressToHandleMap = std::unordered_multimap<u32, SymbolHandle<SymbolType>>;
	using NameToHandleMap = std::unordered_multimap<std::string, SymbolHandle<SymbolType>>;
	
	// For iterating over all the symbols with a given name.
	template <typename Iterator>
	class Iterators {
	public:
		Iterators(Iterator b, Iterator e)
			: m_begin(b), m_end(e) {}
		Iterator begin() const { return m_begin; }
		Iterator end() const { return m_end; }
	protected:
		Iterator m_begin;
		Iterator m_end;
	};
	
	using AddressToHandleMapIterators = Iterators<typename AddressToHandleMap::const_iterator>;
	using NameToHandleMapIterators = Iterators<typename NameToHandleMap::const_iterator>;
	
	AddressToHandleMapIterators handles_from_address(Address address) const;
	NameToHandleMapIterators handles_from_name(const std::string& name) const;
	
	SymbolHandle<SymbolType> first_handle_from_address(Address address) const;
	SymbolHandle<SymbolType> first_handle_from_name(const std::string& name) const;
	
	bool empty() const;
	s32 size() const;
	
	// Create a new symbol. If it's a SymbolSource symbol, source can be left
	// empty, otherwise it has to be valid.
	Result<SymbolType*> create_symbol(std::string name, SymbolSourceHandle source, Address address = Address());
	
	// Update the address of a symbol without changing its handle.
	bool move_symbol(SymbolHandle<SymbolType> handle, Address new_address);
	
	// Update the name of a symbol without changing its handle.
	bool rename_symbol(SymbolHandle<SymbolType> handle, std::string new_name);
	
	// Destroy a single symbol.
	bool destroy_symbol(SymbolHandle<SymbolType> handle);
	
	// Destroy all the symbols in a given range.
	u32 destroy_symbols(SymbolRange<SymbolType> range);
	
	// Destroy all the symbols from a given symbol source. For example, you can
	// use this to free a symbol table without destroying user-defined symbols.
	void destroy_symbols_from_source(SymbolSourceHandle source);
	
	// Destroy all symbols, but don't reset m_next_handle so we don't have to
	// worry about dangling handles.
	void clear();
	
protected:
	// Do a binary search for a handle, and return either its index, or the
	// index where it could be inserted.
	size_t binary_search(SymbolHandle<SymbolType> handle) const;
	
	// Destroy a range of symbols given indices.
	u32 destroy_symbols_impl(size_t begin_index, size_t end_index);
	
	// Keep the address map in sync with the symbol list.
	void link_address_map(SymbolType& symbol);
	void unlink_address_map(SymbolType& symbol);
	
	// Keep the name map in sync with the symbol list.
	void link_name_map(SymbolType& symbol);
	void unlink_name_map(SymbolType& symbol);
	
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
	Symbol();
	Symbol(const Symbol& rhs) = delete;
	Symbol(Symbol&& rhs);
	~Symbol();
	Symbol& operator=(const Symbol& rhs) = delete;
	Symbol& operator=(Symbol&& rhs);
	
	const std::string& name() const { return m_name; }
	u32 raw_handle() const { return m_handle; }
	SymbolSourceHandle source() const { return m_source; }
	
	ast::Node* type() { return m_type; }
	const ast::Node* type() const { return m_type; }
	
	void set_type_once(std::unique_ptr<ast::Node> type) {
		CCC_ASSERT(!m_type);
		m_type = type.release();
	}
	
	// DANGER: Accessing a node handle that was pointing into this symbol after
	// this call is a use after free.
	void set_type_and_invalidate_node_handles(std::unique_ptr<ast::Node> type) {
		m_type = type.release();
	}
	
protected:
	u32 m_handle = (u32) -1;
	SymbolSourceHandle m_source;
	std::string m_name;
	ast::Node* m_type = nullptr;
};

// Variable storage types. This is different to whether the variable is a
// global, local or parameter. For example local variables can have global
// storage (static locals).

enum GlobalStorageLocation {
	NIL,
	DATA,
	BSS,
	ABS,
	SDATA,
	SBSS,
	RDATA,
	COMMON,
	SCOMMON,
	SUNDEFINED
};

const char* global_storage_location_to_string(GlobalStorageLocation location);

struct GlobalStorage {
	GlobalStorageLocation location = GlobalStorageLocation::NIL;
	
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

// All the different types of symbol objects.

class DataType : public Symbol {
	friend SourceFile;
	friend SymbolList<DataType>;
public:
	static constexpr const SymbolDescriptor DESCRIPTOR = SymbolDescriptor::DATA_TYPE;
	static constexpr const char* NAME = "data type";
	static constexpr const u32 FLAGS = WITH_NAME_MAP;
	
	DataTypeHandle handle() const { return m_handle; }
	
	std::vector<SourceFileHandle> files; // List of files for which a given top-level type is present.
	const char* compare_fail_reason = nullptr;
	
	bool probably_defined_in_cpp_file : 1 = false;
};

class Function : public Symbol {
	friend SourceFile;
	friend SymbolList<Function>;
public:
	static constexpr const SymbolDescriptor DESCRIPTOR = SymbolDescriptor::FUNCTION;
	static constexpr const char* NAME = "function";
	static constexpr const u32 FLAGS = WITH_ADDRESS_MAP | WITH_NAME_MAP;
	
	FunctionHandle handle() const { return m_handle; }
	SourceFileHandle source_file() const { return m_source_file; }
	
	std::optional<ParameterVariableRange> parameter_variables() const { return m_parameter_variables; }
	void set_parameter_variables(std::optional<ParameterVariableRange> range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database);
	
	std::optional<LocalVariableRange> local_variables() const { return m_local_variables; }
	void set_local_variables(std::optional<LocalVariableRange> range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database);
	
	Address address() const { return m_address; }
	
	const std::string& mangled_name() const;
	const void set_mangled_name(std::string mangled);
	
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
	bool is_member_function_ish = false; // Filled in by fill_in_pointers_to_member_function_definitions.
	
protected:
	Address& address_ref() { return m_address; }
	
	SourceFileHandle m_source_file;
	std::optional<ParameterVariableRange> m_parameter_variables;
	std::optional<LocalVariableRange> m_local_variables;
	
	Address m_address;
	std::string m_mangled_name;
};

class GlobalVariable : public Symbol {
	friend SourceFile;
	friend SymbolList<GlobalVariable>;
public:
	static constexpr const SymbolDescriptor DESCRIPTOR = SymbolDescriptor::GLOBAL_VARIABLE;
	static constexpr const char* NAME = "global variable";
	static constexpr u32 FLAGS = WITH_ADDRESS_MAP | WITH_NAME_MAP;
	
	GlobalVariableHandle handle() const { return m_handle; }
	Address address() const { return m_address; }
	SourceFileHandle source_file() const { return m_source_file; };
	
	const std::string& mangled_name() const;
	const void set_mangled_name(std::string mangled);
	
	GlobalStorage storage;
	ast::StorageClass storage_class;
	
protected:
	Address& address_ref() { return m_address; }
	
	Address m_address;
	SourceFileHandle m_source_file;
	std::string m_mangled_name;
};

class Label : public Symbol {
	friend SymbolList<Label>;
public:
	static constexpr const SymbolDescriptor DESCRIPTOR = SymbolDescriptor::LABEL;
	static constexpr const char* NAME = "label";
	static constexpr u32 FLAGS = WITH_ADDRESS_MAP;
	
	LabelHandle handle() const { return m_handle; }
	Address address() const { return m_address; }
	
protected:
	Address& address_ref() { return m_address; }
	
	Address m_address;
};

class LocalVariable : public Symbol {
	friend Function;
	friend SymbolList<LocalVariable>;
public:
	static constexpr const SymbolDescriptor DESCRIPTOR = SymbolDescriptor::LOCAL_VARIABLE;
	static constexpr const char* NAME = "local variable";
	static constexpr u32 FLAGS = WITH_ADDRESS_MAP;
	
	Address address() const { return m_address; }
	LocalVariableHandle handle() const { return m_handle; }
	FunctionHandle function() const { return m_function; };
	
	std::variant<GlobalStorage, RegisterStorage, StackStorage> storage;
	AddressRange live_range;
	
protected:
	Address& address_ref() { return m_address; }
	
	Address m_address;
	FunctionHandle m_function;
};

class ParameterVariable : public Symbol {
	friend Function;
	friend SymbolList<ParameterVariable>;
public:
	static constexpr const SymbolDescriptor DESCRIPTOR = SymbolDescriptor::PARAMETER_VARIABLE;
	static constexpr const char* NAME = "parameter variable";
	static constexpr u32 FLAGS = NO_SYMBOL_FLAGS;
	
	ParameterVariableHandle handle() const { return m_handle; }
	FunctionHandle function() const { return m_function; };
	
	std::variant<RegisterStorage, StackStorage> storage;
	
protected:
	FunctionHandle m_function;
};

class Section : public Symbol {
	friend SymbolList<Section>;
public:
	static constexpr const SymbolDescriptor DESCRIPTOR = SymbolDescriptor::SECTION;
	static constexpr const char* NAME = "section";
	static constexpr u32 FLAGS = WITH_ADDRESS_MAP | WITH_NAME_MAP;
	
	SectionHandle handle() const { return m_handle; }
	Address address() const { return m_address; }
	
	u32 size = 0;

protected:
	Address& address_ref() { return m_address; }
	
	Address m_address;
};

class SourceFile : public Symbol {
	friend SymbolList<SourceFile>;
public:
	static constexpr const SymbolDescriptor DESCRIPTOR = SymbolDescriptor::SOURCE_FILE;
	static constexpr const char* NAME = "source file";
	static constexpr u32 FLAGS = NO_SYMBOL_FLAGS;
	
	SourceFileHandle handle() const { return m_handle; }
	const std::string& full_path() const { return name(); }
	
	FunctionRange functions() const { return m_functions; }
	void set_functions(FunctionRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database);
	
	GlobalVariableRange globals_variables() const { return m_globals_variables; }
	void set_globals_variables(GlobalVariableRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database);
	
	std::string relative_path;
	Address text_address = 0;
	std::map<StabsTypeNumber, DataTypeHandle> stabs_type_number_to_handle;
	std::set<std::string> toolchain_version_info;
	
protected:
	FunctionRange m_functions;
	GlobalVariableRange m_globals_variables;
};

class SymbolSource : public Symbol {
	friend SymbolList<SymbolSource>;
public:
	static constexpr const SymbolDescriptor DESCRIPTOR = SymbolDescriptor::SYMBOL_SOURCE;
	static constexpr const char* NAME = "symbol source";
	static constexpr u32 FLAGS = NO_SYMBOL_FLAGS;
	
	SymbolSourceHandle handle() const { return m_handle; }
	Address address() const { return m_address; }

protected:
	Address& address_ref() { return m_address; }

	Address m_address;
};

// The symbol database itself. This owns all the symbols.

class SymbolDatabase {
public:
	SymbolList<DataType> data_types;
	SymbolList<Function> functions;
	SymbolList<GlobalVariable> global_variables;
	SymbolList<Label> labels;
	SymbolList<LocalVariable> local_variables;
	SymbolList<ParameterVariable> parameter_variables;
	SymbolList<Section> sections;
	SymbolList<SourceFile> source_files;
	SymbolList<SymbolSource> symbol_sources;
	
	// Check if a symbol has already been added to the database.
	bool symbol_exists_at_address(Address address) const;
	
	// Check if the symbol referenced by a given node handle still exists. If it
	// does, return the node pointer stored within, otherwise return nullptr.
	const ast::Node* node_from_handle(const NodeHandle& node_handle);
	
	// Destroy all the symbols in the symbol database.
	void clear();
	
	// Destroy all the symbols from a given symbol source. For example, you can
	// use this to free a symbol table without destroying user-defined symbols.
	void destroy_symbols_from_source(SymbolSourceHandle source);
	
	// Deduplicate matching data types with the same name. May replace the
	// existing data type with the new one if the new one is better.
	// DANGER: Accessing a node handle that was pointing into a replaced data
	// type after calling this function is a crash!
	[[nodiscard]] Result<DataType*> create_data_type_if_unique(
		std::unique_ptr<ast::Node> node,
		StabsTypeNumber number,
		const char* name,
		SourceFile& source_file,SymbolSourceHandle source);
	
	// Destroy a function handle as well as all parameter variables and local
	// variables it associated with it.
	bool destroy_function(FunctionHandle handle);
	
	template <typename Callback>
	void for_each_symbol(Callback callback) {
		#define CCC_X(SymbolType, symbol_list) \
			for(SymbolType& symbol : symbol_list) { \
				callback(symbol); \
			}
		CCC_FOR_EACH_SYMBOL_TYPE_DO_X
		#undef CCC_X
	}
};

}
