#include "analysis.h"

#include "stabs_to_ast.h"

namespace ccc {

struct AnalysisContext {
	const mdebug::SymbolTable* symbol_table;
	const std::map<std::string, const mdebug::Symbol*>* globals;
	u32 flags;
};

static Result<void> analyse_file(HighSymbolTable& high, ast::TypeDeduplicatorOMatic& deduplicator, s32 file_index, const AnalysisContext& context);
static void compute_size_bytes_recursive(ast::Node& node, const HighSymbolTable& high);
static std::optional<ast::GlobalVariableLocation> symbol_class_to_global_variable_location(mdebug::SymbolClass symbol_class);

class LocalSymbolTableAnalyser {
public:
	LocalSymbolTableAnalyser(ast::SourceFile& output, StabsToAstState& stabs_to_ast_state)
		: m_output(output), m_stabs_to_ast_state(stabs_to_ast_state) {}
	
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
	Result<void> stab_magic(const char* magic);
	Result<void> source_file(const char* path, u32 text_address);
	Result<void> data_type(const ParsedSymbol& symbol);
	Result<void> global_variable(const char* name, u32 address, const StabsType& type, bool is_static, ast::GlobalVariableLocation location);
	Result<void> sub_source_file(const char* name, u32 text_address);
	Result<void> procedure(const char* name, u32 address, bool is_static);
	Result<void> label(const char* label, u32 address, s32 line_number);
	Result<void> text_end(const char* name, s32 function_size);
	Result<void> function(const char* name, const StabsType& return_type, u32 function_address);
	Result<void> function_end();
	Result<void> parameter(const char* name, const StabsType& type, bool is_stack_variable, s32 offset_or_register, bool is_by_reference);
	Result<void> local_variable(const char* name, const StabsType& type, ast::VariableStorageType storage_type, s32 value, ast::GlobalVariableLocation location, bool is_static);
	Result<void> lbrac(s32 number, s32 begin_offset);
	Result<void> rbrac(s32 number, s32 end_offset);
	
	Result<void> finish();
	
	void create_function(const char* name);
	
protected:
	enum AnalysisState {
		NOT_IN_FUNCTION,
		IN_FUNCTION_BEGINNING,
		IN_FUNCTION_END
	};
	
	ast::SourceFile& m_output;
	StabsToAstState& m_stabs_to_ast_state;
	
	AnalysisState m_state = NOT_IN_FUNCTION;
	ast::FunctionDefinition* m_current_function = nullptr;
	ast::FunctionType* m_current_function_type = nullptr;
	std::vector<ast::Variable*> m_pending_variables_begin;
	std::map<s32, std::vector<ast::Variable*>> m_pending_variables_end;
	std::string m_next_relative_path;
};

static void filter_ast_by_flags(ast::Node& ast_node, u32 flags);

Result<HighSymbolTable> analyse(const mdebug::SymbolTable& symbol_table, u32 flags, s32 file_index) {
	HighSymbolTable high;
	
	Result<std::vector<mdebug::Symbol>> external_symbols = symbol_table.parse_external_symbols();
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
	
	ast::TypeDeduplicatorOMatic deduplicator;
	
	// Bundle together some unchanging state to pass to analyse_file.
	AnalysisContext context;
	context.symbol_table = &symbol_table;
	context.globals = &globals;
	context.flags = flags;
	
	Result<s32> file_count = symbol_table.file_count();
	CCC_RETURN_IF_ERROR(file_count);
	
	// Either analyse a specific file descriptor, or all of them.
	if(file_index > -1) {
		CCC_CHECK_FATAL(file_index < *file_count, "File index out of range.");
		Result<void> result = analyse_file(high, deduplicator, file_index, context);
		CCC_RETURN_IF_ERROR(result);
	} else {
		for(s32 i = 0; i < *file_count; i++) {
			Result<void> result = analyse_file(high, deduplicator, i, context);
			CCC_RETURN_IF_ERROR(result);
		}
	}
	
	// Deduplicate types from different translation units, preserving multiple
	// copies of types that actually differ.
	if(flags & DEDUPLICATE_TYPES) {
		high.deduplicated_types = deduplicator.finish();
		
		// The files field may be modified by further analysis passes, so we
		// need to save this information here.
		for(const std::unique_ptr<ast::Node>& node : high.deduplicated_types) {
			if(node->files.size() == 1) {
				node->probably_defined_in_cpp_file = true;
			}
		}
		
		// Compute size information for all nodes.
		for(std::unique_ptr<ast::SourceFile>& source_file : high.source_files) {
			compute_size_bytes_recursive(*source_file.get(), high);
		}
		for(std::unique_ptr<ast::Node>& type : high.deduplicated_types) {
			compute_size_bytes_recursive(*type.get(), high);
		}
	}
	
	return high;
}

static Result<void> analyse_file(HighSymbolTable& high, ast::TypeDeduplicatorOMatic& deduplicator, s32 file_index, const AnalysisContext& context) {
	Result<mdebug::File> input = context.symbol_table->parse_file(file_index);
	CCC_RETURN_IF_ERROR(input);
	
	auto file = std::make_unique<ast::SourceFile>();
	file->full_path = input->full_path;
	file->is_windows_path = input->is_windows_path;
	
	// Sometimes the INFO symbols contain information about what toolchain
	// version was used for building the executable.
	for(mdebug::Symbol& symbol : input->symbols) {
		if(symbol.storage_class == mdebug::SymbolClass::INFO && strcmp(symbol.string, "@stabs") != 0) {
			file->toolchain_version_info.emplace(symbol.string);
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
	stabs_to_ast_state.file_index = file_index;
	stabs_to_ast_state.stabs_types = &stabs_types;
	
	// Convert the parsed stabs symbols to a more standard C AST.
	LocalSymbolTableAnalyser analyser(*file.get(), stabs_to_ast_state);
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
						ast::VariableStorageType storage_type = ast::VariableStorageType::GLOBAL;
						ast::GlobalVariableLocation location = ast::GlobalVariableLocation::NIL;
						bool is_static = false;
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE) {
							storage_type = ast::VariableStorageType::GLOBAL;
							std::optional<ast::GlobalVariableLocation> location_opt = symbol_class_to_global_variable_location(symbol.raw->storage_class);
							CCC_CHECK(location_opt.has_value(), "Invalid static local variable location.");
							location = *location_opt;
							is_static = true;
						} else if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::REGISTER_VARIABLE) {
							storage_type = ast::VariableStorageType::REGISTER;
						} else {
							storage_type = ast::VariableStorageType::STACK;
						}
						Result<void> result = analyser.local_variable(name, type, storage_type, symbol.raw->value, location, is_static);
						CCC_RETURN_IF_ERROR(result);
						break;
					}
					case StabsSymbolDescriptor::GLOBAL_VARIABLE:
					case StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE: {
						const char* name = symbol.name_colon_type.name.c_str();
						u32 address = -1;
						std::optional<ast::GlobalVariableLocation> location = symbol_class_to_global_variable_location(symbol.raw->storage_class);
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
	ast::remove_duplicate_enums(file->data_types);
	
	// For some reason typedefs referencing themselves are generated along
	// with a proper struct of the same name.
	ast::remove_duplicate_self_typedefs(file->data_types);
	
	// Filter the AST depending on the flags parsed, removing things the
	// calling code didn't ask for.
	filter_ast_by_flags(*file, context.flags);
	
	high.source_files.emplace_back(std::move(file));
	
	// Deduplicate types.
	if(context.flags & DEDUPLICATE_TYPES) {
		deduplicator.process_file(*high.source_files.back().get(), file_index, high.source_files);
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::stab_magic(const char* magic) {
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::source_file(const char* path, u32 text_address) {
	m_output.relative_path = path;
	m_output.text_address = text_address;
	if(m_next_relative_path.empty()) {
		m_next_relative_path = m_output.relative_path;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::data_type(const ParsedSymbol& symbol) {
	Result<std::unique_ptr<ast::Node>> node = stabs_data_type_symbol_to_ast(symbol, m_stabs_to_ast_state);
	CCC_RETURN_IF_ERROR(node);
	(*node)->stabs_type_number = symbol.name_colon_type.type->type_number;
	m_output.data_types.emplace_back(std::move(*node));
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::global_variable(const char* name, u32 address, const StabsType& type, bool is_static, ast::GlobalVariableLocation location) {
	std::unique_ptr<ast::Variable> global = std::make_unique<ast::Variable>();
	global->name = name;
	if(is_static) {
		global->storage_class = ast::SC_STATIC;
	}
	global->variable_class = ast::VariableClass::GLOBAL;
	global->storage.type = ast::VariableStorageType::GLOBAL;
	global->storage.global_location = location;
	global->storage.global_address = address;
	global->type = stabs_type_to_ast_and_handle_errors(type, m_stabs_to_ast_state, 0, 0, true, false);
	m_output.globals.emplace_back(std::move(global));
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::sub_source_file(const char* path, u32 text_address) {
	if(m_current_function && m_state == IN_FUNCTION_BEGINNING) {
		ast::SubSourceFile& sub = m_current_function->sub_source_files.emplace_back();
		sub.address = text_address;
		sub.relative_path = path;
	} else {
		m_next_relative_path = path;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::procedure(const char* name, u32 address, bool is_static) {
	if(!m_current_function || strcmp(name, m_current_function->name.c_str())) {
		create_function(name);
	}
	
	m_current_function->address_range.low = address;
	if(is_static) {
		m_current_function->storage_class = ast::SC_STATIC;
	}
	
	m_pending_variables_begin.clear();
	m_pending_variables_end.clear();
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::label(const char* label, u32 address, s32 line_number) {
	if(address != (u32) -1 && m_current_function && label[0] == '$') {
		CCC_CHECK(address < 256 * 1024 * 1024, "Address too big.");
		ast::LineNumberPair& pair = m_current_function->line_numbers.emplace_back();
		pair.address = address;
		pair.line_number = line_number;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::text_end(const char* name, s32 function_size) {
	if(m_state == IN_FUNCTION_BEGINNING) {
		if(m_current_function->address_range.low != (u32) -1) {
			CCC_ASSERT(m_current_function);
			m_current_function->address_range.high = m_current_function->address_range.low + function_size;
		}
		m_state = IN_FUNCTION_END;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::function(const char* name, const StabsType& return_type, u32 function_address) {
	if(!m_current_function || strcmp(name, m_current_function->name.c_str())) {
		create_function(name);
	}
	
	m_current_function_type->return_type = stabs_type_to_ast_and_handle_errors(return_type, m_stabs_to_ast_state, 0, 0, true, true);
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::function_end() {
	m_current_function = nullptr;
	m_current_function_type = nullptr;
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::parameter(const char* name, const StabsType& type, bool is_stack_variable, s32 offset_or_register, bool is_by_reference) {
	CCC_CHECK(m_current_function_type, "Parameter symbol before first func/proc symbol.");
	std::unique_ptr<ast::Variable> parameter = std::make_unique<ast::Variable>();
	parameter->name = name;
	parameter->variable_class = ast::VariableClass::PARAMETER;
	if(is_stack_variable) {
		parameter->storage.type = ast::VariableStorageType::STACK;
		parameter->storage.stack_pointer_offset = offset_or_register;
	} else {
		parameter->storage.type = ast::VariableStorageType::REGISTER;
		parameter->storage.dbx_register_number = offset_or_register;
		parameter->storage.is_by_reference = is_by_reference;
	}
	parameter->type = stabs_type_to_ast_and_handle_errors(type, m_stabs_to_ast_state, 0, 0, true, true);
	m_current_function_type->parameters->emplace_back(std::move(parameter));
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::local_variable(const char* name, const StabsType& type, ast::VariableStorageType storage_type, s32 value, ast::GlobalVariableLocation location, bool is_static) {
	if(!m_current_function) {
		return Result<void>();
	}
	std::unique_ptr<ast::Variable> local = std::make_unique<ast::Variable>();
	m_pending_variables_begin.emplace_back(local.get());
	local->name = name;
	if(is_static) {
		local->storage_class = ast::SC_STATIC;
	}
	local->variable_class = ast::VariableClass::LOCAL;
	local->storage.type = storage_type;
	switch(storage_type) {
		case ast::VariableStorageType::GLOBAL: {
			local->storage.global_location = location;
			local->storage.global_address = value;
			break;
		}
		case ast::VariableStorageType::REGISTER: {
			local->storage.dbx_register_number = value;
			break;
		}
		case ast::VariableStorageType::STACK: {
			local->storage.stack_pointer_offset = value;
			break;
		}
	}
	local->type = stabs_type_to_ast_and_handle_errors(type, m_stabs_to_ast_state, 0, 0, true, false);
	m_current_function->locals.emplace_back(std::move(local));
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::lbrac(s32 number, s32 begin_offset) {
	auto& pending_end = m_pending_variables_end[number];
	for(ast::Variable* variable : m_pending_variables_begin) {
		pending_end.emplace_back(variable);
		variable->block.low = m_output.text_address + begin_offset;
	}
	m_pending_variables_begin.clear();
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::rbrac(s32 number, s32 end_offset) {
	auto variables = m_pending_variables_end.find(number);
	CCC_CHECK(variables != m_pending_variables_end.end(), "N_RBRAC symbol without a matching N_LBRAC symbol.");
	
	for(ast::Variable* variable : variables->second) {
		variable->block.high = m_output.text_address + end_offset;
	}
	
	return Result<void>();
}

Result<void> LocalSymbolTableAnalyser::finish() {
	CCC_CHECK(m_state != IN_FUNCTION_BEGINNING,
		"Unexpected end of symbol table for '%s'.", m_output.full_path.c_str());
	
	return Result<void>();
}

void LocalSymbolTableAnalyser::create_function(const char* name) {
	std::unique_ptr<ast::FunctionDefinition> ptr = std::make_unique<ast::FunctionDefinition>();
	m_current_function = ptr.get();
	m_output.functions.emplace_back(std::move(ptr));
	m_current_function->name = name;
	m_state = LocalSymbolTableAnalyser::IN_FUNCTION_BEGINNING;
	
	if(!m_next_relative_path.empty() && m_current_function->relative_path != m_output.relative_path) {
		m_current_function->relative_path = m_next_relative_path;
	}
	
	std::unique_ptr<ast::FunctionType> function_type = std::make_unique<ast::FunctionType>();
	m_current_function_type = function_type.get();
	m_current_function_type->parameters.emplace();
	m_current_function->type = std::move(function_type);
}

static void filter_ast_by_flags(ast::Node& ast_node, u32 flags) {
	for_each_node(ast_node, ast::PREORDER_TRAVERSAL, [&](ast::Node& node) {
		if(flags & STRIP_ACCESS_SPECIFIERS) {
			node.access_specifier = ast::AS_PUBLIC;
		}
		if(node.descriptor == ast::STRUCT_OR_UNION) {
			auto& struct_or_union = node.as<ast::StructOrUnion>();
			for(std::unique_ptr<ast::Node>& node : struct_or_union.fields) {
				// This allows us to deduplicate types with vtables.
				if(node->name.starts_with("$vf")) {
					node->name = "__vtable";
				}
				filter_ast_by_flags(*node.get(), flags);
			}
			if(flags & STRIP_MEMBER_FUNCTIONS) {
				struct_or_union.member_functions.clear();
			} else if(flags & STRIP_GENERATED_FUNCTIONS) {
				auto is_special = [](const ast::FunctionType& function, const std::string& name_no_template_args) {
					return function.name == "operator="
						|| function.name.starts_with("$")
						|| (function.name == name_no_template_args
							&& function.parameters->size() == 0);
				};
				
				std::string name_no_template_args =
					node.name.substr(0, node.name.find("<"));
				bool only_special_functions = true;
				for(size_t i = 0; i < struct_or_union.member_functions.size(); i++) {
					if(struct_or_union.member_functions[i]->descriptor == ast::NodeDescriptor::FUNCTION_TYPE) {
						ast::FunctionType& function = struct_or_union.member_functions[i]->as<ast::FunctionType>();
						if(!is_special(function, name_no_template_args)) {
							only_special_functions = false;
						}
					}
				}
				if(only_special_functions) {
					for(size_t i = 0; i < struct_or_union.member_functions.size(); i++) {
						if(struct_or_union.member_functions[i]->descriptor == ast::NodeDescriptor::FUNCTION_TYPE) {
							ast::FunctionType& function = struct_or_union.member_functions[i]->as<ast::FunctionType>();
							if(is_special(function, name_no_template_args)) {
								struct_or_union.member_functions.erase(struct_or_union.member_functions.begin() + i);
								i--;
							}
						}
					}
				}
			}
		}
		return ast::EXPLORE_CHILDREN;
	});
}

static void compute_size_bytes_recursive(ast::Node& node, const HighSymbolTable& high) {
	for_each_node(node, ast::POSTORDER_TRAVERSAL, [&](ast::Node& node) {
		if(node.computed_size_bytes > -1 || node.cannot_compute_size) {
			return ast::EXPLORE_CHILDREN;
		}
		node.cannot_compute_size = 1; // Can't compute size recursively.
		switch(node.descriptor) {
			case ast::ARRAY: {
				ast::Array& array = node.as<ast::Array>();
				if(array.element_type->computed_size_bytes > -1) {
					array.computed_size_bytes = array.element_type->computed_size_bytes * array.element_count;
				}
				break;
			}
			case ast::BITFIELD: {
				break;
			}
			case ast::BUILTIN: {
				ast::BuiltIn& built_in = node.as<ast::BuiltIn>();
				built_in.computed_size_bytes = builtin_class_size(built_in.bclass);
				break;
			}
			case ast::DATA: {
				break;
			}
			case ast::FUNCTION_DEFINITION: {
				break;
			}
			case ast::FUNCTION_TYPE: {
				break;
			}
			case ast::INITIALIZER_LIST: {
				break;
			}
			case ast::ENUM: {
				node.computed_size_bytes = 4;
				break;
			}
			case ast::STRUCT_OR_UNION: {
				node.computed_size_bytes = node.size_bits / 8;
				break;
			}
			case ast::POINTER_OR_REFERENCE: {
				node.computed_size_bytes = 4;
				break;
			}
			case ast::POINTER_TO_DATA_MEMBER: {
				break;
			}
			case ast::SOURCE_FILE: {
				break;
			}
			case ast::TYPE_NAME: {
				ast::TypeName& type_name = node.as<ast::TypeName>();
				if(type_name.referenced_file_index > -1 && type_name.referenced_stabs_type_number.type > -1) {
					const ast::SourceFile& source_file = *high.source_files[type_name.referenced_file_index].get();
					auto type_index = source_file.stabs_type_number_to_deduplicated_type_index.find(type_name.referenced_stabs_type_number);
					if(type_index != source_file.stabs_type_number_to_deduplicated_type_index.end()) {
						ast::Node& resolved_type = *high.deduplicated_types.at(type_index->second).get();
						if(resolved_type.computed_size_bytes < 0 && !resolved_type.cannot_compute_size) {
							compute_size_bytes_recursive(resolved_type, high);
						}
						type_name.computed_size_bytes = resolved_type.computed_size_bytes;
					}
				}
				break;
			}
			case ast::VARIABLE: {
				ast::Variable& variable = node.as<ast::Variable>();
				if(variable.type->computed_size_bytes > -1) {
					variable.computed_size_bytes = variable.type->computed_size_bytes;
				}
				break;
			}
		}
		if(node.computed_size_bytes > -1) {
			node.cannot_compute_size = 0;
		}
		return ast::EXPLORE_CHILDREN;
	});
}

static std::optional<ast::GlobalVariableLocation> symbol_class_to_global_variable_location(mdebug::SymbolClass symbol_class) {
	std::optional<ast::GlobalVariableLocation> location;
	switch(symbol_class) {
		case mdebug::SymbolClass::NIL: location = ast::GlobalVariableLocation::NIL; break;
		case mdebug::SymbolClass::DATA: location = ast::GlobalVariableLocation::DATA; break;
		case mdebug::SymbolClass::BSS: location = ast::GlobalVariableLocation::BSS; break;
		case mdebug::SymbolClass::ABS: location = ast::GlobalVariableLocation::ABS; break;
		case mdebug::SymbolClass::SDATA: location = ast::GlobalVariableLocation::SDATA; break;
		case mdebug::SymbolClass::SBSS: location = ast::GlobalVariableLocation::SBSS; break;
		case mdebug::SymbolClass::RDATA: location = ast::GlobalVariableLocation::RDATA; break;
		case mdebug::SymbolClass::COMMON: location = ast::GlobalVariableLocation::COMMON; break;
		case mdebug::SymbolClass::SCOMMON: location = ast::GlobalVariableLocation::SCOMMON; break;
		default: {}
	}
	return location;
}

std::map<std::string, s32> build_type_name_to_deduplicated_type_index_map(const HighSymbolTable& symbol_table) {
	std::map<std::string, s32> type_name_to_deduplicated_type_index;
	for(size_t i = 0; i < symbol_table.deduplicated_types.size(); i++) {
		ccc::ast::Node& type = *symbol_table.deduplicated_types[i].get();
		if(!type.name.empty()) {
			type_name_to_deduplicated_type_index.emplace(type.name, (s32) i);
		}
	}
	return type_name_to_deduplicated_type_index;
}

s32 lookup_type(const ast::TypeName& type_name, const HighSymbolTable& symbol_table, const std::map<std::string, s32>* type_name_to_deduplicated_type_index) {
	// Lookup the type by its STABS type number. This path ensures that the
	// correct type is found even if multiple types have the same name.
	if(type_name.referenced_file_index > -1 && type_name.referenced_stabs_type_number.type > -1) {
		const ast::SourceFile& source_file = *symbol_table.source_files[type_name.referenced_file_index].get();
		auto type_index = source_file.stabs_type_number_to_deduplicated_type_index.find(type_name.referenced_stabs_type_number);
		if(type_index != source_file.stabs_type_number_to_deduplicated_type_index.end()) {
			return type_index->second;
		}
	}
	// Looking up the type by its STABS type number failed, so look for it by
	// its name instead. This happens when a type is forward declared but not
	// defined in a given translation unit.
	if(type_name_to_deduplicated_type_index) {
		auto iter = type_name_to_deduplicated_type_index->find(type_name.type_name);
		if(iter != type_name_to_deduplicated_type_index->end()) {
			return iter->second;
		}
	}
	// Type lookup failed. This happens when a type is forward declared in a
	// translation unit with symbols but is not defined in one.
	return -1;
}

void fill_in_pointers_to_member_function_definitions(HighSymbolTable& high) {
	// Enumerate data types.
	std::map<std::string, ast::StructOrUnion*> type_name_to_node;
	for(std::unique_ptr<ast::Node>& type : high.deduplicated_types) {
		if(type->descriptor == ast::STRUCT_OR_UNION && !type->name.empty()) {
			type_name_to_node[type->name] = &type->as<ast::StructOrUnion>();
		}
	}
	
	// Fill in pointers from member function declaration to corresponding definitions.
	for(const std::unique_ptr<ast::SourceFile>& source_file : high.source_files) {
		for(const std::unique_ptr<ast::Node>& node : source_file->functions) {
			ast::FunctionDefinition& definition = node->as<ast::FunctionDefinition>();
			std::string::size_type name_separator_pos = definition.name.find_last_of("::");
			if(name_separator_pos != std::string::npos && name_separator_pos > 0) {
				std::string function_name = definition.name.substr(name_separator_pos + 1);
				// This won't work for some template types, and that's okay.
				std::string::size_type type_separator_pos = definition.name.find_last_of("::", name_separator_pos - 2);
				std::string type_name;
				if(type_separator_pos != std::string::npos) {
					type_name = definition.name.substr(type_separator_pos + 1, name_separator_pos - type_separator_pos - 2);
				} else {
					type_name = definition.name.substr(0, name_separator_pos - 1);
				}
				auto type = type_name_to_node.find(type_name);
				if(type != type_name_to_node.end()) {
					for(std::unique_ptr<ast::Node>& declaration : type->second->member_functions) {
						if(declaration->name == function_name) {
							declaration->as<ast::FunctionType>().definition = &definition;
							definition.is_member_function_ish = true;
						}
					}
				}
			}
		}
	}
}

}
