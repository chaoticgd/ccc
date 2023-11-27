// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "symbol_database.h"

#include <mutex>

#include "mdebug.h"
#include "mdebug_analysis.h"

namespace ccc {

template <typename SymbolType>
SymbolType* SymbolList<SymbolType>::operator[](SymbolHandle<SymbolType> handle) {
	if(!handle.valid()) {
		return nullptr;
	}
	
	size_t index = binary_search(handle);
	if(index >= m_symbols.size() || m_symbols[index].m_handle != handle) {
		return nullptr;
	}
	
	return &m_symbols[index];
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
	size_t first = binary_search(range.first);
	size_t last = binary_search(range.last);
	if(last < m_symbols.size() && m_symbols[last].m_handle == range.last) {
		return std::span<SymbolType>(m_symbols.begin() + first, m_symbols.begin() + last + 1);
	} else {
		return std::span<SymbolType>(m_symbols.begin() + first, m_symbols.begin() + last);
	}
}

template <typename SymbolType>
std::span<const SymbolType> SymbolList<SymbolType>::span(SymbolRange<SymbolType> range) const {
	return const_cast<SymbolList<SymbolType>*>(this)->span(range);
}

template <typename SymbolType>
std::span<SymbolType> SymbolList<SymbolType>::span(std::optional<SymbolRange<SymbolType>> range) {
	std::span<SymbolType> result;
	if(range.has_value()) {
		result = span(*range);
	}
	return result;
}

template <typename SymbolType>
std::span<const SymbolType> SymbolList<SymbolType>::span(std::optional<SymbolRange<SymbolType>> range) const {
	std::span<const SymbolType> result;
	if(range.has_value()) {
		result = span(*range);
	}
	return result;
}

template <typename SymbolType>
SymbolHandle<SymbolType> SymbolList<SymbolType>::handle_from_address(Address address) const {
	auto iterator = m_address_to_handle.find(address.value);
	if(iterator != m_address_to_handle.end()) {
		return iterator->second;
	} else {
		return SymbolHandle<SymbolType>();
	}
}

template <typename SymbolType>
typename SymbolList<SymbolType>::NameToHandleMapIterators SymbolList<SymbolType>::handles_from_name(const char* name) const {
	return {m_name_to_handle.find(name), m_name_to_handle.end()};
}

template <typename SymbolType>
bool SymbolList<SymbolType>::empty() const {
	return m_symbols.size() == 0;
}

template <typename SymbolType>
Result<SymbolType*> SymbolList<SymbolType>::create_symbol(std::string name, SymbolSourceHandle source, Address address) {
	CCC_CHECK(m_next_handle != UINT32_MAX, "Failed to allocate space for %s symbol.", SymbolType::SYMBOL_TYPE_NAME);
	
	u32 handle = m_next_handle++;
	
	if constexpr((SymbolType::LIST_FLAGS & WITH_ADDRESS_MAP)) {
		if(address.valid()) {
			auto iterator = m_address_to_handle.find(address.value);
			if(iterator != m_address_to_handle.end()) {
				// We're replacing an existing symbol.
				destroy_symbol(iterator->second);
			}
			
			m_address_to_handle.emplace(address.value, handle);
		}
	}
	
	if(SymbolType::LIST_FLAGS & WITH_NAME_MAP) {
		m_name_to_handle.emplace(name, handle);
	}
	
	SymbolType& symbol = m_symbols.emplace_back();
	
	symbol.m_handle = handle;
	symbol.m_name = std::move(name);
	
	if constexpr(std::is_same_v<SymbolType, SymbolSource>) {
		// It doesn't make sense for the calling code to provide a symbol source
		// handle as an argument if w're creating a symbol source symbol, so we
		// set the source of the new symbol to its own handle.
		symbol.m_source = handle;
	} else {
		CCC_ASSERT(source.valid());
		symbol.m_source = source;
	}
	
	if constexpr(SymbolType::LIST_FLAGS & WITH_ADDRESS_MAP) {
		symbol.address_ref() = address;
	}
	
	return &symbol;
}

template <typename SymbolType>
bool SymbolList<SymbolType>::destroy_symbol(SymbolHandle<SymbolType> handle) {
	SymbolRange<SymbolType> range = {handle, handle};
	return destroy_symbols(range) == 1;
}

template <typename SymbolType>
u32 SymbolList<SymbolType>::destroy_symbols(SymbolRange<SymbolType> range) {
	// Reject invalid ranges so that the <= comparison below works.
	if(!range.valid()) {
		return 0;
	}
	
	// Lookup the index of the first symbol, and find how many should be erased.
	u32 begin_index = binary_search(range.first);
	u32 end_index = begin_index;
	while(end_index < m_symbols.size() && m_symbols[end_index].m_handle <= range.last.value) {
		end_index++;
	}
	
	return destroy_symbols_impl(begin_index, end_index);
}

template <typename SymbolType>
void SymbolList<SymbolType>::destroy_symbols_from_source(SymbolSourceHandle source) {
	for(size_t i = 0; i < m_symbols.size(); i++) {
		if(m_symbols[i].m_source == source) {
			size_t end;
			for(end = i + 1; end < m_symbols.size(); end++) {
				if(m_symbols[end].m_source != source) {
					break;
				}
			}
			destroy_symbols_impl(i, end);
			i--;
		}
	}
}

template <typename SymbolType>
void SymbolList<SymbolType>::clear() {
	m_symbols.clear();
	m_address_to_handle.clear();
	m_name_to_handle.clear();
}

template <typename SymbolType>
u32 SymbolList<SymbolType>::destroy_symbols_impl(size_t begin_index, size_t end_index) {
	// Clean up address map entries.
	if constexpr(SymbolType::LIST_FLAGS & WITH_ADDRESS_MAP) {
		for(u32 i = begin_index; i < end_index; i++) {
			if(m_symbols[i].address_ref().valid()) {
				m_address_to_handle.erase(m_symbols[i].address_ref().value);
			}
		}
	}
	
	// Clean up name map entries.
	if constexpr(SymbolType::LIST_FLAGS & WITH_NAME_MAP) {
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
size_t SymbolList<SymbolType>::binary_search(SymbolHandle<SymbolType> handle) const {
	size_t begin;
	size_t end;
	
	// On the first iteration we use the value of the handle as the mid point.
	if(handle.value < m_symbols.size()) {
		if(m_symbols[handle.value].m_handle < handle) {
			begin = handle.value + 1;
			end = m_symbols.size();
		} else if(m_symbols[handle.value].m_handle > handle) {
			begin = 0;
			end = handle.value;
		} else {
			return handle.value;
		}
	} else {
		begin = 0;
		end = m_symbols.size();
	}
	
	while(begin < end) {
		size_t mid = (begin + end) / 2;
		if(m_symbols[mid].m_handle < handle) {
			begin = mid + 1;
		} else if(m_symbols[mid].m_handle > handle) {
			end = mid;
		} else {
			return mid;
		}
	}
	
	return end;
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

void Variable::set_storage_once(Storage new_storage) {
	GlobalStorage* old_global_storage = std::get_if<GlobalStorage>(&m_storage);
	GlobalStorage* new_global_storage = std::get_if<GlobalStorage>(&new_storage);
	// By default m_storage is set to global storage.
	CCC_ASSERT(old_global_storage);
	// Make sure we don't change the address so the address map in the symbol
	// list doesn't get out of sync.
	bool no_address = !new_global_storage && !old_global_storage->address.valid();
	bool same_address = new_global_storage && new_global_storage->address == old_global_storage->address;
	CCC_ASSERT(no_address || same_address);
	m_storage = new_storage;
}

Address& Variable::address_ref() {
	if(GlobalStorage* global_storage = std::get_if<GlobalStorage>(&m_storage)) {
		return global_storage->address;
	} else {
		CCC_FATAL("Variable::address_ref() called for variable with non-global storage.");
	}
}

// *****************************************************************************

void Function::set_parameter_variables(std::optional<ParameterVariableRange> range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database) {
	if(delete_old_symbols == DELETE_OLD_SYMBOLS && m_parameter_variables.has_value()) {
		database.parameter_variables.destroy_symbols(*m_parameter_variables);
	}
	if(range.has_value()) {
		for(ParameterVariable& parameter_variable : database.parameter_variables.span(*range)) {
			parameter_variable.m_function = m_handle;
		}
	}
	m_parameter_variables = range;
}

void Function::set_local_variables(std::optional<LocalVariableRange> range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database) {
	if(delete_old_symbols == DELETE_OLD_SYMBOLS && m_local_variables.has_value()) {
		database.local_variables.destroy_symbols(*m_local_variables);
	}
	if(range.has_value()) {
		for(LocalVariable& local_variable : database.local_variables.span(*range)) {
			local_variable.m_function = m_handle;
		}
	}
	m_local_variables = range;
}

const std::string& Function::demangled_name() const {
	if(!m_demangled_name.empty()) {
		return m_demangled_name;
	} else {
		return name();
	}
}

const void Function::set_demangled_name(std::string demangled) {
	m_demangled_name = std::move(demangled);
}

const std::string& GlobalVariable::demangled_name() const {
	if(!m_demangled_name.empty()) {
		return m_demangled_name;
	} else {
		return name();
	}
}

const void GlobalVariable::set_demangled_name(std::string demangled) {
	m_demangled_name = std::move(demangled);
}

void SourceFile::set_functions(FunctionRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database) {
	if(delete_old_symbols == DELETE_OLD_SYMBOLS) {
		database.functions.destroy_symbols(m_functions);
	}
	m_functions = range;
	for(Function& function : database.functions.span(range)) {
		function.m_source_file = m_handle;
	}
}

void SourceFile::set_globals_variables(GlobalVariableRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database) {
	if(delete_old_symbols == DELETE_OLD_SYMBOLS) {
		database.global_variables.destroy_symbols(m_globals_variables);
	}
	m_globals_variables = range;
	for(GlobalVariable& global_variable : database.global_variables.span(range)) {
		global_variable.m_source_file = m_handle;
	}
}

// *****************************************************************************

void SymbolDatabase::clear() {
	#define CCC_X(SymbolType, symbol_list) symbol_list.clear();
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
	#undef CCC_X
}

void SymbolDatabase::destroy_symbols_from_source(SymbolSourceHandle source) {
	#define CCC_X(SymbolType, symbol_list) symbol_list.destroy_symbols_from_source(source);
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
	#undef CCC_X
}

DataTypeHandle SymbolDatabase::lookup_type(const ast::TypeName& type_name, bool fallback_on_name_lookup) const {
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

Result<DataType*> SymbolDatabase::create_data_type_if_unique(std::unique_ptr<ast::Node> node, const char* name, SourceFile& source_file, SymbolSourceHandle source) {
	auto types_with_same_name = data_types.handles_from_name(name);
	const char* compare_fail_reason = nullptr;
	if(types_with_same_name.begin() == types_with_same_name.end()) {
		// No types with this name have previously been processed.
		Result<DataType*> data_type = data_types.create_symbol(name, source);
		CCC_RETURN_IF_ERROR(data_type);
		
		(*data_type)->files = {source_file.handle()};
		if(node->stabs_type_number.type > -1) {
			source_file.stabs_type_number_to_handle[node->stabs_type_number] = (*data_type)->handle();
		}
		
		(*data_type)->set_type(std::move(node));
		
		return *data_type;
	} else {
		// Types with this name have previously been processed, we need to
		// figure out if this one matches any of the previous ones.
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
					// The new node almost matches the old one, but the new one
					// is slightly better, so we replace the old type.
					existing_type->set_type_and_invalidate_node_handles(std::move(node));
				}
				match = true;
				break;
			}
		}
		if(!match) {
			// This type doesn't match the others with the same name that have
			// already been processed.
			Result<DataType*> data_type = data_types.create_symbol(name, source);
			CCC_RETURN_IF_ERROR(data_type);
			
			(*data_type)->files = {source_file.handle()};
			if(node->stabs_type_number.type > -1) {
				source_file.stabs_type_number_to_handle[node->stabs_type_number] = (*data_type)->handle();
			}
			(*data_type)->compare_fail_reason = compare_fail_reason;
			
			(*data_type)->set_type(std::move(node));
			
			return *data_type;
		}
	}
	
	return nullptr;
}

}
