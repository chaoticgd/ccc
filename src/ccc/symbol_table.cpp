// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "symbol_table.h"

#include "mdebug.h"
#include "mdebug_analysis.h"

namespace ccc {
	
static void filter_ast_by_flags(ast::Node& ast_node, u32 parser_flags);
static void compute_size_bytes_recursive(ast::Node& node, SymbolDatabase& database);

u32 identify_elf_symbol_tables(const ElfFile& elf) {
	u32 result = 0;
	for(const ElfSection& section : elf.sections) {
		if(section.name == ".symtab" && section.size > 0) {
			result |= SYMTAB;
		}
		if(section.name == ".mdebug" && section.size > 0) {
			result |= MDEBUG;
		}
		if(section.name == ".stab" && section.size > 0) {
			result |= STAB;
		}
		if(section.name == ".debug" && section.size > 0) {
			result |= DWARF;
		}
		if(section.name == ".sndata" && section.size > 0) {
			result |= SNDATA;
		}
	}
	return result;
}

std::string symbol_table_formats_to_string(u32 formats) {
	std::string output;
	bool printed = false;
	for(u32 bit = 1; bit < MAX_SYMBOL_TABLE; bit <<= 1) {
		u32 format = formats & bit;
		if(format != 0) {
			if(printed) {
				output += " ";
			}
			output += symbol_table_format_to_string((SymbolTableFormat) format);
			printed = true;
		}
	}
	if(!printed) {
		output += "none";
	}
	return output;
}

const char* symbol_table_format_to_string(SymbolTableFormat format) {
	switch(format) {
		case SYMTAB: return "symtab";
		case MAP: return "map";
		case MDEBUG: return "mdebug";
		case STAB: return "stab";
		case DWARF: return "dwarf";
		case SNDATA: return "sndata";
		case SNDLL: return "sndll";
	}
	return "";
}

Result<SymbolSourceHandle> parse_symbol_table(SymbolDatabase& database, const ElfFile& elf, u32 parser_flags, DemanglerFunc* demangle) {
	const ElfSection* mdebug_section = elf.lookup_section(".mdebug");
	CCC_CHECK(mdebug_section != nullptr, "No .mdebug section.");
	
	mdebug::SymbolTableReader reader;
	Result<void> reader_result = reader.init(elf.image, mdebug_section->offset);
	CCC_RETURN_IF_ERROR(reader_result);
	
	Result<SymbolSourceHandle> symbol_source = analyse(database, reader, parser_flags, demangle);
	CCC_RETURN_IF_ERROR(symbol_source);
	
	// Filter the AST and compute size information for all nodes.
#define CCC_X(SymbolType, symbol_list) \
	for(SymbolType& symbol : database.symbol_list) { \
		if(symbol.type()) { \
			filter_ast_by_flags(*symbol.type(), parser_flags); \
			compute_size_bytes_recursive(*symbol.type(), database); \
		} \
	}
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X
	
	return symbol_source;
}

static void filter_ast_by_flags(ast::Node& ast_node, u32 parser_flags) {
	for_each_node(ast_node, ast::PREORDER_TRAVERSAL, [&](ast::Node& node) {
		if(parser_flags & STRIP_ACCESS_SPECIFIERS) {
			node.access_specifier = ast::AS_PUBLIC;
		}
		if(node.descriptor == ast::STRUCT_OR_UNION) {
			auto& struct_or_union = node.as<ast::StructOrUnion>();
			if(parser_flags & STRIP_MEMBER_FUNCTIONS) {
				struct_or_union.member_functions.clear();
			} else if(parser_flags & STRIP_GENERATED_FUNCTIONS) {
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

static void compute_size_bytes_recursive(ast::Node& node, SymbolDatabase& database) {
	for_each_node(node, ast::POSTORDER_TRAVERSAL, [&](ast::Node& node) {
		// Skip nodes that have already been processed.
		if(node.computed_size_bytes > -1 || node.cannot_compute_size) {
			return ast::EXPLORE_CHILDREN;
		}
		
		// Can't compute size recursively.
		node.cannot_compute_size = true;
		
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
			case ast::FUNCTION_TYPE: {
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
			case ast::TYPE_NAME: {
				ast::TypeName& type_name = node.as<ast::TypeName>();
				DataTypeHandle resolved_type_handle = database.lookup_type(type_name, false);
				DataType* resolved_type = database.data_types.symbol_from_handle(resolved_type_handle);
				if(resolved_type) {
					ast::Node* resolved_node = resolved_type->type();
					CCC_ASSERT(resolved_node);
					if(resolved_node->computed_size_bytes < 0 && !resolved_node->cannot_compute_size) {
						compute_size_bytes_recursive(*resolved_node, database);
					}
					type_name.computed_size_bytes = resolved_node->computed_size_bytes;
				}
				break;
			}
		}
		
		if(node.computed_size_bytes > -1) {
			node.cannot_compute_size = false;
		}
		
		return ast::EXPLORE_CHILDREN;
	});
}

}
