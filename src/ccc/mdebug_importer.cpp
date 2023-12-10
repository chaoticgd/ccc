// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "mdebug_importer.h"

#include "mdebug_analysis.h"

namespace ccc::mdebug {

static Result<void> import_files(SymbolDatabase& database, const AnalysisContext& context);
static Result<void> import_file(SymbolDatabase& database, s32 file_index, const AnalysisContext& context);
static Result<void> resolve_type_names(SymbolDatabase& database, SymbolSourceHandle source);
static Result<void> resolve_type_name(ast::TypeName& type_name, SymbolDatabase& database, SymbolSourceHandle source);

Result<SymbolSourceHandle> import_symbol_table(SymbolDatabase& database, const mdebug::SymbolTableReader& reader, u32 parser_flags, DemanglerFunc* demangle) {
	Result<std::vector<mdebug::Symbol>> external_symbols = reader.parse_external_symbols();
	CCC_RETURN_IF_ERROR(external_symbols);
	
	Result<SymbolSource*> symbol_source = database.symbol_sources.create_symbol(".mdebug", SymbolSourceHandle());
	CCC_RETURN_IF_ERROR(symbol_source);
	
	// The addresses of the global variables aren't present in the local symbol
	// table, so here we extract them from the external table.
	std::map<std::string, const mdebug::Symbol*> globals;
	for(const mdebug::Symbol& external : *external_symbols) {
		if(external.symbol_type == mdebug::SymbolType::GLOBAL
			&& (external.symbol_class != mdebug::SymbolClass::UNDEFINED)) {
			globals[external.string] = &external;
		}
	}
	
	// Bundle together some unchanging state to pass to import_files.
	AnalysisContext context;
	context.reader = &reader;
	context.globals = &globals;
	context.symbol_source = (*symbol_source)->handle();
	context.parser_flags = parser_flags;
	context.demangle = demangle;
	
	Result<void> result = import_files(database, context);
	if(!result.success()) {
		database.destroy_symbols_from_source((*symbol_source)->handle());
		return result;
	}
	
	return (*symbol_source)->handle();
}

static Result<void> import_files(SymbolDatabase& database, const AnalysisContext& context) {
	Result<s32> file_count = context.reader->file_count();
	CCC_RETURN_IF_ERROR(file_count);
	
	for(s32 i = 0; i < *file_count; i++) {
		Result<void> result = import_file(database, i, context);
		CCC_RETURN_IF_ERROR(result);
	}
	
	// The files field may be modified by further analysis passes, so we
	// need to save this information here.
	for(DataType& data_type : database.data_types) {
		if(data_type.source() == context.symbol_source && data_type.files.size() == 1) {
			data_type.probably_defined_in_cpp_file = true;
		}
	}
	
	// Lookup data types and store data type handles in type names.
	Result<void> type_name_result = resolve_type_names(database, context.symbol_source);
	CCC_RETURN_IF_ERROR(type_name_result);
	
	return Result<void>();
}

static Result<void> import_file(SymbolDatabase& database, s32 file_index, const AnalysisContext& context) {
	Result<mdebug::File> input = context.reader->parse_file(file_index);
	CCC_RETURN_IF_ERROR(input);
	
	Result<SourceFile*> source_file = database.source_files.create_symbol(input->full_path, context.symbol_source);
	CCC_RETURN_IF_ERROR(source_file);
	
	// Sometimes the INFO symbols contain information about what toolchain
	// version was used for building the executable.
	for(mdebug::Symbol& symbol : input->symbols) {
		if(symbol.symbol_class == mdebug::SymbolClass::INFO && strcmp(symbol.string, "@stabs") != 0) {
			(*source_file)->toolchain_version_info.emplace(symbol.string);
		}
	}
	
	// Parse the stab strings into a data structure that's vaguely
	// one-to-one with the text-based representation.
	u32 parser_flags_for_this_file = context.parser_flags;
	Result<std::vector<ParsedSymbol>> symbols = parse_symbols(input->symbols, parser_flags_for_this_file);
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
	stabs_to_ast_state.parser_flags = parser_flags_for_this_file;
	
	// Convert the parsed stabs symbols to a more standard C AST.
	LocalSymbolTableAnalyser analyser(database, stabs_to_ast_state, context, **source_file);
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
						Result<void> result = analyser.local_variable(
							name, type, symbol.raw->value, symbol.name_colon_type.descriptor, symbol.raw->symbol_class);
						CCC_RETURN_IF_ERROR(result);
						break;
					}
					case StabsSymbolDescriptor::GLOBAL_VARIABLE:
					case StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE: {
						const char* name = symbol.name_colon_type.name.c_str();
						u32 address = -1;
						std::optional<GlobalStorageLocation> location =
							symbol_class_to_global_variable_location(symbol.raw->symbol_class);
						if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::GLOBAL_VARIABLE) {
							// The address for non-static global variables is
							// only stored in the external symbol table (and
							// the ELF symbol table), so we pull that
							// information in here.
							auto global_symbol = context.globals->find(symbol.name_colon_type.name);
							if(global_symbol != context.globals->end()) {
								address = (u32) global_symbol->second->value;
								location = symbol_class_to_global_variable_location(global_symbol->second->symbol_class);
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
				Result<void> result = analyser.lbrac(symbol.raw->value);
				CCC_RETURN_IF_ERROR(result);
				break;
			}
			case ParsedSymbolType::RBRAC: {
				Result<void> result = analyser.rbrac(symbol.raw->value);
				CCC_RETURN_IF_ERROR(result);
				break;
			}
			case ParsedSymbolType::FUNCTION_END: {
				Result<void> result = analyser.function_end();
				CCC_RETURN_IF_ERROR(result);
				break;
			}
			case ParsedSymbolType::NON_STABS: {
				if(symbol.raw->symbol_class == mdebug::SymbolClass::TEXT) {
					if(symbol.raw->symbol_type == mdebug::SymbolType::PROC) {
						Result<void> result = analyser.procedure(symbol.raw->string, symbol.raw->value, false);
						CCC_RETURN_IF_ERROR(result);
					} else if(symbol.raw->symbol_type == mdebug::SymbolType::STATICPROC) {
						Result<void> result = analyser.procedure(symbol.raw->string, symbol.raw->value, true);
						CCC_RETURN_IF_ERROR(result);
					} else if(symbol.raw->symbol_type == mdebug::SymbolType::LABEL) {
						Result<void> result = analyser.label(symbol.raw->string, symbol.raw->value, symbol.raw->index);
						CCC_RETURN_IF_ERROR(result);
					} else if(symbol.raw->symbol_type == mdebug::SymbolType::END) {
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

static Result<void> resolve_type_names(SymbolDatabase& database, SymbolSourceHandle source) {
	Result<void> result;
	database.for_each_symbol([&](ccc::Symbol& symbol) {
		if(symbol.source() == source && symbol.type()) {
			ast::for_each_node(*symbol.type(), ast::PREORDER_TRAVERSAL, [&](ast::Node& node) {
				if(node.descriptor == ast::TYPE_NAME) {
					Result<void> type_name_result = resolve_type_name(node.as<ast::TypeName>(), database, source);
					if(!type_name_result.success()) {
						result = std::move(type_name_result);
					}
				}
				return ast::EXPLORE_CHILDREN;
			});
		}
	});
	return result;
}

static Result<void> resolve_type_name(ast::TypeName& type_name, SymbolDatabase& database, SymbolSourceHandle source) {
	// Lookup the type by its STABS type number. This path ensures that the
	// correct type is found even if multiple types have the same name.
	if(type_name.stabs_read_state.referenced_file_handle != (u32) -1 && type_name.stabs_read_state.stabs_type_number_type > -1) {
		const SourceFile* source_file = database.source_files.symbol_from_handle(type_name.stabs_read_state.referenced_file_handle);
		CCC_ASSERT(source_file);
		StabsTypeNumber stabs_type_number = {
			type_name.stabs_read_state.stabs_type_number_file,
			type_name.stabs_read_state.stabs_type_number_type
		};
		auto handle = source_file->stabs_type_number_to_handle.find(stabs_type_number);
		if(handle != source_file->stabs_type_number_to_handle.end()) {
			type_name.data_type_handle = handle->second.value;
		}
	}
	
	type_name.is_forward_declared = true;
	
	// Looking up the type by its STABS type number failed, so look for it by
	// its name instead. This happens when a type is forward declared but not
	// defined in a given translation unit.
	for(auto& name_handle : database.data_types.handles_from_name(type_name.stabs_read_state.type_name)) {
		DataType* data_type = database.data_types.symbol_from_handle(name_handle.second);
		if(data_type->source() == source) {
			type_name.data_type_handle = name_handle.second.value;
			return Result<void>();
		}
	}
	
	// Type lookup failed. This happens when a type is forward declared in a
	// translation unit with symbols but is not defined in one. We haven't
	// already created a forward declared type, so we create one now.
	Result<DataType*> forward_declared_type = database.data_types.create_symbol(type_name.stabs_read_state.type_name, source);
	CCC_RETURN_IF_ERROR(forward_declared_type);
	
	type_name.data_type_handle = (*forward_declared_type)->handle().value;
	
	std::unique_ptr<ast::ForwardDeclared> forward_declared_node = std::make_unique<ast::ForwardDeclared>();
	forward_declared_node->type = type_name.stabs_read_state.type;
	(*forward_declared_type)->set_type_once(std::move(forward_declared_node));
	
	return Result<void>();
}

void fill_in_pointers_to_member_function_definitions(SymbolDatabase& database) {
	// Fill in pointers from member function declaration to corresponding definitions.
	for(Function& function : database.functions) {
		const std::string& demangled_name = function.name();
		std::string::size_type name_separator_pos = demangled_name.find_last_of("::");
		if(name_separator_pos != std::string::npos && name_separator_pos > 0) {
			std::string function_name = demangled_name.substr(name_separator_pos + 1);
			// This won't work for some template types, and that's okay.
			std::string::size_type type_separator_pos = demangled_name.find_last_of("::", name_separator_pos - 2);
			std::string type_name;
			if(type_separator_pos != std::string::npos) {
				type_name = demangled_name.substr(type_separator_pos + 1, name_separator_pos - type_separator_pos - 2);
			} else {
				type_name = demangled_name.substr(0, name_separator_pos - 1);
			}
			for(const auto& name_handle : database.data_types.handles_from_name(type_name)) {
				DataType* data_type = database.data_types.symbol_from_handle(name_handle.second);
				if(data_type && data_type->type() && data_type->type()->descriptor == ast::STRUCT_OR_UNION) {
					ast::StructOrUnion& struct_or_union = data_type->type()->as<ast::StructOrUnion>();
					for(std::unique_ptr<ast::Node>& declaration : struct_or_union.member_functions) {
						if(declaration->name == function_name) {
							declaration->as<ast::Function>().definition_handle = function.handle().value;
							function.is_member_function_ish = true;
						}
					}
				}
			}
		}
	}
}

}
