// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "symbol_database.h"

#include "ast.h"
#include "importer_flags.h"

namespace ccc {

template <typename SymbolType>
SymbolType* SymbolList<SymbolType>::symbol_from_handle(SymbolHandle<SymbolType> handle)
{
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
const SymbolType* SymbolList<SymbolType>::symbol_from_handle(SymbolHandle<SymbolType> handle) const
{
	return const_cast<SymbolList<SymbolType>*>(this)->symbol_from_handle(handle);
}

template <typename SymbolType>
typename SymbolList<SymbolType>::Iterator SymbolList<SymbolType>::begin()
{
	return m_symbols.begin();
}

template <typename SymbolType>
typename SymbolList<SymbolType>::ConstIterator SymbolList<SymbolType>::begin() const
{
	return m_symbols.begin();
}

template <typename SymbolType>
typename SymbolList<SymbolType>::Iterator SymbolList<SymbolType>::end()
{
	return m_symbols.end();
}

template <typename SymbolType>
typename SymbolList<SymbolType>::ConstIterator SymbolList<SymbolType>::end() const
{
	return m_symbols.end();
}

template <typename SymbolType>
std::span<SymbolType> SymbolList<SymbolType>::span(SymbolRange<SymbolType> range)
{
	size_t first = binary_search(range.first);
	size_t last = binary_search(range.last);
	if(last < m_symbols.size() && m_symbols[last].m_handle == range.last) {
		return std::span<SymbolType>(m_symbols.data() + first, m_symbols.data() + last + 1);
	} else {
		return std::span<SymbolType>(m_symbols.data() + first, m_symbols.data() + last);
	}
}

template <typename SymbolType>
std::span<const SymbolType> SymbolList<SymbolType>::span(SymbolRange<SymbolType> range) const
{
	return const_cast<SymbolList<SymbolType>*>(this)->span(range);
}

template <typename SymbolType>
std::span<SymbolType> SymbolList<SymbolType>::optional_span(std::optional<SymbolRange<SymbolType>> range)
{
	if(range.has_value()) {
		return span(*range);
	} else {
		return std::span<SymbolType>();
	}
}

template <typename SymbolType>
std::span<const SymbolType> SymbolList<SymbolType>::optional_span(std::optional<SymbolRange<SymbolType>> range) const
{
	if(range.has_value()) {
		return span(*range);
	} else {
		return std::span<SymbolType>();
	}
}

template <typename SymbolType>
typename SymbolList<SymbolType>::AddressToHandleMapIterators SymbolList<SymbolType>::handles_from_starting_address(Address address) const
{
	auto iterators = m_address_to_handle.equal_range(address.value);
	return {iterators.first, iterators.second};
}

template <typename SymbolType>
SymbolHandle<SymbolType> SymbolList<SymbolType>::first_handle_from_starting_address(Address address) const
{
	auto iterator = m_address_to_handle.find(address.value);
	if(iterator != m_address_to_handle.end()) {
		return iterator->second;
	} else {
		return SymbolHandle<SymbolType>();
	}
}

template <typename SymbolType>
typename SymbolList<SymbolType>::NameToHandleMapIterators SymbolList<SymbolType>::handles_from_name(const std::string& name) const
{
	auto iterators = m_name_to_handle.equal_range(name);
	return {iterators.first, iterators.second};
}

template <typename SymbolType>
SymbolHandle<SymbolType> SymbolList<SymbolType>::first_handle_from_name(const std::string& name) const
{
	auto iterator = m_name_to_handle.find(name);
	if(iterator != m_name_to_handle.end()) {
		return iterator->second;
	} else {
		return SymbolHandle<SymbolType>();
	}
}

template <typename SymbolType>
SymbolType* SymbolList<SymbolType>::symbol_from_contained_address(Address address)
{
	auto iterator = m_address_to_handle.lower_bound(address.value);
	if(iterator != m_address_to_handle.end()) {
		SymbolType* symbol = symbol_from_handle(iterator->second);
		if(symbol && address.value < symbol->m_address.value + symbol->m_size) {
			return symbol;
		}
	}
	return nullptr;
}

template <typename SymbolType>
const SymbolType* SymbolList<SymbolType>::symbol_from_contained_address(Address address) const
{
	return const_cast<SymbolList<SymbolType>*>(this)->symbol_from_contained_address(address);
}

template <typename SymbolType>
s32 SymbolList<SymbolType>::index_from_handle(SymbolHandle<SymbolType> handle) const
{
	if(!handle.valid()) {
		return -1;
	}
	
	size_t index = binary_search(handle);
	if(index >= m_symbols.size() || m_symbols[index].m_handle != handle) {
		return -1;
	}
	
	return (s32) index;
}

template <typename SymbolType>
std::pair<s32, s32> SymbolList<SymbolType>::index_pair_from_range(SymbolRange<SymbolType> range) const
{
	size_t first = binary_search(range.first);
	size_t last = binary_search(range.last);
	if(last < m_symbols.size() && m_symbols[last].m_handle == range.last) {
		return {first, last + 1};
	} else {
		return {first, last};
	}
}

template <typename SymbolType>
bool SymbolList<SymbolType>::empty() const
{
	return m_symbols.size() == 0;
}


template <typename SymbolType>
s32 SymbolList<SymbolType>::size() const
{
	return (s32) m_symbols.size();
}

template <typename SymbolType>
Result<SymbolType*> SymbolList<SymbolType>::create_symbol(
	std::string name, SymbolSourceHandle source, const Module* module_symbol, Address address, u32 importer_flags, DemanglerFunctions demangler)
{
	static const int DMGL_PARAMS = 1 << 0;
	static const int DMGL_RET_POSTFIX = 1 << 5;
	
	std::string demangled_name;
	if constexpr(SymbolType::FLAGS & NAME_NEEDS_DEMANGLING) {
		if((importer_flags & DONT_DEMANGLE_NAMES) == 0 && demangler.cplus_demangle) {
			int demangler_flags = 0;
			if(importer_flags & DEMANGLE_PARAMETERS) demangler_flags |= DMGL_PARAMS;
			if(importer_flags & DEMANGLE_RETURN_TYPE) demangler_flags |= DMGL_RET_POSTFIX;
			char* demangled_name_ptr = demangler.cplus_demangle(name.c_str(), demangler_flags);
			if(demangled_name_ptr) {
				demangled_name = demangled_name_ptr;
				free(reinterpret_cast<void*>(demangled_name_ptr));
			}
		}
	}
	
	std::string& non_mangled_name = demangled_name.empty() ? name : demangled_name;
	
	// Since we parse all the symbol tables in a file, we're gonna encounter
	// duplicate symbols for functions and global variables. This logic filters
	// out said symbols.
	if((importer_flags & DONT_DEDUPLICATE_SYMBOLS) == 0 && address.valid()) {
		for(const auto& [existing_address, existing_handle] : handles_from_starting_address(address)) {
			SymbolType* existing_symbol = symbol_from_handle(existing_handle);
			if(existing_symbol && existing_symbol->name() == non_mangled_name) {
				return nullptr;
			}
		}
	}
	
	Result<SymbolType*> symbol = create_symbol(non_mangled_name, source, module_symbol, address);
	CCC_RETURN_IF_ERROR(symbol);
	
	if constexpr(SymbolType::FLAGS & NAME_NEEDS_DEMANGLING) {
		if(!demangled_name.empty()) {
			(*symbol)->set_mangled_name(name);
		}
	}
	
	return symbol;
}

template <typename SymbolType>
Result<SymbolType*> SymbolList<SymbolType>::create_symbol(
	std::string name, SymbolSourceHandle source, const Module* module_symbol, Address address)
{
	CCC_CHECK(m_next_handle != UINT32_MAX, "Failed to allocate space for %s symbol.", SymbolType::NAME);
	
	u32 handle = m_next_handle++;
	
	SymbolType& symbol = m_symbols.emplace_back();
	
	symbol.m_handle = handle;
	symbol.m_name = std::move(name);
	
	if constexpr(std::is_same_v<SymbolType, SymbolSource>) {
		// It doesn't make sense for the calling code to provide a symbol source
		// handle as an argument if we're creating a symbol source symbol, so we
		// set the source of the new symbol to its own handle.
		symbol.m_source = handle;
	} else {
		CCC_ASSERT(source.valid());
		symbol.m_source = source;
	}
	
	if constexpr(std::is_same_v<SymbolType, Module>) {
		// It doesn't make sense for the calling code to provide a module as an
		// argument if we're creating a module symbol, so we set the module of
		// the new symbol to its own handle.
		symbol.m_address = address;
		symbol.m_module = handle;
	} else if(module_symbol) {
		symbol.m_address = address.add_base_address(module_symbol->address());
		symbol.m_module = module_symbol->handle();
	} else {
		symbol.m_address = address;
	}
	
	link_address_map(symbol);
	link_name_map(symbol);
	
	return &symbol;
}

template <typename SymbolType>
bool SymbolList<SymbolType>::move_symbol(SymbolHandle<SymbolType> handle, Address new_address)
{
	SymbolType* symbol = symbol_from_handle(handle);
	if(!symbol) {
		return false;
	}
	
	if(symbol->m_address != new_address) {
		unlink_address_map(*symbol);
		symbol->m_address = new_address;
		link_address_map(*symbol);
	}
	
	return true;
}

template <typename SymbolType>
bool SymbolList<SymbolType>::rename_symbol(SymbolHandle<SymbolType> handle, std::string new_name)
{
	SymbolType* symbol = symbol_from_handle(handle);
	if(!symbol) {
		return false;
	}
	
	if(symbol->m_name != new_name) {
		unlink_name_map(*symbol);
		symbol->m_name = std::move(new_name);
		link_name_map(*symbol);
	}
	
	return true;
}

template <typename SymbolType>
bool SymbolList<SymbolType>::destroy_symbol(SymbolHandle<SymbolType> handle)
{
	SymbolRange<SymbolType> range = {handle, handle};
	return destroy_symbols(range) == 1;
}

template <typename SymbolType>
u32 SymbolList<SymbolType>::destroy_symbols(SymbolRange<SymbolType> range)
{
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
void SymbolList<SymbolType>::destroy_symbols_from_sources(SymbolSourceRange source_range)
{
	for(size_t i = 0; i < m_symbols.size(); i++) {
		if(m_symbols[i].m_source >= source_range.first && m_symbols[i].m_source <= source_range.last) {
			size_t end;
			for(end = i + 1; end < m_symbols.size(); end++) {
				if(m_symbols[i].m_source >= source_range.first && m_symbols[i].m_source <= source_range.last) {
					break;
				}
			}
			destroy_symbols_impl(i, end);
			i--;
		}
	}
}

template <typename SymbolType>
void SymbolList<SymbolType>::destroy_symbols_from_modules(ModuleRange module_range)
{
	for(size_t i = 0; i < m_symbols.size(); i++) {
		if(m_symbols[i].m_module >= module_range.first && m_symbols[i].m_module <= module_range.last) {
			size_t end;
			for(end = i + 1; end < m_symbols.size(); end++) {
				if(m_symbols[i].m_module >= module_range.first && m_symbols[i].m_module <= module_range.last) {
					break;
				}
			}
			destroy_symbols_impl(i, end);
			i--;
		}
	}
}

template <typename SymbolType>
void SymbolList<SymbolType>::clear()
{
	m_symbols.clear();
	m_address_to_handle.clear();
	m_name_to_handle.clear();
}

template <typename SymbolType>
size_t SymbolList<SymbolType>::binary_search(SymbolHandle<SymbolType> handle) const
{
	size_t begin = 0;
	size_t end = m_symbols.size();
	
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

template <typename SymbolType>
u32 SymbolList<SymbolType>::destroy_symbols_impl(size_t begin_index, size_t end_index)
{
	for(u32 i = begin_index; i < end_index; i++) {
		unlink_address_map(m_symbols[i]);
	}
	
	for(u32 i = begin_index; i < end_index; i++) {
		unlink_name_map(m_symbols[i]);
	}
	
	// Delete the symbols.
	m_symbols.erase(m_symbols.begin() + begin_index, m_symbols.begin() + end_index);
	
	return end_index - begin_index;
}

template <typename SymbolType>
void SymbolList<SymbolType>::link_address_map(SymbolType& symbol)
{
	if constexpr((SymbolType::FLAGS & WITH_ADDRESS_MAP)) {
		if(symbol.m_address.valid()) {
			m_address_to_handle.emplace(symbol.m_address.value, symbol.m_handle);
		}
	}
}

template <typename SymbolType>
void SymbolList<SymbolType>::unlink_address_map(SymbolType& symbol)
{
	if constexpr(SymbolType::FLAGS & WITH_ADDRESS_MAP) {
		if(symbol.m_address.valid()) {
			auto iterators = m_address_to_handle.equal_range(symbol.m_address.value);
			for(auto iterator = iterators.first; iterator != iterators.second; iterator++) {
				if(iterator->second == symbol.m_handle) {
					m_address_to_handle.erase(iterator);
					break;
				}
			}
		}
	}
}

template <typename SymbolType>
void SymbolList<SymbolType>::link_name_map(SymbolType& symbol)
{
	if constexpr(SymbolType::FLAGS & WITH_NAME_MAP) {
		m_name_to_handle.emplace(symbol.m_name, symbol.m_handle);
	}
}

template <typename SymbolType>
void SymbolList<SymbolType>::unlink_name_map(SymbolType& symbol)
{
	if constexpr(SymbolType::FLAGS & WITH_NAME_MAP) {
		auto iterators = m_name_to_handle.equal_range(symbol.m_name);
		for(auto iterator = iterators.first; iterator != iterators.second; iterator++) {
			if(iterator->second == symbol.m_handle) {
				m_name_to_handle.erase(iterator);
				break;
			}
		}
	}
}

#define CCC_X(SymbolType, symbol_list) template class SymbolList<SymbolType>;
CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X

// *****************************************************************************

Symbol::Symbol() {}
Symbol::~Symbol() {}

void Symbol::set_type(std::unique_ptr<ast::Node> type) {
	m_type = std::move(type);
	invalidate_node_handles();
}

// *****************************************************************************

const char* global_storage_location_to_string(GlobalStorageLocation location)
{
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
		case SUNDEFINED: return "sundefined";
	}
	return "";
}

// *****************************************************************************

void Function::set_parameter_variables(
	std::optional<ParameterVariableRange> range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database)
{
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

void Function::set_local_variables(
	std::optional<LocalVariableRange> range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database)
{
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

const std::string& Function::mangled_name() const
{
	if(!m_mangled_name.empty()) {
		return m_mangled_name;
	} else {
		return name();
	}
}

void Function::set_mangled_name(std::string mangled)
{
	m_mangled_name = std::move(mangled);
}

const std::string& GlobalVariable::mangled_name() const
{
	if(!m_mangled_name.empty()) {
		return m_mangled_name;
	} else {
		return name();
	}
}

void GlobalVariable::set_mangled_name(std::string mangled)
{
	m_mangled_name = std::move(mangled);
}

void SourceFile::set_functions(
	FunctionRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database)
{
	if(delete_old_symbols == DELETE_OLD_SYMBOLS) {
		database.functions.destroy_symbols(m_functions);
	}
	m_functions = range;
	for(Function& function : database.functions.span(range)) {
		function.m_source_file = m_handle;
	}
}

void SourceFile::set_global_variables(
	GlobalVariableRange range, ShouldDeleteOldSymbols delete_old_symbols, SymbolDatabase& database)
{
	if(delete_old_symbols == DELETE_OLD_SYMBOLS) {
		database.global_variables.destroy_symbols(m_global_variables);
	}
	m_global_variables = range;
	for(GlobalVariable& global_variable : database.global_variables.span(range)) {
		global_variable.m_source_file = m_handle;
	}
}

// *****************************************************************************

s32 SymbolDatabase::symbol_count() const
{
	s32 sum = 0;
	#define CCC_X(SymbolType, symbol_list) sum += symbol_list.size();
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
	#undef CCC_X
	return sum;
}

bool SymbolDatabase::symbol_exists_with_starting_address(Address address) const
{
	#define CCC_X(SymbolType, symbol_list) \
		if(symbol_list.first_handle_from_starting_address(address).valid()) { \
			return true; \
		}
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
	#undef CCC_X
	return false;
}

Result<SymbolSourceHandle> SymbolDatabase::get_symbol_source(const std::string& name)
{
	SymbolSourceHandle handle = symbol_sources.first_handle_from_name(name);
	if(!handle.valid()) {
		Result<SymbolSource*> source = symbol_sources.create_symbol(name, SymbolSourceHandle(), nullptr);
		CCC_RETURN_IF_ERROR(source);
		handle = (*source)->handle();
	}
	return handle;
}

void SymbolDatabase::clear()
{
	#define CCC_X(SymbolType, symbol_list) symbol_list.clear();
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
	#undef CCC_X
}

void SymbolDatabase::destroy_symbols_from_sources(SymbolSourceRange source_range)
{
	#define CCC_X(SymbolType, symbol_list) symbol_list.destroy_symbols_from_sources(source_range);
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
	#undef CCC_X
}

void SymbolDatabase::destroy_symbols_from_modules(ModuleRange module_range)
{
	#define CCC_X(SymbolType, symbol_list) symbol_list.destroy_symbols_from_modules(module_range);
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
	#undef CCC_X
}

Result<DataType*> SymbolDatabase::create_data_type_if_unique(
	std::unique_ptr<ast::Node> node,
	StabsTypeNumber number,
	const char* name,
	SourceFile& source_file,
	SymbolSourceHandle source,
	const Module* module_symbol)
{
	auto types_with_same_name = data_types.handles_from_name(name);
	const char* compare_fail_reason = nullptr;
	if(types_with_same_name.begin() == types_with_same_name.end()) {
		// No types with this name have previously been processed.
		Result<DataType*> data_type = data_types.create_symbol(name, source, module_symbol);
		CCC_RETURN_IF_ERROR(data_type);
		
		(*data_type)->files = {source_file.handle()};
		if(number.type > -1) {
			source_file.stabs_type_number_to_handle[number] = (*data_type)->handle();
		}
		
		(*data_type)->set_type(std::move(node));
		
		return *data_type;
	} else {
		// Types with this name have previously been processed, we need to
		// figure out if this one matches any of the previous ones.
		bool match = false;
		for(auto [key, existing_type_handle] : types_with_same_name) {
			DataType* existing_type = data_types.symbol_from_handle(existing_type_handle);
			CCC_ASSERT(existing_type);
			
			// We don't want to merge together types from different source so we
			// can destroy all the types from one source without breaking
			// anything else.
			if(existing_type->source() != source) {
				continue;
			}
			
			// We don't want to merge together types from different modules so
			// we can destroy all the types from one module without breaking
			// anything else.
			if(module_symbol) {
				if(existing_type->module_handle() != module_symbol->module_handle()) {
					continue;
				}
			} else {
				if(existing_type->module_handle().valid()) {
					continue;
				}
			}
			
			CCC_ASSERT(existing_type->type());
			ast::CompareResult compare_result = compare_nodes(*existing_type->type(), *node.get(), *this, true);
			if(compare_result.type == ast::CompareResultType::DIFFERS) {
				// The new node doesn't match this existing node.
				bool is_anonymous_enum = existing_type->type()->descriptor == ast::ENUM
					&& existing_type->name().empty();
				if(!is_anonymous_enum) {
					existing_type->compare_fail_reason = compare_fail_reason_to_string(compare_result.fail_reason);
					compare_fail_reason = compare_fail_reason_to_string(compare_result.fail_reason);
				}
			} else {
				// The new node matches this existing node.
				existing_type->files.emplace_back(source_file.handle());
				if(number.type > -1) {
					source_file.stabs_type_number_to_handle[number] = existing_type->handle();
				}
				if(compare_result.type == ast::CompareResultType::MATCHES_FAVOUR_RHS) {
					// The new node almost matches the old one, but the new one
					// is slightly better, so we replace the old type.
					existing_type->set_type(std::move(node));
				}
				match = true;
				break;
			}
		}
		if(!match) {
			// This type doesn't match the others with the same name that have
			// already been processed.
			Result<DataType*> data_type = data_types.create_symbol(name, source, module_symbol);
			CCC_RETURN_IF_ERROR(data_type);
			
			(*data_type)->files = {source_file.handle()};
			if(number.type > -1) {
				source_file.stabs_type_number_to_handle[number] = (*data_type)->handle();
			}
			(*data_type)->compare_fail_reason = compare_fail_reason;
			
			(*data_type)->set_type(std::move(node));
			
			return *data_type;
		}
	}
	
	return nullptr;
}

bool SymbolDatabase::destroy_function(FunctionHandle handle)
{
	Function* function = functions.symbol_from_handle(handle);
	if(!function) {
		return false;
	}
	if(function->parameter_variables().has_value()) {
		parameter_variables.destroy_symbols(*function->parameter_variables());
	}
	if(function->local_variables().has_value()) {
		local_variables.destroy_symbols(*function->local_variables());
	}
	return functions.destroy_symbol(handle);
}

// *****************************************************************************

NodeHandle::NodeHandle() {}

template <typename SymbolType>
NodeHandle::NodeHandle(const SymbolType& symbol, const ast::Node* node)
	: m_descriptor(SymbolType::DESCRIPTOR)
	, m_symbol_handle(symbol.handle().value)
	, m_node(node)
	, m_generation(symbol.generation()) {}

bool NodeHandle::valid() const
{
	return m_symbol_handle != (u32) -1;
}

const ast::Node* NodeHandle::lookup_node(const SymbolDatabase& database) const
{
	const Symbol* symbol = lookup_symbol(database);
	if(symbol && symbol->generation() == m_generation) {
		return m_node;
	} else {
		return nullptr;
	}
}

const Symbol* NodeHandle::lookup_symbol(const SymbolDatabase& database) const
{
	switch(m_descriptor) {
		#define CCC_X(SymbolType, symbol_list) \
			case SymbolType::DESCRIPTOR: \
				return database.symbol_list.symbol_from_handle(m_symbol_handle);
		CCC_FOR_EACH_SYMBOL_TYPE_DO_X
		#undef CCC_X
	}
	return nullptr;
}

NodeHandle NodeHandle::handle_for_child(const ast::Node* child_node) const
{
	NodeHandle child_handle;
	child_handle.m_descriptor = m_descriptor;
	child_handle.m_symbol_handle = m_symbol_handle;
	child_handle.m_node = child_node;
	child_handle.m_generation = m_generation;
	return child_handle;
}

#define CCC_X(SymbolType, symbol_list) template NodeHandle::NodeHandle(const SymbolType& symbol, const ast::Node* node);
CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X

}
