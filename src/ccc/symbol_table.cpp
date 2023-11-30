// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "symbol_table.h"

#include "elf_symtab.h"
#include "mdebug.h"
#include "mdebug_analysis.h"

namespace ccc {

Result<SymbolSourceHandle> import_elf_symbol_table(SymbolDatabase& database, const ElfFile& elf, const SymbolTableConfig& config);
static Result<std::pair<const ElfSection*, SymbolTableFormat>> get_section_and_format(const ElfFile& elf, const SymbolTableConfig& config);
static Result<void> check_sndll_config_is_valid(const SymbolTableConfig& config);
static void filter_ast_by_flags(ast::Node& ast_node, u32 parser_flags);
static void compute_size_bytes_recursive(ast::Node& node, SymbolDatabase& database);

extern const SymbolTableFormatInfo SYMBOL_TABLE_FORMATS[] = {
	{SYMTAB, "symtab", ".symtab", 2},
	{MDEBUG, "mdebug", ".mdebug", 3},
	{STAB, "stab", ".stab", 0},
	{DWARF, "dwarf", ".debug", 0},
	{SNDLL, "sndll", ".sndata", 1}
};
extern const u32 SYMBOL_TABLE_FORMAT_COUNT = CCC_ARRAY_SIZE(SYMBOL_TABLE_FORMATS);

const SymbolTableFormatInfo* symbol_table_format_from_enum(SymbolTableFormat format) {
	for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
		if(SYMBOL_TABLE_FORMATS[i].format == format) {
			return &SYMBOL_TABLE_FORMATS[i];
		}
	}
	return nullptr;
}

const SymbolTableFormatInfo* symbol_table_format_from_name(const char* format_name) {
	for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
		if(strcmp(SYMBOL_TABLE_FORMATS[i].format_name, format_name) == 0) {
			return &SYMBOL_TABLE_FORMATS[i];
		}
	}
	return nullptr;
}

const SymbolTableFormatInfo* symbol_table_format_from_section(const char* section_name) {
	for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
		if(strcmp(SYMBOL_TABLE_FORMATS[i].section_name, section_name) == 0) {
			return &SYMBOL_TABLE_FORMATS[i];
		}
	}
	return nullptr;
}

Result<SymbolSourceHandle> import_symbol_table(SymbolDatabase& database, const SymbolFile& file, const SymbolTableConfig& config) {
	if(const ElfFile* elf = std::get_if<ElfFile>(&file)) {
		return import_elf_symbol_table(database, *elf, config);
	}
	
	if(const SNDLLFile* sndll = std::get_if<SNDLLFile>(&file)) {
		Result<void> result = check_sndll_config_is_valid(config);
		CCC_RETURN_IF_ERROR(result);
		
		return import_sndll_symbol_table(database, *sndll);
	}
	
	return CCC_FAILURE("Invalid symbol file.");
}

Result<SymbolSourceHandle> import_elf_symbol_table(SymbolDatabase& database, const ElfFile& elf, const SymbolTableConfig& config) {
	auto section_and_format = get_section_and_format(elf, config);
	CCC_RETURN_IF_ERROR(section_and_format);
	const ElfSection* section = section_and_format->first;
	SymbolTableFormat format = section_and_format->second;
	
	if(!section) {
		// No symbol table is present.
		return SymbolSourceHandle();
	}
	
	SymbolSourceHandle source;
	
	switch(format) {
		case SYMTAB: {
			Result<SymbolSourceHandle> source_result = elf::parse_symbol_table(database, *section, elf);
			CCC_RETURN_IF_ERROR(source_result);
			source = *source_result;
			
			break;
		}
		case MDEBUG: {
			mdebug::SymbolTableReader reader;
			Result<void> reader_result = reader.init(elf.image, section->offset);
			CCC_RETURN_IF_ERROR(reader_result);
			
			Result<SymbolSourceHandle> source_result = analyse(database, reader, config.parser_flags, config.demangle);
			CCC_RETURN_IF_ERROR(source_result);
			source = *source_result;
			
			// Filter the AST and compute size information for all nodes.
			#define CCC_X(SymbolType, symbol_list) \
				for(SymbolType& symbol : database.symbol_list) { \
					if(symbol.type()) { \
						filter_ast_by_flags(*symbol.type(), config.parser_flags); \
						compute_size_bytes_recursive(*symbol.type(), database); \
					} \
				}
				CCC_FOR_EACH_SYMBOL_TYPE_DO_X
			#undef CCC_X
			
			break;
		}
		case SNDLL: {
			std::span<const u8> section_data = elf.image.subspan(section->offset, section->size);
			
			const u32* magic = get_packed<u32>(section_data, 0);
			if(!magic || *magic == 0) {
				CCC_WARN("Section '%s' is empty.", section->name.c_str());
				break;
			}
			
			Result<SNDLLFile> sndll = parse_sndll_file(section_data, section->address);
			CCC_RETURN_IF_ERROR(sndll);
			
			Result<SymbolSourceHandle> source_result = import_sndll_symbol_table(database, *sndll);
			CCC_RETURN_IF_ERROR(source_result);
			source = *source_result;
			
			break;
		}
		default: {
			return CCC_FAILURE("The selected symbol table format isn't supported.");
		}
	}
	
	return source;
}

Result<void> print_symbol_table(FILE* out, const SymbolFile& file, const SymbolTableConfig& config) {
	if(const ElfFile* elf = std::get_if<ElfFile>(&file)) {
		auto section_and_format = get_section_and_format(*elf, config);
		CCC_RETURN_IF_ERROR(section_and_format);
		const ElfSection* section = section_and_format->first;
		SymbolTableFormat format = section_and_format->second;
		
		if(!section) {
			return CCC_FAILURE("No symbol table present.");
		}
		
		switch(format) {
			case SNDLL: {
				Result<SNDLLFile> sndll = parse_sndll_file(elf->image.subspan(section->offset, section->size), section->address);
				CCC_RETURN_IF_ERROR(sndll);
				print_sndll_symbols(out, *sndll);
				break;
			}
		}
	}
	
	if(const SNDLLFile* sndll = std::get_if<SNDLLFile>(&file)) {
		Result<void> result = check_sndll_config_is_valid(config);
		CCC_RETURN_IF_ERROR(result);
		
		print_sndll_symbols(out, *sndll);
	}
	
	return Result<void>();
}

static Result<std::pair<const ElfSection*, SymbolTableFormat>> get_section_and_format(const ElfFile& elf, const SymbolTableConfig& config) {
	const ElfSection* section = nullptr;
	SymbolTableFormat format = SYMTAB;
	
	if(config.section.has_value()) {
		section = elf.lookup_section(config.section->c_str());
		CCC_CHECK(section, "No '%s' section found.", config.section->c_str());
		
		if(config.format.has_value()) {
			format = *config.format;
		} else {
			const SymbolTableFormatInfo* format_info = symbol_table_format_from_section(section->name.c_str());
			CCC_CHECK(format_info, "Cannot determine symbol table format from section name.");
			
			format = format_info->format;
		}
	} else {
		// Find the most useful symbol table.
		u32 current_utility = 0;
		for(u32 i = 0; i < SYMBOL_TABLE_FORMAT_COUNT; i++) {
			if(SYMBOL_TABLE_FORMATS[i].utility > current_utility) {
				const ElfSection* current_section = elf.lookup_section(SYMBOL_TABLE_FORMATS[i].section_name);
				if(current_section) {
					section = current_section;
					format = SYMBOL_TABLE_FORMATS[i].format;
					current_utility = SYMBOL_TABLE_FORMATS[i].utility;
				}
			}
		}
		
		if(config.format.has_value()) {
			format = *config.format;
		}
	}
	
	return std::make_pair(section, format);
}

static Result<void> check_sndll_config_is_valid(const SymbolTableConfig& config) {
	CCC_CHECK(!config.section.has_value(), "ELF section specified for SNDLL file.");
	CCC_CHECK(!config.format.has_value() || *config.format == SNDLL,
		"Symbol table format specified for SNDLL file is not 'sndll'.");
	return Result<void>();
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
