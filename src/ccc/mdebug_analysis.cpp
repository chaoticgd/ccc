// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "mdebug_analysis.h"

#include "stabs_to_ast.h"

namespace ccc::mdebug {

Result<void> LocalSymbolTableAnalyser::stab_magic(const char* magic) {
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::source_file(const char* path, Address text_address) {
	m_source_file.relative_path = path;
	m_source_file.text_address = text_address;
	if(m_next_relative_path.empty()) {
		m_next_relative_path = m_source_file.relative_path;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::data_type(const ParsedSymbol& symbol) {
	Result<std::unique_ptr<ast::Node>> node = stabs_type_to_ast(*symbol.name_colon_type.type.get(), m_stabs_to_ast_state, 0, 0, false, false);
	CCC_RETURN_IF_ERROR(node);
	
	(*node)->name = (symbol.name_colon_type.name == " ") ? "" : symbol.name_colon_type.name;
	if(symbol.name_colon_type.descriptor == mdebug::StabsSymbolDescriptor::TYPE_NAME) {
		(*node)->storage_class = ast::SC_TYPEDEF;
	}
	
	(*node)->stabs_type_number = symbol.name_colon_type.type->type_number;
	const char* name = (*node)->name.c_str();
	
	if(m_context.parser_flags & DONT_DEDUPLICATE_TYPES) {
		Result<DataType*> data_type = m_database.data_types.create_symbol(name, m_context.symbol_source);
		m_source_file.stabs_type_number_to_handle[(*node)->stabs_type_number] = (*data_type)->handle();
		(*data_type)->set_type_once(std::move(*node));
		
		(*data_type)->files = {m_source_file.handle()};
	} else {
		Result<ccc::DataType*> type = m_database.create_data_type_if_unique(std::move(*node), name, m_source_file, m_context.symbol_source);
		CCC_RETURN_IF_ERROR(type);
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::global_variable(const char* mangled_name, Address address, const StabsType& type, bool is_static, Variable::GlobalStorage::Location location) {
	std::optional<std::string> demangled_name = demangle_name(mangled_name);
	std::string name;
	if(demangled_name.has_value()) {
		name = std::move(*demangled_name);
	} else {
		name = std::move(mangled_name);
	}
	
	Result<GlobalVariable*> global = m_database.global_variables.create_symbol(name, m_context.symbol_source, address);
	CCC_RETURN_IF_ERROR(global);
	
	if(demangled_name.has_value()) {
		(*global)->set_mangled_name(mangled_name);
	}
	
	m_global_variables.expand_to_include((*global)->handle());
	
	
	
	Result<std::unique_ptr<ast::Node>> node = stabs_type_to_ast(type, m_stabs_to_ast_state, 0, 0, true, false);
	CCC_RETURN_IF_ERROR(node);
	
	if(is_static) {
		(*global)->storage_class = ast::SC_STATIC;
	}
	(*global)->set_type_once(std::move(*node));
	
	Variable::GlobalStorage global_storage;
	global_storage.location = location;
	global_storage.address = address;
	(*global)->set_storage_once(global_storage);
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::sub_source_file(const char* path, Address text_address) {
	if(m_current_function && m_state == IN_FUNCTION_BEGINNING) {
		Function::SubSourceFile& sub = m_current_function->sub_source_files.emplace_back();
		sub.address = text_address;
		sub.relative_path = path;
	} else {
		m_next_relative_path = path;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::procedure(const char* mangled_name, Address address, bool is_static) {
	if(!m_current_function || strcmp(mangled_name, m_current_function->mangled_name().c_str()) != 0) {
		Result<void> result = create_function(mangled_name, address);
		CCC_RETURN_IF_ERROR(result);
	}
	
	if(is_static) {
		m_current_function->storage_class = ast::SC_STATIC;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::label(const char* label, Address address, s32 line_number) {
	if(address.valid() && m_current_function && label[0] == '$') {
		Function::LineNumberPair& pair = m_current_function->line_numbers.emplace_back();
		pair.address = address;
		pair.line_number = line_number;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::text_end(const char* name, s32 function_size) {
	if(m_state == IN_FUNCTION_BEGINNING) {
		CCC_CHECK(m_current_function, "END TEXT symbol outside of function.");
		m_current_function->size = function_size;
		m_state = IN_FUNCTION_END;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::function(const char* mangled_name, const StabsType& return_type, Address address) {
	if(!m_current_function || strcmp(mangled_name, m_current_function->mangled_name().c_str())) {
		Result<void> result = create_function(mangled_name, address);
		CCC_RETURN_IF_ERROR(result);
	}
	
	Result<std::unique_ptr<ast::Node>> node = stabs_type_to_ast(return_type, m_stabs_to_ast_state, 0, 0, true, true);;
	CCC_RETURN_IF_ERROR(node);
	m_current_function->set_type_once(std::move(*node));
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::function_end() {
	if(m_current_function) {
		m_current_function->set_parameter_variables(m_current_parameter_variables, DONT_DELETE_OLD_SYMBOLS, m_database);
		m_current_function->set_local_variables(m_current_local_variables, DONT_DELETE_OLD_SYMBOLS, m_database);
	}
	
	m_current_function = nullptr;
	m_current_parameter_variables.clear();
	m_current_local_variables.clear();
	
	m_blocks.clear();
	m_pending_local_variables.clear();
	
	m_state = NOT_IN_FUNCTION;
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::parameter(const char* name, const StabsType& type, bool is_stack_variable, s32 offset_or_register, bool is_by_reference) {
	CCC_CHECK(m_current_function, "Parameter symbol before first func/proc symbol.");
	
	Result<ParameterVariable*> parameter_variable = m_database.parameter_variables.create_symbol(name, m_context.symbol_source);
	CCC_RETURN_IF_ERROR(parameter_variable);
	m_current_parameter_variables.expand_to_include((*parameter_variable)->handle());
	
	Result<std::unique_ptr<ast::Node>> node = stabs_type_to_ast(type, m_stabs_to_ast_state, 0, 0, true, true);
	CCC_RETURN_IF_ERROR(node);
	(*parameter_variable)->set_type_once(std::move(*node));
	
	if(is_stack_variable) {
		Variable::StackStorage stack_storage;
		stack_storage.stack_pointer_offset = offset_or_register;
		(*parameter_variable)->set_storage_once(stack_storage);
	} else {
		Variable::RegisterStorage register_storage;
		register_storage.dbx_register_number = offset_or_register;
		register_storage.is_by_reference = is_by_reference;
		(*parameter_variable)->set_storage_once(register_storage);
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::local_variable(const char* name, const StabsType& type, const Variable::Storage& storage, bool is_static) {
	if(!m_current_function) {
		return Result<void>();
	}
	
	const Variable::GlobalStorage* global_storage = std::get_if<Variable::GlobalStorage>(&storage);
	Address address = global_storage ? global_storage->address : Address();
	Result<LocalVariable*> local_variable = m_database.local_variables.create_symbol(name, m_context.symbol_source, address);
	CCC_RETURN_IF_ERROR(local_variable);
	m_current_local_variables.expand_to_include((*local_variable)->handle());
	m_pending_local_variables.emplace_back((*local_variable)->handle());
	
	Result<std::unique_ptr<ast::Node>> node = stabs_type_to_ast(type, m_stabs_to_ast_state, 0, 0, true, false);
	CCC_RETURN_IF_ERROR(node);
	
	if(is_static) {
		(*node)->storage_class = ast::SC_STATIC;
	}
	(*local_variable)->set_type_once(std::move(*node));
	
	(*local_variable)->set_storage_once(storage);
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::lbrac(s32 begin_offset) {
	for(LocalVariableHandle local_variable_handle : m_pending_local_variables) {
		if(LocalVariable* local_variable = m_database.local_variables.symbol_from_handle(local_variable_handle)) {
			local_variable->live_range.low = m_source_file.text_address.value + begin_offset;
		}
	}
	
	m_blocks.emplace_back(std::move(m_pending_local_variables));
	m_pending_local_variables = {};
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::rbrac(s32 end_offset) {
	CCC_CHECK(!m_blocks.empty(), "RBRAC symbol without a matching LBRAC symbol.");
	
	std::vector<LocalVariableHandle>& variables = m_blocks.back();
	for(LocalVariableHandle local_variable_handle : variables) {
		if(LocalVariable* local_variable = m_database.local_variables.symbol_from_handle(local_variable_handle)) {
			local_variable->live_range.high = m_source_file.text_address.value + end_offset;
		}
	}
	
	m_blocks.pop_back();
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::finish() {
	CCC_CHECK(m_state != IN_FUNCTION_BEGINNING,
		"Unexpected end of symbol table for '%s'.", m_source_file.name().c_str());
	
	if(m_current_function) {
		Result<void> result = function_end();
		CCC_RETURN_IF_ERROR(result);
	}
	
	m_source_file.set_functions(m_functions, DONT_DELETE_OLD_SYMBOLS, m_database);
	m_source_file.set_globals_variables(m_global_variables, DONT_DELETE_OLD_SYMBOLS, m_database);
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::create_function(const char* mangled_name, Address address) {
	if(m_current_function) {
		Result<void> result = function_end();
		CCC_RETURN_IF_ERROR(result);
	}
	
	std::optional<std::string> demangled_name = demangle_name(mangled_name);
	std::string name;
	if(demangled_name.has_value()) {
		name = std::move(*demangled_name);
	} else {
		name = std::move(mangled_name);
	}
	
	Result<Function*> function = m_database.functions.create_symbol(std::move(name), m_context.symbol_source, address);
	CCC_RETURN_IF_ERROR(function);
	m_current_function = *function;
	
	if(demangled_name.has_value()) {
		m_current_function->set_mangled_name(std::move(mangled_name));
	}
	
	m_functions.expand_to_include(m_current_function->handle());
	
	m_state = LocalSymbolTableAnalyser::IN_FUNCTION_BEGINNING;
	
	if(!m_next_relative_path.empty() && m_current_function->relative_path != m_source_file.relative_path) {
		m_current_function->relative_path = m_next_relative_path;
	}
	
	return Result<void>();
}

std::optional<std::string> LocalSymbolTableAnalyser::demangle_name(const char* mangled_name) {
	if(m_context.demangle) {
		const char* demangled_name = m_context.demangle(mangled_name, 0);
		if(demangled_name) {
			std::string name = demangled_name;
			free((void*) demangled_name);
			return name;
		}
	}
	return std::nullopt;
}

std::optional<Variable::GlobalStorage::Location> symbol_class_to_global_variable_location(SymbolClass symbol_class) {
	std::optional<Variable::GlobalStorage::Location> location;
	switch(symbol_class) {
		case SymbolClass::NIL: location = Variable::GlobalStorage::Location::NIL; break;
		case SymbolClass::DATA: location = Variable::GlobalStorage::Location::DATA; break;
		case SymbolClass::BSS: location = Variable::GlobalStorage::Location::BSS; break;
		case SymbolClass::ABS: location = Variable::GlobalStorage::Location::ABS; break;
		case SymbolClass::SDATA: location = Variable::GlobalStorage::Location::SDATA; break;
		case SymbolClass::SBSS: location = Variable::GlobalStorage::Location::SBSS; break;
		case SymbolClass::RDATA: location = Variable::GlobalStorage::Location::RDATA; break;
		case SymbolClass::COMMON: location = Variable::GlobalStorage::Location::COMMON; break;
		case SymbolClass::SCOMMON: location = Variable::GlobalStorage::Location::SCOMMON; break;
		case SymbolClass::SUNDEFINED: location = Variable::GlobalStorage::Location::SUNDEFINED; break;
		default: {}
	}
	return location;
}

}
