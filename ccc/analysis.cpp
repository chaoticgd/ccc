#include "analysis.h"

namespace ccc {

// read_symbol_table
// analyse_program
// analyse_file
static void filter_ast_by_flags(ast::Node& ast_node, u32 flags);
// scan_for_functions

SymbolTable read_symbol_table(const std::vector<Module*>& modules) {
	std::optional<SymbolTable> symbol_table;
	for(Module* mod : modules) {
		ModuleSection* mdebug_section = mod->lookup_section(".mdebug");
		if(mdebug_section) {
			symbol_table = parse_mdebug_section(*mod, *mdebug_section);
			break;
		}
	}
	verify(symbol_table.has_value(), "No .mdebug section!");
	return *symbol_table;
}

AnalysisResults analyse(const SymbolTable& symbol_table, u32 flags, s32 file_descriptor_index) {
	AnalysisResults results;
	
	// Either analyse a specific file descriptor, or all of them.
	if(file_descriptor_index > -1) {
		assert(file_descriptor_index < symbol_table.files.size());
		analyse_file(results, symbol_table, symbol_table.files[file_descriptor_index], flags);
	} else {
		for(const SymFileDescriptor& fd : symbol_table.files) {
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

void analyse_file(AnalysisResults& results, const SymbolTable& symbol_table, const SymFileDescriptor& fd, u32 flags) {
	TranslationUnit& translation_unit = results.translation_units.emplace_back();
	translation_unit.full_path = fd.full_path;
	// Parse the stab strings into a data structure that's vaguely
	// one-to-one with the text-based representation.
	translation_unit.symbols = parse_symbols(fd.symbols, fd.detected_language);
	// In stabs, types can be referenced by their number from other stabs,
	// so here we build a map of type numbers to the parsed types.
	std::map<s32, const StabsType*> stabs_types;
	for(const ParsedSymbol& symbol : translation_unit.symbols) {
		if(symbol.is_stabs) {
			symbol.type->enumerate_numbered_types(stabs_types);
		}
	}
	
	// Convert the parsed stabs symbols to a more standard C AST.
	for(const ParsedSymbol& symbol : translation_unit.symbols) {
		if(symbol.is_stabs) {
			switch(symbol.descriptor) {
				case StabsSymbolDescriptor::LOCAL_FUNCTION:
				case StabsSymbolDescriptor::GLOBAL_FUNCTION: {
					Function& function = translation_unit.functions.emplace_back();
					function.name = symbol.name;
					function.return_type = ast::stabs_type_to_ast_no_throw(*symbol.type.get(), stabs_types, 0, 0, true);
					break;
				}
				case StabsSymbolDescriptor::REFERENCE_PARAMETER:
				case StabsSymbolDescriptor::REGISTER_PARAMETER:
				case StabsSymbolDescriptor::VALUE_PARAMETER: {
					if(translation_unit.functions.empty()) {
						break;
					}
					Function& function = translation_unit.functions.back();
					Parameter& parameter = function.parameters.emplace_back();
					parameter.name = symbol.name;
					parameter.type = ast::stabs_type_to_ast_no_throw(*symbol.type.get(), stabs_types, 0, 0, true);
					if(symbol.descriptor == StabsSymbolDescriptor::VALUE_PARAMETER) {
						parameter.storage.location = VariableStorageLocation::STACK;
						parameter.storage.stack_pointer_offset = symbol.raw->value;
					} else {
						parameter.storage.location = VariableStorageLocation::REGISTER;
						parameter.storage.dbx_register_number = symbol.raw->value;
						std::tie(parameter.storage.register_class, parameter.storage.register_index_relative) =
							mips::map_gcc_register_index(parameter.storage.dbx_register_number);
					}
					break;
				}
				case StabsSymbolDescriptor::REGISTER_VARIABLE:
				case StabsSymbolDescriptor::LOCAL_VARIABLE:
				case StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE: {
					if(translation_unit.functions.empty()) {
						break;
					}
					Function& function = translation_unit.functions.back();
					LocalVariable& local = function.locals.emplace_back();
					local.name = symbol.name;
					local.type = ast::stabs_type_to_ast_no_throw(*symbol.type.get(), stabs_types, 0, 0, true);
					if(symbol.descriptor == StabsSymbolDescriptor::REGISTER_VARIABLE) {
						local.storage.location = VariableStorageLocation::REGISTER;
						local.storage.dbx_register_number = symbol.raw->value;
						std::tie(local.storage.register_class, local.storage.register_index_relative) =
							mips::map_gcc_register_index(local.storage.dbx_register_number);
					} else {
						local.storage.location = VariableStorageLocation::STACK;
						local.storage.stack_pointer_offset = symbol.raw->value;
					}
					break;
				}
				case StabsSymbolDescriptor::GLOBAL_VARIABLE:
				case StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE: {
					GlobalVariable& global = translation_unit.globals.emplace_back();
					global.name = symbol.name;
					global.type = ast::stabs_type_to_ast_no_throw(*symbol.type.get(), stabs_types, 0, 0, true);
					if(symbol.descriptor == StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE) {
						global.type->storage_class = ast::StorageClass::STATIC;
					}
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
		}
	}
}

static void filter_ast_by_flags(ast::Node& ast_node, u32 flags) {
	switch(ast_node.descriptor) {
		case ast::NodeDescriptor::ARRAY:
		case ast::NodeDescriptor::BITFIELD:
		case ast::NodeDescriptor::BUILTIN:
		case ast::NodeDescriptor::FUNCTION:
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
				auto is_special = [](const ast::Function& function, const std::string& name_no_template_args) {
					return function.name == "operator="
						|| function.name.starts_with("$")
						|| (function.name == name_no_template_args
							&& function.parameters->size() == 0);
				};
				
				std::string name_no_template_args =
					ast_node.name.substr(0, ast_node.name.find("<"));
				bool only_special_functions = true;
				for(size_t i = 0; i < struct_or_union.member_functions.size(); i++) {
					if(struct_or_union.member_functions[i]->descriptor == ast::NodeDescriptor::FUNCTION) {
						ast::Function& function = struct_or_union.member_functions[i]->as<ast::Function>();
						if(!is_special(function, name_no_template_args)) {
							only_special_functions = false;
						}
					}
				}
				if(only_special_functions) {
					for(size_t i = 0; i < struct_or_union.member_functions.size(); i++) {
						if(struct_or_union.member_functions[i]->descriptor == ast::NodeDescriptor::FUNCTION) {
							ast::Function& function = struct_or_union.member_functions[i]->as<ast::Function>();
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
		case ast::NodeDescriptor::TYPE_NAME: {
			break;
		}
	}
}

std::map<u32, Function> scan_for_functions(u32 address, std::span<mips::Insn> insns) {
	std::map<u32, Function> functions;
	for(mips::Insn& insn : insns) {
		if(insn.opcode() == OPCODE_JAL) {
			u32 address = insn.target_bytes();
			Function& func = functions[address];
			func.name = "func_" + stringf("%08x", address);
			func.address = address;
		}
	}
	return functions;
}

}
