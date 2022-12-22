#include "analysis.h"

#include "ast.h"
#include "stabs.h"
#include "mdebug.h"

namespace ccc {

// read_symbol_table
// analyse_program
// analyse_file_descriptor
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

std::optional<AnalysisResults> analyse(const SymbolTable& symbol_table, u32 flags, s32 file_descriptor_index) {
	AnalysisResults results;
	
	// Either analyse a specific file descriptor, or all of them.
	if(file_descriptor_index > -1) {
		assert(file_descriptor_index < symbol_table.files.size());
		analyse_file_descriptor(results, symbol_table, symbol_table.files[file_descriptor_index], flags);
	} else {
		for(const SymFileDescriptor& fd : symbol_table.files) {
			analyse_file_descriptor(results, symbol_table, fd, flags);
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

void analyse_file_descriptor(AnalysisResults& results, const SymbolTable& symbol_table, const SymFileDescriptor& fd, u32 flags) {
	TranslationUnit& translation_unit = results.translation_units.emplace_back();
	translation_unit.full_path = fd.name;
	// Parse the stab strings into a data structure that's vaguely
	// one-to-one with the text-based representation.
	std::vector<StabsSymbol>& symbols = translation_unit.symbols.emplace_back(parse_stabs_symbols(fd.symbols, fd.detected_language));
	// In stabs, types can be referenced by their number from other stabs,
	// so here we build a map of type numbers to the parsed types.
	const std::map<s32, const StabsType*> types = enumerate_numbered_types(symbols);
	// Convert the stabs data structure to a more standard C AST.
	translation_unit.types = ast::symbols_to_ast(symbols, types);
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
