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
	
	// Either analyse a specific file descriptor, or all of them.
	if(file_descriptor_index > -1) {
		assert(file_descriptor_index < symbol_table.files.size());
		analyse_file(results, symbol_table, symbol_table.files[file_descriptor_index], flags);
	} else {
		for(const mdebug::SymFileDescriptor& fd : symbol_table.files) {
			analyse_file(results, symbol_table, fd, flags);
		}
	}
	
	for(TranslationUnit& translation_unit : results.translation_units) {
		// Some enums have two separate stabs generated for them, one with a
		// name of " ", where one stab references the other. Remove these
		// duplicate AST nodes.
		ast::remove_duplicate_enums(translation_unit.types);
		for(std::unique_ptr<ast::Node>& ast_node : translation_unit.types) {
			// Filter the AST depending on the flags parsed, removing things the
			// calling code didn't ask for.
			filter_ast_by_flags(*ast_node.get(), flags);
		}
	}
	
	// Deduplicate types from different translation units, preserving multiple
	// copies of types that actually differ.
	if(flags & DEDUPLICATE_TYPES) {
		std::vector<std::pair<std::string, std::vector<std::unique_ptr<ast::Node>>>> per_file_types;
		for(TranslationUnit& translation_unit : results.translation_units) {
			per_file_types.emplace_back(translation_unit.full_path, std::move(translation_unit.types));
		}
		results.deduplicated_types = ast::deduplicate_ast(per_file_types);
	}
	
	return results;
}

void analyse_file(AnalysisResults& results, const mdebug::SymbolTable& symbol_table, const mdebug::SymFileDescriptor& fd, u32 flags) {
	TranslationUnit& translation_unit = results.translation_units.emplace_back();
	translation_unit.full_path = fd.full_path;
	// Parse the stab strings into a data structure that's vaguely
	// one-to-one with the text-based representation.
	translation_unit.symbols = parse_symbols(fd.symbols, fd.detected_language);
	// In stabs, types can be referenced by their number from other stabs,
	// so here we build a map of type numbers to the parsed types.
	std::map<s32, const StabsType*> stabs_types;
	for(const ParsedSymbol& symbol : translation_unit.symbols) {
		if(symbol.type == ParsedSymbolType::NAME_COLON_TYPE) {
			symbol.name_colon_type.type->enumerate_numbered_types(stabs_types);
		}
	}
	
	// Convert the parsed stabs symbols to a more standard C AST.
	ast::FunctionDefinition* current_function = nullptr;
	ast::FunctionType* current_function_type = nullptr;
	ast::Scope* current_function_body = nullptr;
	std::vector<ast::Variable*> pending_variables_begin;
	std::map<s32, std::vector<ast::Variable*>> pending_variables_end;
	for(const ParsedSymbol& symbol : translation_unit.symbols) {
		switch(symbol.type) {
			case ParsedSymbolType::NAME_COLON_TYPE: {
				switch(symbol.name_colon_type.descriptor) {
					case StabsSymbolDescriptor::LOCAL_FUNCTION:
					case StabsSymbolDescriptor::GLOBAL_FUNCTION: {
						std::unique_ptr<ast::FunctionDefinition> function = std::make_unique<ast::FunctionDefinition>();
						current_function = function.get();
						function->name = symbol.name_colon_type.name;
						
						std::unique_ptr<ast::FunctionType> function_type = std::make_unique<ast::FunctionType>();
						current_function_type = function_type.get();
						function_type->return_type = ast::stabs_type_to_ast_no_throw(*symbol.name_colon_type.type.get(), stabs_types, 0, 0, true);
						function_type->parameters.emplace();
						function->type = std::move(function_type);
						
						auto body = std::make_unique<ast::Scope>();
						current_function_body = body.get();
						function->body = std::move(body);
						
						translation_unit.functions_and_globals.emplace_back(std::move(function));
						
						pending_variables_begin.clear();
						pending_variables_end.clear();
						
						break;
					}
					case StabsSymbolDescriptor::REFERENCE_PARAMETER:
					case StabsSymbolDescriptor::REGISTER_PARAMETER:
					case StabsSymbolDescriptor::VALUE_PARAMETER: {
						verify(current_function, "Parameter variable symbol before first function symbol.");
						std::unique_ptr<ast::Variable> parameter = std::make_unique<ast::Variable>();
						parameter->name = symbol.name_colon_type.name;
						parameter->variable_class = ast::VariableClass::PARAMETER;
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::VALUE_PARAMETER) {
							parameter->storage.location = ast::VariableStorageLocation::STACK;
							parameter->storage.stack_pointer_offset = symbol.raw->value;
						} else {
							parameter->storage.location = ast::VariableStorageLocation::REGISTER;
							parameter->storage.dbx_register_number = symbol.raw->value;
							std::tie(parameter->storage.register_class, parameter->storage.register_index_relative) =
								mips::map_dbx_register_index(parameter->storage.dbx_register_number);
						}
						parameter->type = ast::stabs_type_to_ast_no_throw(*symbol.name_colon_type.type.get(), stabs_types, 0, 0, true);
						current_function_type->parameters->emplace_back(std::move(parameter));
						break;
					}
					case StabsSymbolDescriptor::REGISTER_VARIABLE:
					case StabsSymbolDescriptor::LOCAL_VARIABLE:
					case StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE: {
						if(!current_function) {
							continue;
						}
						std::unique_ptr<ast::Variable> local = std::make_unique<ast::Variable>();
						pending_variables_begin.emplace_back(local.get());
						local->name = symbol.name_colon_type.name;
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE) {
							local->storage_class = ast::StorageClass::STATIC;
						}
						local->variable_class = ast::VariableClass::LOCAL;
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::REGISTER_VARIABLE) {
							local->storage.location = ast::VariableStorageLocation::REGISTER;
							local->storage.dbx_register_number = symbol.raw->value;
							std::tie(local->storage.register_class, local->storage.register_index_relative) =
								mips::map_dbx_register_index(local->storage.dbx_register_number);
						} else {
							local->storage.location = ast::VariableStorageLocation::STACK;
							local->storage.stack_pointer_offset = symbol.raw->value;
						}
						local->type = ast::stabs_type_to_ast_no_throw(*symbol.name_colon_type.type.get(), stabs_types, 0, 0, true);
						current_function_body->children.emplace_back(std::move(local));
						break;
					}
					case StabsSymbolDescriptor::GLOBAL_VARIABLE:
					case StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE: {
						std::unique_ptr<ast::Variable> global = std::make_unique<ast::Variable>();
						global->name = symbol.name_colon_type.name;
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE) {
							global->storage_class = ast::StorageClass::STATIC;
						}
						global->variable_class = ast::VariableClass::GLOBAL;
						if(symbol.raw->code == mdebug::N_LCSYM) {
							global->storage.location = ast::VariableStorageLocation::BSS;
						} else {
							global->storage.location = ast::VariableStorageLocation::DATA;
						}
						global->type = ast::stabs_type_to_ast_no_throw(*symbol.name_colon_type.type.get(), stabs_types, 0, 0, true);
						translation_unit.functions_and_globals.emplace_back(std::move(global));
						break;
					}
					case StabsSymbolDescriptor::TYPE_NAME:
					case StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG: {
						std::unique_ptr<ast::Node> node = ast::stabs_symbol_to_ast(symbol, stabs_types);
						if(node != nullptr) {
							translation_unit.types.emplace_back(std::move(node));
						}
						break;
					}
				}
				break;
			}
			case ParsedSymbolType::SOURCE_FILE: {
				translation_unit.text_address = symbol.so.translation_unit_text_address;
				break;
			}
			case ParsedSymbolType::SUB_SOURCE_FILE: {
				break;
			}
			case ParsedSymbolType::SCOPE_BEGIN: {
				auto& pending_end = pending_variables_end[symbol.scope.number];
				for(ast::Variable* variable : pending_variables_begin) {
					pending_end.emplace_back(variable);
					variable->block.low = translation_unit.text_address + symbol.raw->value;
				}
				pending_variables_begin.clear();
				break;
			}
			case ParsedSymbolType::SCOPE_END: {
				auto variables = pending_variables_end.find(symbol.scope.number);
				verify(variables != pending_variables_end.end(), "N_RBRAC symbol without a matching N_LBRAC symbol.");
				for(ast::Variable* variable : variables->second) {
					variable->block.high = translation_unit.text_address + symbol.raw->value;
				}
				break;
			}
			case ParsedSymbolType::NON_STABS: {
				break;
			}
		}
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
		case ast::NodeDescriptor::SCOPE:
		case ast::NodeDescriptor::TYPE_NAME:
		case ast::NodeDescriptor::VARIABLE: {
			break;
		}
	}
}

}
