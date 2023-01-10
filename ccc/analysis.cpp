#include "analysis.h"

namespace ccc {

// read_symbol_table
// analyse_program
// analyse_file
static void filter_ast_by_flags(ast::Node& ast_node, u32 flags);
// scan_for_functions

mdebug::SymbolTable read_symbol_table(const std::vector<Module*>& modules) {
	std::optional<mdebug::SymbolTable> symbol_table;
	for(Module* mod : modules) {
		ModuleSection* mdebug_section = mod->lookup_section(".mdebug");
		if(mdebug_section) {
			symbol_table = mdebug::parse_symbol_table(*mod, *mdebug_section);
			break;
		}
	}
	verify(symbol_table.has_value(), "No .mdebug section!");
	return *symbol_table;
}

AnalysisResults analyse(const mdebug::SymbolTable& symbol_table, u32 flags, s32 file_descriptor_index) {
	AnalysisResults results;
	
	// The addresses of the global variables aren't present in the local symbol
	// table, so here we extract them from the external table.
	std::map<std::string, ExternalGlobalVariable> global_addresses;
	for(const mdebug::Symbol& external : symbol_table.externals) {
		if(external.storage_type == mdebug::SymbolType::GLOBAL) {
			global_addresses[external.string] = {external.value, external.storage_class};
		}
	}
	
	// Either analyse a specific file descriptor, or all of them.
	if(file_descriptor_index > -1) {
		assert(file_descriptor_index < symbol_table.files.size());
		analyse_file(results, symbol_table, symbol_table.files[file_descriptor_index], global_addresses, file_descriptor_index, flags);
	} else {
		for(s32 i = 0; i < (s32) symbol_table.files.size(); i++) {
			const mdebug::SymFileDescriptor& fd = symbol_table.files[i];
			analyse_file(results, symbol_table, fd, global_addresses, i, flags);
		}
	}
	
	for(std::unique_ptr<ast::SourceFile>& source_file : results.source_files) {
		// Some enums have two separate stabs generated for them, one with a
		// name of " ", where one stab references the other. Remove these
		// duplicate AST nodes.
		ast::remove_duplicate_enums(source_file->types);
		// For some reason typedefs referencing themselves are generated along
		// with a proper struct of the same name.
		ast::remove_duplicate_self_typedefs(source_file->types);
		// Filter the AST depending on the flags parsed, removing things the
		// calling code didn't ask for.
		filter_ast_by_flags(*source_file, flags);
	}
	
	// Deduplicate types from different translation units, preserving multiple
	// copies of types that actually differ.
	if(flags & DEDUPLICATE_TYPES) {
		results.deduplicated_types = ast::deduplicate_types(results.source_files);
	}
	
	return results;
}

void analyse_file(AnalysisResults& results, const mdebug::SymbolTable& symbol_table, const mdebug::SymFileDescriptor& fd, const std::map<std::string, ExternalGlobalVariable>& global_addresses, s32 file_index, u32 flags) {
	auto file = std::make_unique<ast::SourceFile>();
	file->full_path = fd.full_path;
	// Parse the stab strings into a data structure that's vaguely
	// one-to-one with the text-based representation.
	file->symbols = parse_symbols(fd.symbols, fd.detected_language);
	// In stabs, types can be referenced by their number from other stabs,
	// so here we build a map of type numbers to the parsed types.
	std::map<s32, const StabsType*> stabs_types;
	for(const ParsedSymbol& symbol : file->symbols) {
		if(symbol.type == ParsedSymbolType::NAME_COLON_TYPE) {
			symbol.name_colon_type.type->enumerate_numbered_types(stabs_types);
		}
	}
	
	ast::StabsToAstState stabs_to_ast_state;
	stabs_to_ast_state.file_index = file_index;
	stabs_to_ast_state.stabs_types = &stabs_types;
	
	// Convert the parsed stabs symbols to a more standard C AST.
	LocalSymbolTableAnalyser analyser(*file.get(), stabs_to_ast_state);
	for(const ParsedSymbol& symbol : file->symbols) {
		switch(symbol.type) {
			case ParsedSymbolType::NAME_COLON_TYPE: {
				switch(symbol.name_colon_type.descriptor) {
					case StabsSymbolDescriptor::LOCAL_FUNCTION:
					case StabsSymbolDescriptor::GLOBAL_FUNCTION: {
						const char* name = symbol.name_colon_type.name.c_str();
						const StabsType& type = *symbol.name_colon_type.type.get();
						analyser.return_type(name, type, symbol.raw->value);
						break;
					}
					case StabsSymbolDescriptor::REFERENCE_PARAMETER:
					case StabsSymbolDescriptor::REGISTER_PARAMETER:
					case StabsSymbolDescriptor::VALUE_PARAMETER: {
						const char* name = symbol.name_colon_type.name.c_str();
						const StabsType& type = *symbol.name_colon_type.type.get();
						bool is_stack_variable = symbol.name_colon_type.descriptor == StabsSymbolDescriptor::VALUE_PARAMETER;
						analyser.parameter(name, type, is_stack_variable, symbol.raw->value);
						break;
					}
					case StabsSymbolDescriptor::REGISTER_VARIABLE:
					case StabsSymbolDescriptor::LOCAL_VARIABLE:
					case StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE: {
						const char* name = symbol.name_colon_type.name.c_str();
						const StabsType& type = *symbol.name_colon_type.type.get();
						bool is_register_variable = symbol.name_colon_type.descriptor == StabsSymbolDescriptor::REGISTER_VARIABLE;
						ast::VariableStorageType storage_type = ast::VariableStorageType::GLOBAL;
						ast::GlobalVariableLocation location = ast::GlobalVariableLocation::NIL;
						bool is_static = false;
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE) {
							storage_type = ast::VariableStorageType::GLOBAL;
							location = symbol_class_to_global_variable_location(symbol.raw->storage_class);
							is_static = true;
						} else if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::REGISTER_VARIABLE) {
							storage_type = ast::VariableStorageType::REGISTER;
						} else {
							storage_type = ast::VariableStorageType::STACK;
						}
						analyser.local_variable(name, type, storage_type, symbol.raw->value, location, is_static);
						break;
					}
					case StabsSymbolDescriptor::GLOBAL_VARIABLE:
					case StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE: {
						const char* name = symbol.name_colon_type.name.c_str();
						s32 address = -1;
						ast::GlobalVariableLocation location = symbol_class_to_global_variable_location(symbol.raw->storage_class);
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::GLOBAL_VARIABLE) {
							// The address for non-static global variables is
							// only stored in the external symbol table (and
							// the ELF symbol table), so we pull that
							// information in here.
							auto global_address = global_addresses.find(symbol.name_colon_type.name);
							if(global_address != global_addresses.end()) {
								address = global_address->second.address;
								location = symbol_class_to_global_variable_location(global_address->second.location);
							}
						} else {
							// And for static global variables it's just stored
							// in the local symbol table.
							address = symbol.raw->value;
						}
						const StabsType& type = *symbol.name_colon_type.type.get();
						bool is_static = symbol.name_colon_type.descriptor == StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE;
						analyser.global_variable(name, address, type, is_static, location);
						break;
					}
					case StabsSymbolDescriptor::TYPE_NAME:
					case StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG: {
						analyser.data_type(symbol);
						break;
					}
				}
				break;
			}
			case ParsedSymbolType::SOURCE_FILE: {
				analyser.source_file(symbol.raw->string, symbol.raw->value);
				break;
			}
			case ParsedSymbolType::SUB_SOURCE_FILE: {
				analyser.sub_source_file(symbol.raw->string, symbol.raw->value);
				break;
			}
			case ParsedSymbolType::SCOPE_BEGIN: {
				analyser.lbrac(symbol.scope.number, symbol.raw->value);
				break;
			}
			case ParsedSymbolType::SCOPE_END: {
				analyser.rbrac(symbol.scope.number, symbol.raw->value);
				break;
			}
			case ParsedSymbolType::NON_STABS: {
				if(symbol.raw->storage_class == mdebug::SymbolClass::TEXT) {
					if(symbol.raw->storage_type == mdebug::SymbolType::PROC) {
						analyser.function(symbol.raw->string, symbol.raw->value, false);
					} else if(symbol.raw->storage_type == mdebug::SymbolType::STATICPROC) {
						analyser.function(symbol.raw->string, symbol.raw->value, true);
					} else if(symbol.raw->storage_type == mdebug::SymbolType::END) {
						analyser.text_end(symbol.raw->string, symbol.raw->value);
					}
				}
				break;
			}
		}
	}
	
	results.source_files.emplace_back(std::move(file));
}

ast::GlobalVariableLocation symbol_class_to_global_variable_location(mdebug::SymbolClass symbol_class) {
	switch(symbol_class) {
		case mdebug::SymbolClass::NIL: return ast::GlobalVariableLocation::NIL;
		case mdebug::SymbolClass::DATA: return ast::GlobalVariableLocation::DATA;
		case mdebug::SymbolClass::BSS: return ast::GlobalVariableLocation::BSS;
		case mdebug::SymbolClass::ABS: return ast::GlobalVariableLocation::ABS;
		case mdebug::SymbolClass::SDATA: return ast::GlobalVariableLocation::SDATA;
		case mdebug::SymbolClass::SBSS: return ast::GlobalVariableLocation::SBSS;
		case mdebug::SymbolClass::RDATA: return ast::GlobalVariableLocation::RDATA;
		default: {}
	}
	verify_not_reached("Bad variable storage location '%s'.", mdebug::symbol_class(symbol_class));
}

void LocalSymbolTableAnalyser::stab_magic(const char* magic) {
	
}

void LocalSymbolTableAnalyser::source_file(const char* path, s32 text_address) {
	output.text_address = text_address;
}

void LocalSymbolTableAnalyser::data_type(const ParsedSymbol& symbol) {
	std::unique_ptr<ast::Node> node = ast::stabs_symbol_to_ast(symbol, stabs_to_ast_state);
	node->order = output.next_order++;
	node->stabs_type_number = symbol.name_colon_type.type->type_number;
	output.types.emplace_back(std::move(node));
}

void LocalSymbolTableAnalyser::global_variable(const char* name, s32 address, const StabsType& type, bool is_static, ast::GlobalVariableLocation location) {
	std::unique_ptr<ast::Variable> global = std::make_unique<ast::Variable>();
	global->name = name;
	if(is_static) {
		global->storage_class = ast::StorageClass::STATIC;
	}
	global->variable_class = ast::VariableClass::GLOBAL;
	global->storage.type = ast::VariableStorageType::GLOBAL;
	global->storage.global_location = location;
	global->storage.global_address = address;
	global->type = ast::stabs_type_to_ast_no_throw(type, stabs_to_ast_state, 0, 0, true);
	global->order = output.next_order++;
	output.globals.emplace_back(std::move(global));
}

void LocalSymbolTableAnalyser::sub_source_file(const char* path, s32 text_address) {
	
}

void LocalSymbolTableAnalyser::function(const char* name, s32 address, bool is_static) {
	next_function_is_static = is_static;
}

void LocalSymbolTableAnalyser::label(const char* label, s32 address, s32 line_number) {
	
}

void LocalSymbolTableAnalyser::text_end(const char* name, s32 function_size) {
	next_function_size = function_size;
}

void LocalSymbolTableAnalyser::return_type(const char* name, const StabsType& return_type, s32 function_address) {
	std::unique_ptr<ast::FunctionDefinition> function = std::make_unique<ast::FunctionDefinition>();
	current_function = function.get();
	function->name = name;
	
	function->address_range.low = function_address;
	assert(next_function_size >= 0);
	if(function->address_range.low >= 0) {
		function->address_range.high = function->address_range.low + (u32) next_function_size;
	}
	next_function_size = -1;
	
	if(next_function_is_static) {
		function->storage_class = ast::StorageClass::STATIC;
	}
	next_function_is_static = false;
	
	std::unique_ptr<ast::FunctionType> function_type = std::make_unique<ast::FunctionType>();
	current_function_type = function_type.get();
	function_type->return_type = ast::stabs_type_to_ast_no_throw(return_type, stabs_to_ast_state, 0, 0, true);
	function_type->parameters.emplace();
	function->type = std::move(function_type);
	
	auto body = std::make_unique<ast::CompoundStatement>();
	current_function_body = body.get();
	function->body = std::move(body);
	
	function->order = output.next_order++;
	output.functions.emplace_back(std::move(function));
	
	pending_variables_begin.clear();
	pending_variables_end.clear();
}

void LocalSymbolTableAnalyser::parameter(const char* name, const StabsType& type, bool is_stack_variable, s32 offset_or_register) {
	verify(current_function, "Parameter variable symbol before first function symbol.");
	std::unique_ptr<ast::Variable> parameter = std::make_unique<ast::Variable>();
	parameter->name = name;
	parameter->variable_class = ast::VariableClass::PARAMETER;
	if(is_stack_variable) {
		parameter->storage.type = ast::VariableStorageType::STACK;
		parameter->storage.stack_pointer_offset = offset_or_register;
	} else {
		parameter->storage.type = ast::VariableStorageType::REGISTER;
		parameter->storage.dbx_register_number = offset_or_register;
		std::tie(parameter->storage.register_class, parameter->storage.register_index_relative) =
			mips::map_dbx_register_index(parameter->storage.dbx_register_number);
	}
	parameter->type = ast::stabs_type_to_ast_no_throw(type, stabs_to_ast_state, 0, 0, true);
	current_function_type->parameters->emplace_back(std::move(parameter));
}

void LocalSymbolTableAnalyser::local_variable(const char* name, const StabsType& type, ast::VariableStorageType storage_type, s32 value, ast::GlobalVariableLocation location, bool is_static) {
	if(!current_function) {
		return;
	}
	std::unique_ptr<ast::Variable> local = std::make_unique<ast::Variable>();
	pending_variables_begin.emplace_back(local.get());
	local->name = name;
	if(is_static) {
		local->storage_class = ast::StorageClass::STATIC;
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
			std::tie(local->storage.register_class, local->storage.register_index_relative) =
				mips::map_dbx_register_index(local->storage.dbx_register_number);
			break;
		}
		case ast::VariableStorageType::STACK: {
			local->storage.stack_pointer_offset = value;
			break;
		}
	}
	local->type = ast::stabs_type_to_ast_no_throw(type, stabs_to_ast_state, 0, 0, true);
	current_function_body->children.emplace_back(std::move(local));
}

void LocalSymbolTableAnalyser::lbrac(s32 number, s32 begin_offset) {
	auto& pending_end = pending_variables_end[number];
	for(ast::Variable* variable : pending_variables_begin) {
		pending_end.emplace_back(variable);
		variable->block.low = output.text_address + begin_offset;
	}
	pending_variables_begin.clear();
}

void LocalSymbolTableAnalyser::rbrac(s32 number, s32 end_offset) {
	auto variables = pending_variables_end.find(number);
	verify(variables != pending_variables_end.end(), "N_RBRAC symbol without a matching N_LBRAC symbol.");
	for(ast::Variable* variable : variables->second) {
		variable->block.high = output.text_address + end_offset;
	}
}


static void filter_ast_by_flags(ast::Node& ast_node, u32 flags) {
	switch(ast_node.descriptor) {
		case ast::NodeDescriptor::ARRAY:
		case ast::NodeDescriptor::BITFIELD:
		case ast::NodeDescriptor::BUILTIN:
		case ast::NodeDescriptor::FUNCTION_DEFINITION:
		case ast::NodeDescriptor::FUNCTION_TYPE:
		case ast::NodeDescriptor::INLINE_ENUM: {
			break;
		}
		case ast::NodeDescriptor::INLINE_STRUCT_OR_UNION: {
			auto& struct_or_union = ast_node.as<ast::InlineStructOrUnion>();
			for(std::unique_ptr<ast::Node>& node : struct_or_union.fields) {
				// This allows us to deduplicate types with vtables.
				if(node->name.starts_with("$vf")) {
					node->name = "CCC_VTABLE";
				}
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
					ast_node.name.substr(0, ast_node.name.find("<"));
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
			break;
		}
		case ast::NodeDescriptor::POINTER: {
			filter_ast_by_flags(*ast_node.as<ast::Pointer>().value_type.get(), flags);
			break;
		}
		case ast::NodeDescriptor::REFERENCE: {
			filter_ast_by_flags(*ast_node.as<ast::Reference>().value_type.get(), flags);
			break;
		}
		case ast::NodeDescriptor::SOURCE_FILE: {
			ast::SourceFile& source_file = ast_node.as<ast::SourceFile>();
			for(std::unique_ptr<ast::Node>& child : source_file.types) {
				filter_ast_by_flags(*child.get(), flags);
			}
		}
		case ast::NodeDescriptor::COMPOUND_STATEMENT:
		case ast::NodeDescriptor::TYPE_NAME:
		case ast::NodeDescriptor::VARIABLE: {
			break;
		}
	}
}

}
