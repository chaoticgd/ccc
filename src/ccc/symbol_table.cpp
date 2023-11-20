// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "symbol_table.h"

#include <mutex>

#include "mdebug.h"
#include "mdebug_analysis.h"

namespace ccc {

u32 identify_symbol_tables(const ElfFile& elf) {
	u32 result = 0;
	for(const ElfSection& section : elf.sections) {
		if(section.name == ".symtab" && section.size > 0) {
			result |= SYMTAB;
		}
		if(section.name == ".mdebug" && section.size > 0) {
			result |= MDEBUG;
		}
		if(section.name == ".stab" && section.size > 0) {
			result |= STAB;
		}
		if(section.name == ".debug" && section.size > 0) {
			result |= DWARF;
		}
		if(section.name == ".sndata" && section.size > 0) {
			result |= SNDATA;
		}
	}
	return result;
}

void print_symbol_table_formats_to_string(FILE* out, u32 formats) {
	bool printed = false;
	for(u32 bit = 1; bit < MAX_SYMBOL_TABLE; bit <<= 1) {
		u32 format = formats & bit;
		if(format != 0) {
			fprintf(out, "%s%s", printed ? " " : "", symbol_table_format_to_string((SymbolTableFormat) format));
			printed = true;
		}
	}
	if(!printed) {
		fprintf(out, "none");
	}
}

const char* symbol_table_format_to_string(SymbolTableFormat format) {
	switch(format) {
		case SYMTAB: return "symtab";
		case MAP: return "map";
		case MDEBUG: return "mdebug";
		case STAB: return "stab";
		case DWARF: return "dwarf";
		case SNDATA: return "sndata";
		case SNDLL: return "sndll";
	}
	return "";
}

// *****************************************************************************

template <typename SymbolType>
SymbolType* SymbolList<SymbolType>::operator[](SymbolHandle<SymbolType> handle) {
	size_t index = binary_search(handle);
	return m_symbols[index].m_handle == handle ? &m_symbols[index] : nullptr;
}

template <typename SymbolType>
const SymbolType* SymbolList<SymbolType>::operator[](SymbolHandle<SymbolType> handle) const {
	return (*const_cast<SymbolList<SymbolType>*>(this))[handle];
}

template <typename SymbolType>
SymbolType& SymbolList<SymbolType>::at(SymbolHandle<SymbolType> handle) {
	SymbolType* symbol = (*this)[handle];
	CCC_ASSERT(symbol);
	return *symbol;
}

template <typename SymbolType>
const SymbolType& SymbolList<SymbolType>::at(SymbolHandle<SymbolType> handle) const {
	return const_cast<SymbolList<SymbolType>*>(this)->at(handle);
}

template <typename SymbolType>
typename SymbolList<SymbolType>::Iterator SymbolList<SymbolType>::begin() {
	return m_symbols.begin();
}

template <typename SymbolType>
typename SymbolList<SymbolType>::ConstIterator SymbolList<SymbolType>::begin() const {
	return m_symbols.begin();
}

template <typename SymbolType>
typename SymbolList<SymbolType>::Iterator SymbolList<SymbolType>::end() {
	return m_symbols.end();
}

template <typename SymbolType>
typename SymbolList<SymbolType>::ConstIterator SymbolList<SymbolType>::end() const {
	return m_symbols.end();
}

template <typename SymbolType>
std::span<SymbolType> SymbolList<SymbolType>::span(SymbolRange<SymbolType> range) {
	if(range.empty()) return std::span<SymbolType>();
	size_t first = binary_search(range.first);
	size_t last = binary_search(range.last);
	return std::span<SymbolType>(m_symbols.begin() + first, m_symbols.begin() + last + 1);
}

template <typename SymbolType>
std::span<const SymbolType> SymbolList<SymbolType>::span(SymbolRange<SymbolType> range) const {
	return const_cast<SymbolList<SymbolType>*>(this)->span(range);
}

template <typename SymbolType>
bool SymbolList<SymbolType>::empty() const {
	return m_symbols.size() == 0;
}

template <typename SymbolType>
SymbolHandle<SymbolType> SymbolList<SymbolType>::handle_from_address(u32 address) const {
	auto iterator = m_address_to_handle.find(address);
	if(iterator != m_address_to_handle.end()) {
		return iterator->second;
	} else {
		return SymbolHandle<SymbolType>();
	}
}

template <typename SymbolType>
typename SymbolList<SymbolType>::NameMapIterators SymbolList<SymbolType>::handles_from_name(const char* name) const {
	return {m_name_to_handle.find(name), m_name_to_handle.end()};
}

template <typename SymbolType>
Result<SymbolType*> SymbolList<SymbolType>::create_symbol(std::string name, u32 address) {
	CCC_CHECK(m_next_handle != UINT32_MAX, "Failed to allocate space for %s symbol.", SymbolType::SYMBOL_TYPE_NAME);
	
	u32 handle = m_next_handle++;
	
	if(SymbolType::LIST_FLAGS & WITH_ADDRESS_MAP) {
		auto iterator = m_address_to_handle.find(address);
		if(iterator != m_address_to_handle.end()) {
			// We're replacing an existing symbol.
			destroy_symbol(iterator->second);
		}
		
		m_address_to_handle.emplace(address, handle);
	}
	
	if(SymbolType::LIST_FLAGS & WITH_NAME_MAP) {
		m_name_to_handle.emplace(name, handle);
	}
	
	SymbolType& symbol = m_symbols.emplace_back();
	symbol.m_handle = m_next_handle++;
	symbol.m_address = address;
	symbol.m_name = std::move(name);
	
	return &symbol;
}

template <typename SymbolType>
bool SymbolList<SymbolType>::destroy_symbol(SymbolHandle<SymbolType> handle) {
	SymbolRange<SymbolType> range = {handle, handle};
	return destroy_symbols(range) == 1;
}

template <typename SymbolType>
u32 SymbolList<SymbolType>::destroy_symbols(SymbolRange<SymbolType> range) {
	if(range.empty()) {
		return 0;
	}
	
	// Lookup the index of the first symbol, and find how many should be erased.
	u32 begin_index = binary_search(range.first);
	u32 end_index = begin_index;
	while(m_symbols[end_index].m_handle <= range.last.value) {
		end_index++;
	}
	
	// Clean up map entries.
	if(SymbolType::LIST_FLAGS & WITH_ADDRESS_MAP) {
		for(u32 i = begin_index; i < end_index; i++) {
			m_address_to_handle.erase(m_symbols[i].m_address);
		}
	}
	
	if(SymbolType::LIST_FLAGS & WITH_NAME_MAP) {
		for(u32 i = begin_index; i < end_index; i++) {
			auto iterator = m_name_to_handle.find(m_symbols[i].m_name);
			while(iterator != m_name_to_handle.end()) {
				if(iterator->second == m_symbols[i].m_handle) {
					auto to_delete = iterator;
					iterator++;
					m_name_to_handle.erase(to_delete);
				} else {
					iterator++;
				}
			}
		}
	}
	
	// Delete the symbols.
	m_symbols.erase(m_symbols.begin() + begin_index, m_symbols.begin() + end_index);
	
	return end_index - begin_index;
}

template <typename SymbolType>
u32 SymbolList<SymbolType>::binary_search(SymbolHandle<SymbolType> handle) const {
	size_t begin = 0;
	size_t end = m_symbols.size();
	
	while(begin < end) {
		size_t mid = (begin + end) / 2;
		if(m_symbols[mid].m_handle < handle) {
			begin = mid + 1;
		} else if(m_symbols[mid].m_handle > handle) {
			end = mid;
		} else {
			return (u32) mid;
		}
	}
	
	return (u32) end;
}

#define CCC_X(SymbolType, symbol_list) template class SymbolList<SymbolType>;
CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X

// *****************************************************************************

const char* Variable::GlobalStorage::location_to_string(Location location) {
	switch(location) {
		case NIL: return "nil";
		case DATA: return "data";
		case BSS: return "bss";
		case ABS: return "abs";
		case SDATA: return "sdata";
		case SBSS: return "sbss";
		case RDATA: return "rdata";
		case COMMON: return "common";
		case SCOMMON: return "scommon";
	}
	return "";
}

void Function::set_parameter_variables(ParameterVariableRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolTable& symbol_table) {
	if(delete_old_symbols == DELETE_OLD_SYMBOLS) {
		symbol_table.parameter_variables.destroy_symbols(m_parameter_variables);
	}
	m_parameter_variables = range;
	for(ParameterVariable& parameter_variable : symbol_table.parameter_variables.span(range)) {
		parameter_variable.m_function = m_handle;
	}
}

void Function::set_local_variables(LocalVariableRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolTable& symbol_table) {
	if(delete_old_symbols == DELETE_OLD_SYMBOLS) {
		symbol_table.local_variables.destroy_symbols(m_local_variables);
	}
	m_local_variables = range;
	for(LocalVariable& local_variable : symbol_table.local_variables.span(range)) {
		local_variable.m_function = m_handle;
	}
}

void SourceFile::set_functions(FunctionRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolTable& symbol_table) {
	if(delete_old_symbols == DELETE_OLD_SYMBOLS) {
		symbol_table.functions.destroy_symbols(m_functions);
	}
	m_functions = range;
	for(Function& function : symbol_table.functions.span(range)) {
		function.m_source_file = m_handle;
	}
}

void SourceFile::set_globals_variables(GlobalVariableRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolTable& symbol_table) {
	if(delete_old_symbols == DELETE_OLD_SYMBOLS) {
		symbol_table.global_variables.destroy_symbols(m_globals_variables);
	}
	m_globals_variables = range;
	for(GlobalVariable& global_variable : symbol_table.global_variables.span(range)) {
		global_variable.m_source_file = m_handle;
	}
}

// *****************************************************************************

DataTypeHandle SymbolTable::lookup_type(const ast::TypeName& type_name, bool fallback_on_name_lookup) const {
	// Lookup the type by its STABS type number. This path ensures that the
	// correct type is found even if multiple types have the same name.
	if(type_name.referenced_file_handle != (u32) -1 && type_name.referenced_stabs_type_number.type > -1) {
		const SourceFile* source_file = source_files[type_name.referenced_file_handle];
		CCC_ASSERT(source_file);
		auto handle = source_file->stabs_type_number_to_handle.find(type_name.referenced_stabs_type_number);
		if(handle != source_file->stabs_type_number_to_handle.end()) {
			return handle->second;
		}
	}
	
	// Looking up the type by its STABS type number failed, so look for it by
	// its name instead. This happens when a type is forward declared but not
	// defined in a given translation unit.
	if(fallback_on_name_lookup) {
		auto types_with_name = data_types.handles_from_name(type_name.type_name.c_str());
		if(types_with_name.begin() != types_with_name.end()) {
			return types_with_name.begin()->second;
		}
	}
	
	// Type lookup failed. This happens when a type is forward declared in a
	// translation unit with symbols but is not defined in one.
	return DataTypeHandle();
}

Result<DataType*> SymbolTable::create_data_type_if_unique(std::unique_ptr<ast::Node> node, const char* name, SourceFile& source_file) {
	auto types_with_same_name = data_types.handles_from_name(name);
	const char* compare_fail_reason = nullptr;
	if(types_with_same_name.begin() == types_with_same_name.end()) {
		// No types with this name have previously been processed.
		Result<DataType*> data_type = data_types.create_symbol(name);
		CCC_RETURN_IF_ERROR(data_type);
		
		(*data_type)->files = {source_file.handle()};
		if(node->stabs_type_number.type > -1) {
			source_file.stabs_type_number_to_handle[node->stabs_type_number] = (*data_type)->handle();
		}
		return *data_type;
	} else {
		// Types with this name have previously been processed, we need
		// to figure out if this one matches any of the previous ones.
		bool match = false;
		for(auto [key, existing_type_handle] : types_with_same_name) {
			DataType* existing_type = data_types[existing_type_handle];
			CCC_ASSERT(existing_type);
			
			ast::CompareResult compare_result = compare_nodes(existing_type->type(), *node.get(), *this, true);
			if(compare_result.type == ast::CompareResultType::DIFFERS) {
				// The new node doesn't match this existing node.
				bool is_anonymous_enum = existing_type->type().descriptor == ast::ENUM
					&& existing_type->name().empty();
				if(!is_anonymous_enum) {
					existing_type->compare_fail_reason = compare_fail_reason_to_string(compare_result.fail_reason);
					compare_fail_reason = compare_fail_reason_to_string(compare_result.fail_reason);
				}
			} else {
				// The new node matches this existing node.
				existing_type->files.emplace_back(source_file.handle());
				if(node->stabs_type_number.type > -1) {
					source_file.stabs_type_number_to_handle[node->stabs_type_number] = existing_type->handle();
				}
				if(compare_result.type == ast::CompareResultType::MATCHES_FAVOUR_RHS) {
					// The new node matches the old one, but the new one
					// is slightly better, so we swap them.
					existing_type->set_type_and_invalidate_node_handles(std::move(node));
				}
				match = true;
				break;
			}
		}
		if(!match) {
			// This type doesn't match the others with the same name
			// that have already been processed.
			Result<DataType*> data_type = data_types.create_symbol(name);
			CCC_RETURN_IF_ERROR(data_type);
			(*data_type)->files = {source_file.handle()};
			if(node->stabs_type_number.type > -1) {
				source_file.stabs_type_number_to_handle[node->stabs_type_number] = (*data_type)->handle();
			}
			(*data_type)->compare_fail_reason = compare_fail_reason;
			return *data_type;
		}
	}
	
	return nullptr;
}

// *****************************************************************************

SymbolTableGuardian::SymbolTableGuardian() {}

SymbolTableHandle SymbolTableGuardian::create(std::string name) {
	if(m_symbol_tables.size() > UINT32_MAX) {
		return SymbolTableHandle();
	}
	std::unique_lock lock(m_big_symbol_table_lock);
	SymbolTableHandle handle = (u32) m_symbol_tables.size();
	m_symbol_tables.emplace_back();
	m_small_symbol_table_locks.emplace_back(std::make_unique<std::shared_mutex>());
	return handle;
}

bool SymbolTableGuardian::destroy(SymbolTableHandle handle) {
	std::unique_lock lock(m_big_symbol_table_lock);
	m_symbol_tables.at(handle.value) = {}; // Free the memory.
	m_small_symbol_table_locks.at(handle.value) = nullptr; // Mark the slot as dead.
	return true;
}

void SymbolTableGuardian::clear() {
	std::unique_lock lock(m_big_symbol_table_lock);
	m_symbol_tables.clear();
}

std::optional<SymbolTableHandle> SymbolTableGuardian::lookup(const std::string& name) const {
	std::shared_lock lock(m_big_symbol_table_lock);
	for(u32 i = 0; i < (u32) m_symbol_tables.size(); i++) {
		if(m_small_symbol_table_locks[i].get() && m_symbol_tables[i].name == name) {
			return i;
		}
	}
	return std::nullopt;
}

[[nodiscard]] bool SymbolTableGuardian::read(SymbolTableHandle handle, std::function<void(const SymbolTable&)> callback) const {
	std::shared_lock big_lock(m_big_symbol_table_lock);
	if(handle.value >= m_symbol_tables.size() || !m_small_symbol_table_locks[handle.value].get()) {
		return false;
	}
	std::shared_lock small_lock(*m_small_symbol_table_locks[handle.value]);
	callback(m_symbol_tables[handle.value]);
	return true;
}

[[nodiscard]] bool SymbolTableGuardian::write(SymbolTableHandle handle, std::function<void(SymbolTable&)> callback) {
	std::shared_lock big_lock(m_big_symbol_table_lock);
	if(handle.value >= m_symbol_tables.size() || !m_small_symbol_table_locks[handle.value].get()) {
		return false;
	}
	std::unique_lock small_lock(*m_small_symbol_table_locks[handle.value]);
	callback(m_symbol_tables[handle.value]);
	return true;
}

Result<SymbolTable> parse_symbol_table(ElfFile& elf) {
	ElfSection* mdebug_section = elf.lookup_section(".mdebug");
	CCC_CHECK(mdebug_section != nullptr, "No .mdebug section.");
	
	mdebug::SymbolTableReader reader;
	Result<void> reader_result = reader.init(elf.image, mdebug_section->file_offset);
	CCC_EXIT_IF_ERROR(reader_result);
	
	return analyse(reader, NO_ANALYSIS_FLAGS);
}

}
