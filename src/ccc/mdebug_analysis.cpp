// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "mdebug_analysis.h"

#include "stabs_to_ast.h"
#include "symbol_table_formats.h"

namespace ccc {

struct AnalysisContext {
	const mdebug::SymbolTableReader* reader;
	const std::map<std::string, const mdebug::Symbol*>* globals;
};

static Result<void> analyse_file(SymbolTable& symbol_table, s32 file_index, const AnalysisContext& context, u32 parser_flags);
static std::optional<Variable::GlobalStorage::Location> symbol_class_to_global_variable_location(mdebug::SymbolClass symbol_class);

class LocalSymbolTableAnalyser {
public:
	LocalSymbolTableAnalyser(SymbolTable& symbol_table, StabsToAstState& stabs_to_ast_state, u32 parser_flags, SourceFile& source_file)
		: m_symbol_table(symbol_table)
		, m_stabs_to_ast_state(stabs_to_ast_state)
		, m_parser_flags(parser_flags)
		, m_source_file(source_file) {}
	
	// Functions for processing individual symbols.
	//
	// In most cases these symbols will appear in the following order:
	//   proc
	//   ... line numbers ...
	//   end
	//   func
	//   ... parameters ...
	//   ... blocks ...
	//   
	// For some compiler versions the symbols can appear in this order:
	//   func
	//   ... parameters ...
	//   $LM1
	//   proc
	//   ... line numbers ...
	//   end
	//   ... blocks ...
	[[nodiscard]] Result<void> stab_magic(const char* magic);
	[[nodiscard]] Result<void> source_file(const char* path, Address text_address);
	[[nodiscard]] Result<void> data_type(const ParsedSymbol& symbol);
	[[nodiscard]] Result<void> global_variable(const char* name, Address address, const StabsType& type, bool is_static, Variable::GlobalStorage::Location location);
	[[nodiscard]] Result<void> sub_source_file(const char* name, Address text_address);
	[[nodiscard]] Result<void> procedure(const char* name, Address address, bool is_static);
	[[nodiscard]] Result<void> label(const char* label, Address address, s32 line_number);
	[[nodiscard]] Result<void> text_end(const char* name, s32 function_size);
	[[nodiscard]] Result<void> function(const char* name, const StabsType& return_type, Address address);
	[[nodiscard]] Result<void> function_end();
	[[nodiscard]] Result<void> parameter(const char* name, const StabsType& type, bool is_stack_variable, s32 offset_or_register, bool is_by_reference);
	[[nodiscard]] Result<void> local_variable(const char* name, const StabsType& type, const Variable::Storage& storage, bool is_static);
	[[nodiscard]] Result<void> lbrac(s32 number, s32 begin_offset);
	[[nodiscard]] Result<void> rbrac(s32 number, s32 end_offset);
	
	[[nodiscard]] Result<void> finish();
	
	[[nodiscard]] Result<void> create_function(Address address, const char* name);
	
protected:
	enum AnalysisState {
		NOT_IN_FUNCTION,
		IN_FUNCTION_BEGINNING,
		IN_FUNCTION_END
	};
	
	SymbolTable& m_symbol_table;
	StabsToAstState& m_stabs_to_ast_state;
	u32 m_parser_flags;
	
	AnalysisState m_state = NOT_IN_FUNCTION;
	SourceFile& m_source_file;
	FunctionRange m_functions;
	Function* m_current_function = nullptr;
	ParameterVariableRange m_current_parameter_variables;
	LocalVariableRange m_current_local_variables;
	std::vector<LocalVariableHandle> m_pending_local_variables_begin;
	std::map<s32, std::vector<LocalVariableHandle>> m_pending_local_variables_end;
	std::string m_next_relative_path;
};

Result<SymbolTable> analyse(const mdebug::SymbolTableReader& reader, u32 parser_flags, s32 file_index) {
	SymbolTable symbol_table;
	
	Result<std::vector<mdebug::Symbol>> external_symbols = reader.parse_external_symbols();
	CCC_RETURN_IF_ERROR(external_symbols);
	
	// The addresses of the global variables aren't present in the local symbol
	// table, so here we extract them from the external table.
	std::map<std::string, const mdebug::Symbol*> globals;
	for(const mdebug::Symbol& external : *external_symbols) {
		if(external.storage_type == mdebug::SymbolType::GLOBAL
			&& (external.storage_class != mdebug::SymbolClass::UNDEFINED)) {
			globals[external.string] = &external;
		}
	}
	
	// Bundle together some unchanging state to pass to analyse_file.
	AnalysisContext context;
	context.reader = &reader;
	context.globals = &globals;
	
	Result<s32> file_count = reader.file_count();
	CCC_RETURN_IF_ERROR(file_count);
	
	// Either analyse a specific file descriptor, or all of them.
	if(file_index > -1) {
		CCC_CHECK_FATAL(file_index < *file_count, "File index out of range.");
		Result<void> result = analyse_file(symbol_table, file_index, context, parser_flags);
		CCC_RETURN_IF_ERROR(result);
	} else {
		for(s32 i = 0; i < *file_count; i++) {
			Result<void> result = analyse_file(symbol_table, i, context, parser_flags);
			CCC_RETURN_IF_ERROR(result);
		}
	}
	
	// The files field may be modified by further analysis passes, so we
	// need to save this information here.
	for(DataType& data_type : symbol_table.data_types) {
		if(data_type.files.size() == 1) {
			data_type.probably_defined_in_cpp_file = true;
		}
	}

	
	return symbol_table;
}

static Result<void> analyse_file(SymbolTable& symbol_table, s32 file_index, const AnalysisContext& context, u32 parser_flags) {
	Result<mdebug::File> input = context.reader->parse_file(file_index);
	CCC_RETURN_IF_ERROR(input);
	
	Result<SourceFile*> source_file = symbol_table.source_files.create_symbol(input->full_path);
	CCC_RETURN_IF_ERROR(source_file);
	
	// Sometimes the INFO symbols contain information about what toolchain
	// version was used for building the executable.
	for(mdebug::Symbol& symbol : input->symbols) {
		if(symbol.storage_class == mdebug::SymbolClass::INFO && strcmp(symbol.string, "@stabs") != 0) {
			(*source_file)->toolchain_version_info.emplace(symbol.string);
		}
	}
	
	// Parse the stab strings into a data structure that's vaguely
	// one-to-one with the text-based representation.
	Result<std::vector<ParsedSymbol>> symbols = parse_symbols(input->symbols, input->detected_language);
	CCC_RETURN_IF_ERROR(symbols);
	
	// In stabs, types can be referenced by their number from other stabs,
	// so here we build a map of type numbers to the parsed types.
	std::map<StabsTypeNumber, const StabsType*> stabs_types;
	for(const ParsedSymbol& symbol : *symbols) {
		if(symbol.type == ParsedSymbolType::NAME_COLON_TYPE) {
			symbol.name_colon_type.type->enumerate_numbered_types(stabs_types);
		}
	}
	
	StabsToAstState stabs_to_ast_state;
	stabs_to_ast_state.file_handle = (*source_file)->handle().value;
	stabs_to_ast_state.stabs_types = &stabs_types;
	
	// Convert the parsed stabs symbols to a more standard C AST.
	LocalSymbolTableAnalyser analyser(symbol_table, stabs_to_ast_state, parser_flags, **source_file);
	for(const ParsedSymbol& symbol : *symbols) {
		switch(symbol.type) {
			case ParsedSymbolType::NAME_COLON_TYPE: {
				switch(symbol.name_colon_type.descriptor) {
					case StabsSymbolDescriptor::LOCAL_FUNCTION:
					case StabsSymbolDescriptor::GLOBAL_FUNCTION: {
						const char* name = symbol.name_colon_type.name.c_str();
						const StabsType& type = *symbol.name_colon_type.type.get();
						Result<void> result = analyser.function(name, type, symbol.raw->value);
						CCC_RETURN_IF_ERROR(result);
						break;
					}
					case StabsSymbolDescriptor::REFERENCE_PARAMETER_A:
					case StabsSymbolDescriptor::REGISTER_PARAMETER:
					case StabsSymbolDescriptor::VALUE_PARAMETER:
					case StabsSymbolDescriptor::REFERENCE_PARAMETER_V: {
						const char* name = symbol.name_colon_type.name.c_str();
						const StabsType& type = *symbol.name_colon_type.type.get();
						bool is_stack_variable = symbol.name_colon_type.descriptor == StabsSymbolDescriptor::VALUE_PARAMETER;
						bool is_by_reference = symbol.name_colon_type.descriptor == StabsSymbolDescriptor::REFERENCE_PARAMETER_A
							|| symbol.name_colon_type.descriptor == StabsSymbolDescriptor::REFERENCE_PARAMETER_V;
						
						Result<void> result = analyser.parameter(name, type, is_stack_variable, symbol.raw->value, is_by_reference);
						CCC_RETURN_IF_ERROR(result);
						break;
					}
					case StabsSymbolDescriptor::REGISTER_VARIABLE:
					case StabsSymbolDescriptor::LOCAL_VARIABLE:
					case StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE: {
						const char* name = symbol.name_colon_type.name.c_str();
						const StabsType& type = *symbol.name_colon_type.type.get();
						Variable::Storage storage;
						bool is_static = false;
						
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE) {
							Variable::GlobalStorage global_storage;
							std::optional<Variable::GlobalStorage::Location> location_opt =
								symbol_class_to_global_variable_location(symbol.raw->storage_class);
							CCC_CHECK(location_opt.has_value(), "Invalid static local variable location.");
							global_storage.location = *location_opt;
							global_storage.address = symbol.raw->value;
							storage = global_storage;
							is_static = true;
						} else if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::REGISTER_VARIABLE) {
							Variable::RegisterStorage register_storage;
							register_storage.dbx_register_number = symbol.raw->value;
							storage = register_storage;
						} else {
							Variable::StackStorage stack_storage;
							stack_storage.stack_pointer_offset = symbol.raw->value;
							storage = stack_storage;
						}
						
						Result<void> result = analyser.local_variable(name, type, storage, is_static);
						CCC_RETURN_IF_ERROR(result);
						break;
					}
					case StabsSymbolDescriptor::GLOBAL_VARIABLE:
					case StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE: {
						const char* name = symbol.name_colon_type.name.c_str();
						u32 address = -1;
						std::optional<Variable::GlobalStorage::Location> location =
							symbol_class_to_global_variable_location(symbol.raw->storage_class);
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::GLOBAL_VARIABLE) {
							// The address for non-static global variables is
							// only stored in the external symbol table (and
							// the ELF symbol table), so we pull that
							// information in here.
							auto global_symbol = context.globals->find(symbol.name_colon_type.name);
							if(global_symbol != context.globals->end()) {
								address = (u32) global_symbol->second->value;
								location = symbol_class_to_global_variable_location(global_symbol->second->storage_class);
							}
						} else {
							// And for static global variables it's just stored
							// in the local symbol table.
							address = (u32) symbol.raw->value;
						}
						CCC_CHECK(location.has_value(), "Invalid global variable location.")
						const StabsType& type = *symbol.name_colon_type.type.get();
						bool is_static = symbol.name_colon_type.descriptor == StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE;
						Result<void> result = analyser.global_variable(name, address, type, is_static, *location);
						CCC_RETURN_IF_ERROR(result);
						break;
					}
					case StabsSymbolDescriptor::TYPE_NAME:
					case StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG: {
						Result<void> result = analyser.data_type(symbol);
						CCC_RETURN_IF_ERROR(result);
						break;
					}
				}
				break;
			}
			case ParsedSymbolType::SOURCE_FILE: {
				Result<void> result = analyser.source_file(symbol.raw->string, symbol.raw->value);
				CCC_RETURN_IF_ERROR(result);
				break;
			}
			case ParsedSymbolType::SUB_SOURCE_FILE: {
				Result<void> result = analyser.sub_source_file(symbol.raw->string, symbol.raw->value);
				CCC_RETURN_IF_ERROR(result);
				break;
			}
			case ParsedSymbolType::LBRAC: {
				Result<void> result = analyser.lbrac(symbol.lrbrac.number, symbol.raw->value);
				CCC_RETURN_IF_ERROR(result);
				break;
			}
			case ParsedSymbolType::RBRAC: {
				Result<void> result = analyser.rbrac(symbol.lrbrac.number, symbol.raw->value);
				CCC_RETURN_IF_ERROR(result);
				break;
			}
			case ParsedSymbolType::FUNCTION_END: {
				Result<void> result = analyser.function_end();
				CCC_RETURN_IF_ERROR(result);
				break;
			}
			case ParsedSymbolType::NON_STABS: {
				if(symbol.raw->storage_class == mdebug::SymbolClass::TEXT) {
					if(symbol.raw->storage_type == mdebug::SymbolType::PROC) {
						Result<void> result = analyser.procedure(symbol.raw->string, symbol.raw->value, false);
						CCC_RETURN_IF_ERROR(result);
					} else if(symbol.raw->storage_type == mdebug::SymbolType::STATICPROC) {
						Result<void> result = analyser.procedure(symbol.raw->string, symbol.raw->value, true);
						CCC_RETURN_IF_ERROR(result);
					} else if(symbol.raw->storage_type == mdebug::SymbolType::LABEL) {
						Result<void> result = analyser.label(symbol.raw->string, symbol.raw->value, symbol.raw->index);
						CCC_RETURN_IF_ERROR(result);
					} else if(symbol.raw->storage_type == mdebug::SymbolType::END) {
						Result<void> result = analyser.text_end(symbol.raw->string, symbol.raw->value);
						CCC_RETURN_IF_ERROR(result);
					}
				}
				break;
			}
		}
	}
	
	Result<void> result = analyser.finish();
	CCC_RETURN_IF_ERROR(result);
	
	// The STABS types are no longer needed, so delete them now.
	symbols->clear();
	
	// Some enums have two separate stabs generated for them, one with a
	// name of " ", where one stab references the other. Remove these
	// duplicate AST nodes.
	//ast::remove_duplicate_enums(source_file.data_types);
	
	// For some reason typedefs referencing themselves are generated along
	// with a proper struct of the same name.
	//ast::remove_duplicate_self_typedefs(source_file.data_types);
	
	return Result<void>();
}

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
	Result<std::unique_ptr<ast::Node>> node = stabs_data_type_symbol_to_ast(symbol, m_stabs_to_ast_state);
	CCC_RETURN_IF_ERROR(node);
	(*node)->stabs_type_number = symbol.name_colon_type.type->type_number;
	const char* name = (*node)->name.c_str();
	
	if(m_parser_flags & DONT_DEDUPLICATE_TYPES) {
		Result<DataType*> data_type = m_symbol_table.data_types.create_symbol(name);
		m_source_file.stabs_type_number_to_handle[(*node)->stabs_type_number] = (*data_type)->handle();
		(*data_type)->set_type_and_invalidate_node_handles(std::move(*node));
	} else {
		Result<ccc::DataType*> type = m_symbol_table.create_data_type_if_unique(std::move(*node), name, m_source_file);
		CCC_RETURN_IF_ERROR(type);
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::global_variable(const char* name, Address address, const StabsType& type, bool is_static, Variable::GlobalStorage::Location location) {
	Result<GlobalVariable*> global = m_symbol_table.global_variables.create_symbol(name, address);
	CCC_RETURN_IF_ERROR(global);
	
	std::unique_ptr<ast::Node> node = stabs_type_to_ast_and_handle_errors(type, m_stabs_to_ast_state, 0, 0, true, false);
	if(is_static) {
		node->storage_class = ast::SC_STATIC;
	}
	(*global)->set_type_and_invalidate_node_handles(std::move(node));
	
	(*global)->variable_class = Variable::Class::GLOBAL;
	Variable::GlobalStorage global_storage;
	global_storage.location = location;
	global_storage.address = address;
	(*global)->storage = global_storage;
	
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

Result<void> LocalSymbolTableAnalyser::procedure(const char* name, Address address, bool is_static) {
	if(!m_current_function || strcmp(name, m_current_function->name().c_str())) {
		Result<void> result = create_function(address, name);
		CCC_RETURN_IF_ERROR(result);
	}
	
	m_current_function->address_range.low = address;
	if(is_static) {
		m_current_function->storage_class = ast::SC_STATIC;
	}
	
	m_pending_local_variables_begin.clear();
	m_pending_local_variables_end.clear();
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::label(const char* label, Address address, s32 line_number) {
	if(address != (u32) -1 && m_current_function && label[0] == '$') {
		CCC_CHECK(address < 256 * 1024 * 1024, "Address too big.");
		Function::LineNumberPair& pair = m_current_function->line_numbers.emplace_back();
		pair.address = address;
		pair.line_number = line_number;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::text_end(const char* name, s32 function_size) {
	if(m_state == IN_FUNCTION_BEGINNING) {
		if(m_current_function->address_range.low != (u32) -1) {
			CCC_ASSERT(m_current_function);
			m_current_function->address_range.high = m_current_function->address_range.low.value + function_size;
		}
		m_state = IN_FUNCTION_END;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::function(const char* name, const StabsType& return_type, Address address) {
	if(!m_current_function || strcmp(name, m_current_function->name().c_str())) {
		Result<void> result = create_function(address, name);
		CCC_RETURN_IF_ERROR(result);
	}
	
	std::unique_ptr<ast::Node> node = stabs_type_to_ast_and_handle_errors(return_type, m_stabs_to_ast_state, 0, 0, true, true);;
	m_current_function->set_type_and_invalidate_node_handles(std::move(node));
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::function_end() {
	if(m_current_function) {
		m_current_function->set_parameter_variables(m_current_parameter_variables, DONT_DELETE_OLD_SYMBOLS, m_symbol_table);
		m_current_function->set_local_variables(m_current_local_variables, DONT_DELETE_OLD_SYMBOLS, m_symbol_table);
	}
	
	m_current_function = nullptr;
	m_current_parameter_variables.clear();
	m_current_local_variables.clear();
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::parameter(const char* name, const StabsType& type, bool is_stack_variable, s32 offset_or_register, bool is_by_reference) {
	CCC_CHECK(m_current_function, "Parameter symbol before first func/proc symbol.");
	
	Result<ParameterVariable*> parameter_variable = m_symbol_table.parameter_variables.create_symbol(name);
	CCC_RETURN_IF_ERROR(parameter_variable);
	m_current_parameter_variables.expand_to_include((*parameter_variable)->handle());
	
	(*parameter_variable)->set_type_and_invalidate_node_handles(stabs_type_to_ast_and_handle_errors(type, m_stabs_to_ast_state, 0, 0, true, true));
	
	(*parameter_variable)->variable_class = Variable::Class::PARAMETER;
	if(is_stack_variable) {
		Variable::StackStorage stack_storage;
		stack_storage.stack_pointer_offset = offset_or_register;
		(*parameter_variable)->storage = stack_storage;
	} else {
		Variable::RegisterStorage register_storage;
		register_storage.dbx_register_number = offset_or_register;
		register_storage.is_by_reference = is_by_reference;
		(*parameter_variable)->storage = register_storage;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::local_variable(const char* name, const StabsType& type, const Variable::Storage& storage, bool is_static) {
	if(!m_current_function) {
		return Result<void>();
	}
	
	Result<LocalVariable*> local_variable = m_symbol_table.local_variables.create_symbol(name);
	CCC_RETURN_IF_ERROR(local_variable);
	m_pending_local_variables_begin.emplace_back((*local_variable)->handle());
	
	std::unique_ptr<ast::Node> node = stabs_type_to_ast_and_handle_errors(type, m_stabs_to_ast_state, 0, 0, true, false);
	if(is_static) {
		node->storage_class = ast::SC_STATIC;
	}
	(*local_variable)->set_type_and_invalidate_node_handles(std::move(node));
	
	(*local_variable)->variable_class = Variable::Class::LOCAL;
	(*local_variable)->storage = storage;
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::lbrac(s32 number, s32 begin_offset) {
	for(LocalVariableHandle local_variable_handle : m_pending_local_variables_begin) {
		LocalVariable* local_variable = m_symbol_table.local_variables[local_variable_handle];
		CCC_ASSERT(local_variable);
		local_variable->live_range.low = m_source_file.text_address.value + begin_offset;
	}
	
	auto& pending_end = m_pending_local_variables_end[number];
	pending_end.insert(pending_end.end(), CCC_BEGIN_END(m_pending_local_variables_begin));
	m_pending_local_variables_begin.clear();
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::rbrac(s32 number, s32 end_offset) {
	auto variables = m_pending_local_variables_end.find(number);
	CCC_CHECK(variables != m_pending_local_variables_end.end(), "N_RBRAC symbol without a matching N_LBRAC symbol.");
	
	for(LocalVariableHandle local_variable_handle : variables->second) {
		LocalVariable* local_variable = m_symbol_table.local_variables[local_variable_handle];
		CCC_ASSERT(local_variable);
		local_variable->live_range.high = m_source_file.text_address.value + end_offset;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::finish() {
	CCC_CHECK(m_state != IN_FUNCTION_BEGINNING,
		"Unexpected end of symbol table for '%s'.", m_source_file.name().c_str());
	
	m_source_file.set_functions(m_functions, DONT_DELETE_OLD_SYMBOLS, m_symbol_table);
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::create_function(Address address, const char* name) {
	Result<Function*> function = m_symbol_table.functions.create_symbol(name, address);
	CCC_RETURN_IF_ERROR(function);
	m_current_function = *function;
	m_functions.expand_to_include(m_current_function->handle());
	
	m_state = LocalSymbolTableAnalyser::IN_FUNCTION_BEGINNING;
	
	if(!m_next_relative_path.empty() && m_current_function->relative_path != m_source_file.relative_path) {
		m_current_function->relative_path = m_next_relative_path;
	}
	
	return Result<void>();
}

static std::optional<Variable::GlobalStorage::Location> symbol_class_to_global_variable_location(mdebug::SymbolClass symbol_class) {
	std::optional<Variable::GlobalStorage::Location> location;
	switch(symbol_class) {
		case mdebug::SymbolClass::NIL: location = Variable::GlobalStorage::Location::NIL; break;
		case mdebug::SymbolClass::DATA: location = Variable::GlobalStorage::Location::DATA; break;
		case mdebug::SymbolClass::BSS: location = Variable::GlobalStorage::Location::BSS; break;
		case mdebug::SymbolClass::ABS: location = Variable::GlobalStorage::Location::ABS; break;
		case mdebug::SymbolClass::SDATA: location = Variable::GlobalStorage::Location::SDATA; break;
		case mdebug::SymbolClass::SBSS: location = Variable::GlobalStorage::Location::SBSS; break;
		case mdebug::SymbolClass::RDATA: location = Variable::GlobalStorage::Location::RDATA; break;
		case mdebug::SymbolClass::COMMON: location = Variable::GlobalStorage::Location::COMMON; break;
		case mdebug::SymbolClass::SCOMMON: location = Variable::GlobalStorage::Location::SCOMMON; break;
		default: {}
	}
	return location;
}

void fill_in_pointers_to_member_function_definitions(SymbolTable& symbol_table) {
	//// Enumerate data types.
	//std::map<std::string, ast::StructOrUnion*> type_name_to_node;
	//for(std::unique_ptr<ast::Node>& type : high.deduplicated_types) {
	//	if(type->descriptor == ast::STRUCT_OR_UNION && !type->name.empty()) {
	//		type_name_to_node[type->name] = &type->as<ast::StructOrUnion>();
	//	}
	//}
	//
	//// Fill in pointers from member function declaration to corresponding definitions.
	//for(const std::unique_ptr<ast::SourceFile>& source_file : high.source_files) {
	//	for(const std::unique_ptr<ast::Node>& node : source_file->functions) {
	//		ast::FunctionDefinition& definition = node->as<ast::FunctionDefinition>();
	//		std::string::size_type name_separator_pos = definition.name.find_last_of("::");
	//		if(name_separator_pos != std::string::npos && name_separator_pos > 0) {
	//			std::string function_name = definition.name.substr(name_separator_pos + 1);
	//			// This won't work for some template types, and that's okay.
	//			std::string::size_type type_separator_pos = definition.name.find_last_of("::", name_separator_pos - 2);
	//			std::string type_name;
	//			if(type_separator_pos != std::string::npos) {
	//				type_name = definition.name.substr(type_separator_pos + 1, name_separator_pos - type_separator_pos - 2);
	//			} else {
	//				type_name = definition.name.substr(0, name_separator_pos - 1);
	//			}
	//			auto type = type_name_to_node.find(type_name);
	//			if(type != type_name_to_node.end()) {
	//				for(std::unique_ptr<ast::Node>& declaration : type->second->member_functions) {
	//					if(declaration->name == function_name) {
	//						declaration->as<ast::FunctionType>().definition = &definition;
	//						definition.is_member_function_ish = true;
	//					}
	//				}
	//			}
	//		}
	//	}
	//}
}

}