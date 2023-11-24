// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "print_cpp.h"

#include <cmath>
#include <chrono>

#include "registers.h"

namespace ccc {

enum VariableNamePrintFlags {
	NO_VAR_PRINT_FLAGS = 0,
	INSERT_SPACE_TO_LEFT = (1 << 0),
	BRACKETS_IF_POINTER = (1 << 2)
};

static void print_cpp_storage_class(FILE* out, ast::StorageClass storage_class);
static void print_cpp_variable_name(FILE* out, VariableName& name, u32 flags);
static void indent(FILE* out, s32 level);

void CppPrinter::comment_block_beginning(const char* input_file) {
	if(m_has_anything_been_printed) {
		fprintf(out, "\n");
	}
	
	fprintf(out, "// File written by stdump");
	time_t cftime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	tm* t = std::localtime(&cftime);
	if(t) {
		fprintf(out, " on %04d-%02d-%02d", 1900 + t->tm_year, t->tm_mon + 1, t->tm_mday);
	}
	fprintf(out, "\n");
	fprintf(out, "// \n");
	fprintf(out, "// Input file:\n");
	fprintf(out, "//   %s\n", input_file);
	
	m_last_wants_spacing = true;
	m_has_anything_been_printed = true;
}

void CppPrinter::comment_block_toolchain_version_info(const SymbolDatabase& database) {
	std::set<std::string> toolchain_version_info;
	for(const SourceFile& source_file : database.source_files) {
		if(!source_file.toolchain_version_info.empty()) {
			for(const std::string& string : source_file.toolchain_version_info) {
				toolchain_version_info.emplace(string);
			}
		} else {
			toolchain_version_info.emplace("unknown");
		}
	}
	
	fprintf(out, "// Toolchain version(s):\n");
	for(const std::string& string : toolchain_version_info) {
		fprintf(out, "//   %s\n", string.c_str());
	}
	
	m_last_wants_spacing = true;
	m_has_anything_been_printed = true;
}

void CppPrinter::comment_block_builtin_types(const SymbolList<DataType>& data_types) {
	std::set<std::pair<std::string, ast::BuiltInClass>> builtins;
	for(const DataType& data_type : data_types) {
		if(data_type.type().descriptor == ast::BUILTIN) {
			builtins.emplace(data_type.type().name, data_type.type().as<ast::BuiltIn>().bclass);
		}
	}
	
	if(!builtins.empty()) {
		fprintf(out, "// Built-in types:\n");
		
		for(const auto& [type, bclass] : builtins) {
			fprintf(out, "//   %-25s%s\n", type.c_str(), ast::builtin_class_to_string(bclass));
		}
	}
	
	m_last_wants_spacing = true;
	m_has_anything_been_printed = true;
}

void CppPrinter::comment_block_file(const char* path) {
	if(m_has_anything_been_printed) {
		fprintf(out, "\n");
	}
	
	fprintf(out, "// *****************************************************************************\n");
	fprintf(out, "// FILE -- %s\n", path);
	fprintf(out, "// *****************************************************************************\n");
	
	m_last_wants_spacing = true;
	m_has_anything_been_printed = true;
}


void CppPrinter::begin_include_guard(const char* macro) {
	if(m_has_anything_been_printed) {
		fprintf(out, "\n");
	}
	
	fprintf(out, "#ifndef %s\n", macro);
	fprintf(out, "#define %s\n", macro);
	
	m_last_wants_spacing = true;
	m_has_anything_been_printed = true;
}

void CppPrinter::end_include_guard(const char* macro) {
	if(m_has_anything_been_printed) {
		fprintf(out, "\n");
	}
	
	fprintf(out, "#endif // %s\n", macro);
	
	m_last_wants_spacing = true;
	m_has_anything_been_printed = true;
}

void CppPrinter::include_directive(const char* path) {
	if(m_has_anything_been_printed) {
		fprintf(out, "\n");
	}
	
	fprintf(out, "#include \"%s\"\n", path);
	
	m_last_wants_spacing = true;
	m_has_anything_been_printed = true;
}

bool CppPrinter::data_type(const DataType& symbol) {
	const ast::Node& node = symbol.type();
	
	if(node.descriptor == ast::BUILTIN) {
		return false;
	}
	
	bool wants_spacing =
		node.descriptor == ast::ENUM ||
		node.descriptor == ast::STRUCT_OR_UNION;
	if(m_has_anything_been_printed && (m_last_wants_spacing || wants_spacing)) {
		fprintf(out, "\n");
	}
	
	if(symbol.conflict && (node.descriptor != ast::ENUM || !node.name.empty())) {
		fprintf(out, "// warning: multiple differing types with the same name (%s not equal)\n", symbol.compare_fail_reason);
	}
	if(node.descriptor == ast::NodeDescriptor::TYPE_NAME && node.as<ast::TypeName>().source == ast::TypeNameSource::ERROR) {
		fprintf(out, "// warning: this type name was generated to handle an error\n");
	}
	
	VariableName name;
	name.identifier = &symbol.name();
	if(node.descriptor == ast::STRUCT_OR_UNION && node.size_bits > 0) {
		m_digits_for_offset = (s32) ceilf(log2(node.size_bits / 8.f) / 4.f);
	}
	ast_node(node, name, 0);
	fprintf(out, ";\n");
	
	m_last_wants_spacing = wants_spacing;
	m_has_anything_been_printed = true;
	
	return true;
}

void CppPrinter::function(const Function& symbol, const SymbolDatabase& database) {
	if(m_config.skip_statics && symbol.storage_class == ast::SC_STATIC) {
		return;
	}
	
	if(m_config.skip_member_functions_outside_types && symbol.is_member_function_ish) {
		return;
	}
	
	std::span<const ParameterVariable> parameter_variables = database.parameter_variables.span(symbol.parameter_variables());
	std::span<const LocalVariable> local_variables = database.local_variables.span(symbol.local_variables());
	
	bool wants_spacing = m_config.print_function_bodies
		&& (!local_variables.empty() || function_bodies);
	if(m_has_anything_been_printed && (m_last_wants_spacing || wants_spacing)) {
		fprintf(out, "\n");
	}
	
	VariableName name;
	name.identifier = &symbol.demangled_name();
	
	if(m_config.print_storage_information) {
		fprintf(out, "/* %08x %08x */ ", symbol.address().value, symbol.size);
	}
	
	// Print out the storage class, return type and function name.
	print_cpp_storage_class(out, symbol.storage_class);
	if(true) {//!function.is_constructor) {
		if(symbol.type_ptr()) {
			VariableName dummy;
			ast_node(symbol.type(), dummy, 0);
			fprintf(out, " ");
		}
	}
	print_cpp_variable_name(out, name, BRACKETS_IF_POINTER);
	
	// Print out the parameter list.
	fprintf(out, "(");
	if(symbol.parameter_variables().has_value()) {
		bool skip_this = m_config.omit_this_parameter && !parameter_variables.empty() && parameter_variables[0].name() == "this";
		for(size_t i = skip_this ? 1 : 0; i < parameter_variables.size(); i++) {
			VariableName variable_name;
			variable_name.identifier = &parameter_variables[i].name();
			ast_node(parameter_variables[i].type(), variable_name, 0);
			if(i + 1 != parameter_variables.size()) {
				fprintf(out, ", ");
			}
		}
	} else {
		fprintf(out, "/* parameters unknown */");
	}
	fprintf(out, ")");
	
	// Print out the function body.
	if(m_config.print_function_bodies) {
		fprintf(out, " ");
		const std::span<char>* body = nullptr;
		if(function_bodies) {
			auto body_iter = function_bodies->find(symbol.address().value);
			if(body_iter != function_bodies->end()) {
				body = &body_iter->second;
			}
		}
		if(!local_variables.empty() || body) {
			fprintf(out, "{\n");
			if(!local_variables.empty()) {
				for(const LocalVariable& variable : local_variables) {
					indent(out, 1);
					ast_node(variable.type(), name, 1);
					fprintf(out, ";\n");
				}
			}
			if(body) {
				if(!local_variables.empty()) {
					indent(out, 1);
					fprintf(out, "\n");
				}
				fwrite(body->data(), body->size(), 1, out);
			}
			indent(out, 0);
			fprintf(out, "}");
		} else {
			fprintf(out, "{}");
		}
	} else {
		fprintf(out, ";");
	}
	
	fprintf(out, "\n");
	
	m_last_wants_spacing = wants_spacing;
	m_has_anything_been_printed = true;
}

void CppPrinter::global_variable(const GlobalVariable& symbol) {
	const ast::Node& node = symbol.type();
	
	if(m_config.skip_statics && node.storage_class == ast::SC_STATIC) {
		return;
	}
	
	bool wants_spacing = m_config.print_variable_data
	;//	&& node.data != nullptr
	//	&& node.data->descriptor == ast::INITIALIZER_LIST;
	if(m_has_anything_been_printed && (m_last_wants_spacing || wants_spacing)) {
		fprintf(out, "\n");
	}
	
	variable_storage_comment(symbol.storage);
	
	VariableName name;
	name.identifier = &symbol.demangled_name();
	ast_node(node, name, 0);
	fprintf(out, ";\n");
	
	m_last_wants_spacing = wants_spacing;
}

void CppPrinter::ast_node(const ast::Node& node, VariableName& parent_name, s32 indentation_level) {
	VariableName this_name{&node.name};
	VariableName& name = node.name.empty() ? parent_name : this_name;
	
	if(node.descriptor == ast::FUNCTION_TYPE) {
		const ast::FunctionType& func_type = node.as<ast::FunctionType>();
		if(func_type.vtable_index > -1) {
			fprintf(out, "/* vtable[%d] */ ", func_type.vtable_index);
		}
	}
	
	ast::StorageClass storage_class = (ast::StorageClass) node.storage_class;
	//if(m_config.make_globals_extern && node.descriptor == ast::VARIABLE && node.as<ast::Variable>().variable_class == ast::VariableClass::GLOBAL) {
	//	storage_class = ast::SC_EXTERN; // For printing out header files.
	//}
	print_cpp_storage_class(out, storage_class);
	
	if(node.is_const) {
		fprintf(out, "const ");
	}
	if(node.is_volatile) {
		fprintf(out, "volatile ");
	}
	
	switch(node.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = node.as<ast::Array>();
			CCC_ASSERT(array.element_type.get());
			name.array_indices.emplace_back(array.element_count);
			ast_node(*array.element_type.get(), name, indentation_level);
			break;
		}
		case ast::BITFIELD: {
			const ast::BitField& bit_field = node.as<ast::BitField>();
			CCC_ASSERT(bit_field.underlying_type.get());
			ast_node(*bit_field.underlying_type.get(), name, indentation_level);
			fprintf(out, " : %d", bit_field.size_bits);
			break;
		}
		case ast::BUILTIN: {
			const ast::BuiltIn& builtin = node.as<ast::BuiltIn>();
			if(builtin.bclass == ast::BuiltInClass::VOID) {
				fprintf(out, "void");
			} else {
				fprintf(out, "CCC_BUILTIN(%s)", builtin_class_to_string(builtin.bclass));
			}
			print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			break;
		}
		case ast::DATA: {
			const ast::Data& data = node.as<ast::Data>();
			if(!data.field_name.empty()) {
				fprintf(out, "/* %s = */ ", data.field_name.c_str());
			}
			fprintf(out, "%s", data.string.c_str());
			break;
		}
		case ast::ENUM: {
			const ast::Enum& enumeration = node.as<ast::Enum>();
			fprintf(out, "enum");
			bool name_on_top = (indentation_level == 0) && (enumeration.storage_class != ast::SC_TYPEDEF);
			if(name_on_top) {
				print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			}
			fprintf(out, " {");
			if(enumeration.size_bits > -1) {
				fprintf(out, " // 0x%x", enumeration.size_bits / 8);
			}
			fprintf(out, "\n");
			for(size_t i = 0; i < enumeration.constants.size(); i++) {
				s32 value = enumeration.constants[i].first;
				const std::string& name = enumeration.constants[i].second;
				bool is_last = i == enumeration.constants.size() - 1;
				indent(out, indentation_level + 1);
				fprintf(out, "%s = %d%s\n", name.c_str(), value, is_last ? "" : ",");
			}
			indent(out, indentation_level);
			fprintf(out, "}");
			if(!name_on_top) {
				print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			}
			break;
		}
		case ast::FUNCTION_TYPE: {
			const ast::FunctionType& function = node.as<ast::FunctionType>();
			if(function.modifier == ast::MemberFunctionModifier::STATIC) {
				fprintf(out, "static ");
			} else if(function.modifier == ast::MemberFunctionModifier::VIRTUAL) {
				fprintf(out, "virtual ");
			}
			if(!function.is_constructor) {
				if(function.return_type.has_value()) {
					VariableName dummy;
					ast_node(*function.return_type->get(), dummy, indentation_level);
					fprintf(out, " ");
				}
			}
			print_cpp_variable_name(out, name, BRACKETS_IF_POINTER);
			fprintf(out, "(");
			if(function.parameters.has_value()) {
				const std::vector<std::unique_ptr<ast::Node>>* parameters = &(*function.parameters);
				
				// The parameters provided in STABS member function declarations
				// are wrong, so are swapped out for the correct ones here.
				//if(m_config.substitute_parameter_lists && function.definition) {
				//	ast::FunctionType& definition_type = function.definition->type->as<ast::FunctionType>();
				//	if(definition_type.parameters.has_value()) {
				//		parameters = &(*definition_type.parameters);
				//	}
				//}
				
				size_t start;
				if(m_config.omit_this_parameter && parameters->size() >= 1 && (*parameters)[0]->name == "this") {
					start = 1;
				} else {
					start = 0;
				}
				for(size_t i = start; i < parameters->size(); i++) {
					VariableName dummy;
					ast_node(*(*parameters)[i].get(), dummy, indentation_level);
					if(i != parameters->size() - 1) {
						fprintf(out, ", ");
					}
				}
			} else {
				fprintf(out, "/* parameters unknown */");
			}
			fprintf(out, ")");
			break;
		}
		case ast::INITIALIZER_LIST: {
			const ast::InitializerList& list = node.as<ast::InitializerList>();
			if(!list.field_name.empty()) {
				fprintf(out, "/* %s = */ ", list.field_name.c_str());
			}
			fprintf(out, "{\n");
			for(size_t i = 0; i < list.children.size(); i++) {
				indent(out, indentation_level + 1);
				VariableName dummy;
				ast_node(*list.children[i].get(), dummy, indentation_level + 1);
				if(i != list.children.size() - 1) {
					fprintf(out, ",");
				}
				fprintf(out, "\n");
			}
			indent(out, indentation_level);
			fprintf(out, "}");
			break;
		}
		case ast::POINTER_OR_REFERENCE: {
			const ast::PointerOrReference& pointer_or_reference = node.as<ast::PointerOrReference>();
			CCC_ASSERT(pointer_or_reference.value_type.get());
			if(pointer_or_reference.is_pointer) {
				name.pointer_chars.emplace_back('*');
			} else {
				name.pointer_chars.emplace_back('&');
			}
			ast_node(*pointer_or_reference.value_type.get(), name, indentation_level);
			print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			break;
		}
		case ast::POINTER_TO_DATA_MEMBER: {
			// This probably isn't correct for nested pointers to data members
			// but for now lets not think about that.
			const ast::PointerToDataMember& member_pointer = node.as<ast::PointerToDataMember>();
			VariableName dummy;
			ast_node(*member_pointer.member_type.get(), dummy, indentation_level);
			fprintf(out, " ");
			ast_node(*member_pointer.class_type.get(), dummy, indentation_level);
			fprintf(out, "::");
			print_cpp_variable_name(out, name, NO_VAR_PRINT_FLAGS);
			break;
		}
		case ast::STRUCT_OR_UNION: {
			const ast::StructOrUnion& struct_or_union = node.as<ast::StructOrUnion>();
			s32 access_specifier = ast::AS_PUBLIC;
			if(struct_or_union.is_struct) {
				fprintf(out, "struct");
			} else {
				fprintf(out, "union");
			}
			bool name_on_top = (indentation_level == 0) && (struct_or_union.storage_class != ast::SC_TYPEDEF);
			if(name_on_top) {
				print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			}
			// Print base classes.
			if(!struct_or_union.base_classes.empty()) {
				fprintf(out, " : ");
				for(size_t i = 0; i < struct_or_union.base_classes.size(); i++) {
					ast::Node& base_class = *struct_or_union.base_classes[i].get();
					CCC_ASSERT(base_class.descriptor == ast::TypeName::DESCRIPTOR);
					offset(base_class);
					if(base_class.access_specifier != ast::AS_PUBLIC) {
						fprintf(out, "%s ", ast::access_specifier_to_string((ast::AccessSpecifier) base_class.access_specifier));
					}
					VariableName dummy;
					ast_node(base_class, dummy, indentation_level + 1);
					if(i != struct_or_union.base_classes.size() - 1) {
						fprintf(out, ", ");
					}
				}
			}
			
			fprintf(out, " {");
			if(m_config.print_offsets_and_sizes) {
				fprintf(out, " // 0x%x", struct_or_union.size_bits / 8);
			}
			fprintf(out, "\n");
			
			// Print fields.
			for(const std::unique_ptr<ast::Node>& field : struct_or_union.fields) {
				CCC_ASSERT(field.get());
				if(access_specifier != field->access_specifier) {
					indent(out, indentation_level);
					fprintf(out, "%s:\n", ast::access_specifier_to_string((ast::AccessSpecifier) field->access_specifier));
					access_specifier = field->access_specifier;
				}
				indent(out, indentation_level + 1);
				offset(*field.get());
				ast_node(*field.get(), name, indentation_level + 1);
				fprintf(out, ";\n");
			}
			// Print member functions.
			if(!struct_or_union.member_functions.empty()) {
				if(!struct_or_union.fields.empty()) {
					indent(out, indentation_level + 1);
					fprintf(out, "\n");
				}
				for(size_t i = 0; i < struct_or_union.member_functions.size(); i++) {
					ast::FunctionType& member_func = struct_or_union.member_functions[i]->as<ast::FunctionType>();
					if(access_specifier != member_func.access_specifier) {
						indent(out, indentation_level);
						fprintf(out, "%s:\n", ast::access_specifier_to_string((ast::AccessSpecifier) member_func.access_specifier));
						access_specifier = member_func.access_specifier;
					}
					indent(out, indentation_level + 1);
					ast_node(*struct_or_union.member_functions[i].get(), name, indentation_level + 1);
					fprintf(out, ";\n");
				}
			}
			indent(out, indentation_level);
			fprintf(out, "}");
			if(!name_on_top) {
				print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			}
			break;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = node.as<ast::TypeName>();
			fprintf(out, "%s", type_name.type_name.c_str());
			print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			break;
		}
	}
}

static void print_cpp_storage_class(FILE* out, ast::StorageClass storage_class) {
	switch(storage_class) {
		case ast::SC_NONE: break;
		case ast::SC_TYPEDEF: fprintf(out, "typedef "); break;
		case ast::SC_EXTERN: fprintf(out, "extern "); break;
		case ast::SC_STATIC: fprintf(out, "static "); break;
		case ast::SC_AUTO: fprintf(out, "auto "); break;
		case ast::SC_REGISTER: fprintf(out, "register "); break;
	}
}

static void print_cpp_variable_name(FILE* out, VariableName& name, u32 flags) {
	bool has_name = name.identifier != nullptr && !name.identifier->empty();
	bool has_brackets = (flags & BRACKETS_IF_POINTER) && !name.pointer_chars.empty();
	if(has_name && (flags & INSERT_SPACE_TO_LEFT)) {
		fprintf(out, " ");
	}
	if(has_brackets) {
		fprintf(out, "(");
	}
	for(s32 i = (s32) name.pointer_chars.size() - 1; i >= 0; i--) {
		fprintf(out, "%c", name.pointer_chars[i]);
	}
	name.pointer_chars.clear();
	if(has_name) {
		fprintf(out, "%s", name.identifier->c_str());
		name.identifier = nullptr;
	}
	for(s32 index : name.array_indices) {
		fprintf(out, "[%d]", index);
	}
	name.array_indices.clear();
	if(has_brackets) {
		fprintf(out, ")");
	}
}

void CppPrinter::variable_storage_comment(const Variable::Storage& storage) {
	if(m_config.print_storage_information) {
		fprintf(out, "/* ");
		if(const Variable::GlobalStorage* global_storage = std::get_if<Variable::GlobalStorage>(&storage)) {
			fprintf(out, "%s", Variable::GlobalStorage::location_to_string(global_storage->location));
			if(global_storage->address.valid()) {
				fprintf(out, " %x", global_storage->address.value);
			}
		}
		if(const Variable::RegisterStorage* register_storage = std::get_if<Variable::RegisterStorage>(&storage)) {
			auto [register_class, register_index_relative] =
				mips::map_dbx_register_index(register_storage->dbx_register_number);
			const char** name_table = mips::REGISTER_STRING_TABLES[(s32) register_class];
			CCC_ASSERT((u64) register_index_relative < mips::REGISTER_STRING_TABLE_SIZES[(s32) register_class]);
			const char* register_name = name_table[register_index_relative];
			fprintf(out, "%s %d", register_name, register_storage->dbx_register_number);
		}
		if(const Variable::StackStorage* stack_storage = std::get_if<Variable::StackStorage>(&storage)) {
			if(stack_storage->stack_pointer_offset >= 0) {
				fprintf(out, "0x%x(sp)", stack_storage->stack_pointer_offset);
			} else {
				fprintf(out, "-0x%x(sp)", -stack_storage->stack_pointer_offset);
			}
		}
		fprintf(out, " */ ");
	}
}

void CppPrinter::offset(const ast::Node& node) {
	if(m_config.print_offsets_and_sizes && node.storage_class != ast::SC_STATIC && node.absolute_offset_bytes > -1) {
		CCC_ASSERT(m_digits_for_offset > -1 && m_digits_for_offset < 100);
		fprintf(out, "/* 0x%0*x", m_digits_for_offset, node.absolute_offset_bytes);
		if(node.descriptor == ast::BITFIELD) {
			fprintf(out, ":%d", node.as<ast::BitField>().bitfield_offset_bits);
		}
		fprintf(out, " */ ");
	}
}

static void indent(FILE* out, s32 level) {
	for(s32 i = 0; i < level; i++) {
		fputc('\t', out);
	}
}

}
