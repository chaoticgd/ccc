#include "analysis.h"

#include "elf.h"

namespace ccc {

static void create_function(LocalSymbolTableAnalyser& analyser, const char* name);
static void filter_ast_by_flags(ast::Node& ast_node, u32 flags);

mdebug::SymbolTable read_symbol_table(Module& mod, const fs::path& input_file) {
	mod = loaders::read_elf_file(input_file);
	ModuleSection* mdebug_section = mod.lookup_section(".mdebug");
	verify(mdebug_section, "No .mdebug section.");
	return mdebug::parse_symbol_table(mod, *mdebug_section);
}

HighSymbolTable analyse(const mdebug::SymbolTable& symbol_table, u32 flags, s32 file_descriptor_index) {
	HighSymbolTable high;
	
	// The addresses of the global variables aren't present in the local symbol
	// table, so here we extract them from the external table.
	std::map<std::string, const mdebug::Symbol*> globals;
	for(const mdebug::Symbol& external : symbol_table.externals) {
		if(external.storage_type == mdebug::SymbolType::GLOBAL
			&& (external.storage_class != mdebug::SymbolClass::UNDEFINED)) {
			globals[external.string] = &external;
		}
	}
	
	ast::TypeDeduplicatorOMatic deduplicator;
	
	// Either analyse a specific file descriptor, or all of them.
	if(file_descriptor_index > -1) {
		assert(file_descriptor_index < symbol_table.files.size());
		analyse_file(high, deduplicator, symbol_table, symbol_table.files[file_descriptor_index], globals, file_descriptor_index, flags);
	} else {
		for(s32 i = 0; i < (s32) symbol_table.files.size(); i++) {
			const mdebug::SymFileDescriptor& fd = symbol_table.files[i];
			analyse_file(high, deduplicator, symbol_table, fd, globals, i, flags);
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

void analyse_file(HighSymbolTable& high, ast::TypeDeduplicatorOMatic& deduplicator, const mdebug::SymbolTable& symbol_table, const mdebug::SymFileDescriptor& fd, const std::map<std::string, const mdebug::Symbol*>& globals, s32 file_index, u32 flags) {
	auto file = std::make_unique<ast::SourceFile>();
	file->full_path = fd.full_path;
	file->is_windows_path = fd.is_windows_path;
	
	// Parse the stab strings into a data structure that's vaguely
	// one-to-one with the text-based representation.
	file->symbols = parse_symbols(fd.symbols, fd.detected_language);
	
	// In stabs, types can be referenced by their number from other stabs,
	// so here we build a map of type numbers to the parsed types.
	std::map<s64, const StabsType*> stabs_types;
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
						analyser.function(name, type, symbol.raw->value);
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
						analyser.parameter(name, type, is_stack_variable, symbol.raw->value, is_by_reference);
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
							auto global_symbol = globals.find(symbol.name_colon_type.name);
							if(global_symbol != globals.end()) {
								address = global_symbol->second->value;
								location = symbol_class_to_global_variable_location(global_symbol->second->storage_class);
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
			case ParsedSymbolType::LBRAC: {
				analyser.lbrac(symbol.lrbrac.number, symbol.raw->value);
				break;
			}
			case ParsedSymbolType::RBRAC: {
				analyser.rbrac(symbol.lrbrac.number, symbol.raw->value);
				break;
			}
			case ParsedSymbolType::FUNCTION_END: {
				analyser.function_end();
			}
			case ParsedSymbolType::NON_STABS: {
				if(symbol.raw->storage_class == mdebug::SymbolClass::TEXT) {
					if(symbol.raw->storage_type == mdebug::SymbolType::PROC) {
						analyser.procedure(symbol.raw->string, symbol.raw->value, false);
					} else if(symbol.raw->storage_type == mdebug::SymbolType::STATICPROC) {
						analyser.procedure(symbol.raw->string, symbol.raw->value, true);
					} else if(symbol.raw->storage_type == mdebug::SymbolType::LABEL) {
						analyser.label(symbol.raw->string, symbol.raw->value, symbol.raw->index);
					} else if(symbol.raw->storage_type == mdebug::SymbolType::END) {
						analyser.text_end(symbol.raw->string, symbol.raw->value);
					}
				}
				break;
			}
		}
	}
	
	analyser.finish();
	
	// The STABS types are no longer needed, so delete them now.
	for(ParsedSymbol& symbol : file->symbols) {
		symbol.name_colon_type.type = nullptr;
	}
	
	// Some enums have two separate stabs generated for them, one with a
	// name of " ", where one stab references the other. Remove these
	// duplicate AST nodes.
	ast::remove_duplicate_enums(file->data_types);
	
	// For some reason typedefs referencing themselves are generated along
	// with a proper struct of the same name.
	ast::remove_duplicate_self_typedefs(file->data_types);
	
	// Filter the AST depending on the flags parsed, removing things the
	// calling code didn't ask for.
	filter_ast_by_flags(*file, flags);
	
	high.source_files.emplace_back(std::move(file));
	
	// Deduplicate types.
	if(flags & DEDUPLICATE_TYPES) {
		deduplicator.process_file(*high.source_files.back().get(), file_index, high.source_files);
	}
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
		case mdebug::SymbolClass::COMMON: return ast::GlobalVariableLocation::COMMON;
		case mdebug::SymbolClass::SCOMMON: return ast::GlobalVariableLocation::SCOMMON;
		default: {}
	}
	verify_not_reached("Bad variable storage location '%s'.", mdebug::symbol_class(symbol_class));
}

void LocalSymbolTableAnalyser::stab_magic(const char* magic) {
	
}

void LocalSymbolTableAnalyser::source_file(const char* path, s32 text_address) {
	output.relative_path = path;
	output.text_address = text_address;
	if(next_relative_path.empty()) {
		next_relative_path = output.relative_path;
	}
}

void LocalSymbolTableAnalyser::data_type(const ParsedSymbol& symbol) {
	std::unique_ptr<ast::Node> node = ast::stabs_symbol_to_ast(symbol, stabs_to_ast_state);
	node->stabs_type_number = symbol.name_colon_type.type->type_number;
	output.data_types.emplace_back(std::move(node));
}

void LocalSymbolTableAnalyser::global_variable(const char* name, s32 address, const StabsType& type, bool is_static, ast::GlobalVariableLocation location) {
	std::unique_ptr<ast::Variable> global = std::make_unique<ast::Variable>();
	global->name = name;
	if(is_static) {
		global->storage_class = ast::SC_STATIC;
	}
	global->variable_class = ast::VariableClass::GLOBAL;
	global->storage.type = ast::VariableStorageType::GLOBAL;
	global->storage.global_location = location;
	global->storage.global_address = address;
	global->type = ast::stabs_type_to_ast_no_throw(type, stabs_to_ast_state, 0, 0, true, false);
	output.globals.emplace_back(std::move(global));
}

void LocalSymbolTableAnalyser::sub_source_file(const char* path, s32 text_address) {
	if(current_function && state == IN_FUNCTION_BEGINNING) {
		ast::SubSourceFile& sub = current_function->sub_source_files.emplace_back();
		sub.address = text_address;
		sub.relative_path = path;
	} else {
		next_relative_path = path;
	}
}

void LocalSymbolTableAnalyser::procedure(const char* name, s32 address, bool is_static) {
	if(!current_function || strcmp(name, current_function->name.c_str())) {
		create_function(*this, name);
	}
	
	current_function->address_range.low = address;
	if(is_static) {
		current_function->storage_class = ast::SC_STATIC;
	}
	
	pending_variables_begin.clear();
	pending_variables_end.clear();
}

void LocalSymbolTableAnalyser::label(const char* label, s32 address, s32 line_number) {
	if(address > -1 && current_function && label[0] == '$') {
		assert(address < 256 * 1024 * 1024);
		ast::LineNumberPair& pair = current_function->line_numbers.emplace_back();
		pair.address = address;
		pair.line_number = line_number;
	}
}

void LocalSymbolTableAnalyser::text_end(const char* name, s32 function_size) {
	if(state == IN_FUNCTION_BEGINNING) {
		if(current_function->address_range.low >= 0) {
			assert(current_function);
			current_function->address_range.high = current_function->address_range.low + function_size;
		}
		state = IN_FUNCTION_END;
	}
}

void LocalSymbolTableAnalyser::function(const char* name, const StabsType& return_type, s32 function_address) {
	if(!current_function || strcmp(name, current_function->name.c_str())) {
		create_function(*this, name);
	}
	
	current_function_type->return_type = ast::stabs_type_to_ast_no_throw(return_type, stabs_to_ast_state, 0, 0, true, true);
}

void LocalSymbolTableAnalyser::function_end() {
	current_function = nullptr;
	current_function_type = nullptr;
}

void LocalSymbolTableAnalyser::parameter(const char* name, const StabsType& type, bool is_stack_variable, s32 offset_or_register, bool is_by_reference) {
	assert(current_function_type);
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
		parameter->storage.is_by_reference = is_by_reference;
	}
	parameter->type = ast::stabs_type_to_ast_no_throw(type, stabs_to_ast_state, 0, 0, true, true);
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
			std::tie(local->storage.register_class, local->storage.register_index_relative) =
				mips::map_dbx_register_index(local->storage.dbx_register_number);
			break;
		}
		case ast::VariableStorageType::STACK: {
			local->storage.stack_pointer_offset = value;
			break;
		}
	}
	local->type = ast::stabs_type_to_ast_no_throw(type, stabs_to_ast_state, 0, 0, true, false);
	current_function->locals.emplace_back(std::move(local));
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

void LocalSymbolTableAnalyser::finish() {
	verify(state != IN_FUNCTION_BEGINNING,
		"Unexpected end of symbol table for '%s'.", output.full_path.c_str());
}

static void create_function(LocalSymbolTableAnalyser& analyser, const char* name) {
	std::unique_ptr<ast::FunctionDefinition> ptr = std::make_unique<ast::FunctionDefinition>();
	analyser.current_function = ptr.get();
	analyser.output.functions.emplace_back(std::move(ptr));
	analyser.current_function->name = name;
	analyser.state = LocalSymbolTableAnalyser::IN_FUNCTION_BEGINNING;
	
	if(!analyser.next_relative_path.empty() && analyser.current_function->relative_path != analyser.output.relative_path) {
		analyser.current_function->relative_path = analyser.next_relative_path;
	}
	
	std::unique_ptr<ast::FunctionType> function_type = std::make_unique<ast::FunctionType>();
	analyser.current_function_type = function_type.get();
	analyser.current_function_type->parameters.emplace();
	analyser.current_function->type = std::move(function_type);
}

static void filter_ast_by_flags(ast::Node& ast_node, u32 flags) {
	if(flags & STRIP_ACCESS_SPECIFIERS) {
		ast_node.access_specifier = ast::AS_PUBLIC;
	}
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
		case ast::NodeDescriptor::POINTER_TO_DATA_MEMBER: {
			break;
		}
		case ast::NodeDescriptor::REFERENCE: {
			filter_ast_by_flags(*ast_node.as<ast::Reference>().value_type.get(), flags);
			break;
		}
		case ast::NodeDescriptor::SOURCE_FILE: {
			ast::SourceFile& source_file = ast_node.as<ast::SourceFile>();
			for(std::unique_ptr<ast::Node>& child : source_file.data_types) {
				filter_ast_by_flags(*child.get(), flags);
			}
		}
		case ast::NodeDescriptor::TYPE_NAME:
		case ast::NodeDescriptor::VARIABLE: {
			break;
		}
	}
}

void compute_size_bytes_recursive(ast::Node& node, const HighSymbolTable& high) {
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
			case ast::FUNCTION_DEFINITION: {
				break;
			}
			case ast::FUNCTION_TYPE: {
				break;
			}
			case ast::INITIALIZER_LIST: {
				break;
			}
			case ast::INLINE_ENUM: {
				node.computed_size_bytes = 4;
				break;
			}
			case ast::INLINE_STRUCT_OR_UNION: {
				node.computed_size_bytes = node.size_bits / 8;
				break;
			}
			case ast::LITERAL: {
				break;
			}
			case ast::POINTER: {
				node.computed_size_bytes = 4;
				break;
			}
			case ast::POINTER_TO_DATA_MEMBER: {
				break;
			}
			case ast::REFERENCE: {
				node.computed_size_bytes = 4;
				break;
			}
			case ast::SOURCE_FILE: {
				break;
			}
			case ast::TYPE_NAME: {
				ast::TypeName& type_name = node.as<ast::TypeName>();
				if(type_name.referenced_file_index > -1 && type_name.referenced_stabs_type_number > -1) {
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

}
