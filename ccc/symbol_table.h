#pragma once

#include <atomic>
#include <functional>

#include "elf.h"

namespace ccc {

// Determine which symbol tables are present in a given file.

enum SymbolTableFormat {
	SYMTAB = 1 << 0, // Standard ELF symbol table
	MAP    = 1 << 1, // Text-based (.map) symbol table
	MDEBUG = 1 << 2, // The infamous Third Eye symbol table
	STAB   = 1 << 3, // Simpler container format for STABS symbols
	DWARF  = 1 << 4, // DWARF 1 symbol table
	SNDATA = 1 << 5, // SNDLL linker symbols from an executable (.elf)
	SNDLL  = 1 << 6  // SNDLL linker smybols from a dynamic library (.rel)
};

enum {
	NO_SYMBOL_TABLE = 0,      // No symbol table present
	MAX_SYMBOL_TABLE = 1 << 7 // End marker
};

u32 identify_symbol_tables(const ElfFile& elf);

// Forward declare all the different types of symbol table objects.

struct DataType;
struct Function;
struct GlobalVariable;
struct Label;
struct SourceFile;
struct SymbolTable;

// Define strongly typed handles for all of the symbol table objects.

template <typename SymbolType>
struct SymbolHandle {
	s32 value = -1;
	
	SymbolHandle() : value(-1) {}
	SymbolHandle(s32 v) : value(v) {}
	
	SymbolHandle& operator++() { ++value; return *this; }
	SymbolHandle operator++(int) { SymbolHandle old = *this; value++; return old; }
	operator s32() const { return value; }
	friend auto operator<=>(const SymbolHandle& lhs, const SymbolHandle& rhs) = default;
};

using DataTypeHandle = SymbolHandle<DataType>;
using FunctionHandle = SymbolHandle<Function>;
using GlobalVariableHandle = SymbolHandle<GlobalVariable>;
using LabelHandle = SymbolHandle<Label>;
using SourceFileHandle = SymbolHandle<SourceFile>;
using SymbolTableHandle = SymbolHandle<SymbolTable>;

// Define iterators for all of the symbol table objects that skip over all
// deleted objects.

template <typename SymbolType>
struct SymbolIterator {
	using iterator_category = std::forward_iterator_tag;
	using difference_type   = std::ptrdiff_t;
	using value_type        = std::pair<SymbolHandle<SymbolType>, SymbolType>;
	using pointer           = value_type*;
	using reference         = value_type&;
	
	SymbolIterator(const std::vector<SymbolType>& symbols, size_t current)
		: m_symbols(symbols)
		, m_current((s32) current) { skip_deleted(); }
	
	value_type& operator*() const { return m_current.operator*(); }
	value_type* operator->() { return m_current.operator->(); }
	SymbolIterator& operator++() { skip_deleted(); return *this; }
	SymbolIterator operator++(int) { SymbolIterator old = *this; skip_deleted(); return old; }
	friend auto operator<=>(const SymbolIterator& lhs, const SymbolIterator& rhs) = default;
	
protected:
	void skip_deleted() {
		while((u32) m_current.value < m_symbols.size() && m_symbols[m_current.value].is_deleted) {
			m_current++;
		}
	}

	const std::vector<SymbolType>& m_symbols;
	SymbolHandle<SymbolType> m_current;
};

using DataTypeIterator = SymbolIterator<DataType>;
using FunctionIterator = SymbolIterator<Function>;
using GlobalVariableIterator = SymbolIterator<GlobalVariable>;
using LabelIterator = SymbolIterator<Label>;
using SourceFileIterator = SymbolIterator<SourceFile>;
using SymbolTableIterator = SymbolIterator<SymbolTable>;

struct Symbol {
	std::string name;
	u32 address = 0;
	bool is_deleted = false;
};

// All the different types of symbol table objects.

struct DataType : Symbol {
	
};

struct Function : Symbol {
	SourceFileHandle source_file;
};

struct GlobalVariable : Symbol {
	SourceFileHandle source_file;
};

struct Label : Symbol {
	
};

struct SourceFile : Symbol {
	std::string path;
	std::optional<std::string> working_dir;
	std::optional<std::string> relative_path;
	
	FunctionHandle first_function;
	FunctionHandle last_function;
};

// A container for symbols of a given type that maintains maps of their names
// and optionally their addresses.

template <typename SymbolType, bool unique_addresses>
class SymbolList {
public:
	const SymbolType* operator[](SymbolHandle<SymbolType> handle) const {
		if(handle.value < 0 || (size_t) handle >= m_symbols.size()) {
			return nullptr;
		}
		return &m_symbols[handle];
	}

	SymbolIterator<SymbolType> begin() const { return SymbolIterator<SymbolType>(m_symbols, 0); }
	SymbolIterator<SymbolType> end() const { return SymbolIterator<SymbolType>(m_symbols, m_symbols.size()); }
	
	SymbolHandle<SymbolType> add(SymbolType symbol) {
		if constexpr(unique_addresses) {
			auto handle = m_address_to_handle.find(symbol.address);
			if(handle != m_address_to_handle.end()) {
				remove(handle->second);
			}
		}
		
		m_name_to_handle.emplace(symbol.name, (s32) m_symbols.size());
		m_address_to_handle.emplace(symbol.address, (s32) m_symbols.size());
		
		m_symbols.emplace_back(std::move(symbol));
	}
	
	bool remove(SymbolHandle<SymbolType> handle) {
		SymbolType* symbol = (*this)[handle];
		if(symbol) {
			symbol->is_deleted = true;
			return true;
		}
		return false;
	}

protected:
	std::vector<DataType> m_symbols;
	std::map<std::string_view, SymbolHandle<SymbolType>> m_name_to_handle;
	std::map<u32, SymbolHandle<SymbolType>> m_address_to_handle;
};

// The symbol table type itself.

struct SymbolTable {
	SymbolList<DataType, false> data_types;
	SymbolList<Function, true> functions;
	SymbolList<GlobalVariable, true> global_variables;
	SymbolList<Label, true> labels;
	SymbolList<SourceFile, false> source_files;
};

// Handles synchronising access to a symbol table from multiple threads.

class SymbolTableGuardian {
public:
	SymbolTableGuardian();
	
	// Get the handle for the current symbol table.
	std::optional<SymbolTableHandle> get_current_handle() const;
	
	// Use this when you want to read from the symbol table, including accessing
	// pointers to AST nodes of said symbol table. If the symbol table was
	// destroyed this function will return false and your callback will not run.
	[[nodiscard]] bool read(SymbolTableHandle handle, std::function<void(const SymbolTable&)> callback) const;
	
	// Overwrite the currently stored symbol table with a new one, thereby
	// invalidating the current symbol table handle.
	void overwrite(SymbolTable symbol_table);
	
protected:
	SymbolTable m_symbol_table;
	SymbolTableHandle m_current_handle;
	static std::atomic<s32> s_next_handle;
	mutable std::mutex m_big_symbol_table_lock;
};

SymbolTable parse_symbol_table();

void print_symbol_table_formats_to_string(FILE* out, u32 formats);
const char* symbol_table_format_to_string(SymbolTableFormat format);

}
