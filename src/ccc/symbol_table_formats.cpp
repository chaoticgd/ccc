// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "mdebug.h"
#include "mdebug_analysis.h"
#include "symbol_table_formats.h"

namespace ccc {
	
static void filter_ast_by_flags(ast::Node& ast_node, u32 parser_flags);
static void compute_size_bytes_recursive(ast::Node& node, SymbolTable& symbol_table);

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

Result<SymbolTable> parse_symbol_table(std::vector<u8> image, u32 parser_flags) {
	Result<ElfFile> elf = parse_elf_file(std::move(image));
	CCC_RETURN_IF_ERROR(elf);
	
	ElfSection* mdebug_section = elf->lookup_section(".mdebug");
	CCC_CHECK(mdebug_section != nullptr, "No .mdebug section.");
	
	mdebug::SymbolTableReader reader;
	Result<void> reader_result = reader.init(elf->image, mdebug_section->file_offset);
	CCC_EXIT_IF_ERROR(reader_result);
	
	Result<SymbolTable> symbol_table = analyse(reader, parser_flags);
	CCC_EXIT_IF_ERROR(symbol_table);
	
	// Filter the AST and compute size information for all nodes.
#define CCC_X(SymbolType, symbol_list) \
	for(SymbolType& symbol : symbol_table->symbol_list) { \
		if(symbol.type_ptr()) { \
			filter_ast_by_flags(*symbol.type_ptr(), parser_flags); \
			compute_size_bytes_recursive(*symbol.type_ptr(), *symbol_table); \
		} \
	}
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
#undef CCC_X
	
	return symbol_table;
}

static void filter_ast_by_flags(ast::Node& ast_node, u32 parser_flags) {
	for_each_node(ast_node, ast::PREORDER_TRAVERSAL, [&](ast::Node& node) {
		if(parser_flags & STRIP_ACCESS_SPECIFIERS) {
			node.access_specifier = ast::AS_PUBLIC;
		}
		if(node.descriptor == ast::STRUCT_OR_UNION) {
			auto& struct_or_union = node.as<ast::StructOrUnion>();
			for(std::unique_ptr<ast::Node>& node : struct_or_union.fields) {
				// This allows us to deduplicate types with vtables.
				if(node->name.starts_with("$vf")) {
					node->name = "__vtable";
				}
				filter_ast_by_flags(*node.get(), parser_flags);
			}
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

static void compute_size_bytes_recursive(ast::Node& node, SymbolTable& symbol_table) {
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
			case ast::DATA: {
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
			case ast::TYPE_NAME: {
				ast::TypeName& type_name = node.as<ast::TypeName>();
				DataTypeHandle resolved_type_handle = symbol_table.lookup_type(type_name, false);
				DataType* resolved_type = symbol_table.data_types[resolved_type_handle];
				if(resolved_type) {
					ast::Node& resolved_node = resolved_type->type();
					if(resolved_node.computed_size_bytes < 0 && !resolved_node.cannot_compute_size) {
						compute_size_bytes_recursive(resolved_node, symbol_table);
					}
					type_name.computed_size_bytes = resolved_node.computed_size_bytes;
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
