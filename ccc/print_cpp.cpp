#include "print_cpp.h"

#include <cmath>
#include <chrono>

namespace ccc {

enum VariableNamePrintFlags {
	NO_VAR_PRINT_FLAGS = 0,
	INSERT_SPACE_TO_LEFT = (1 << 0),
	BRACKETS_IF_POINTER = (1 << 2)
};

static void print_cpp_storage_class(FILE* out, ast::StorageClass storage_class);
static void print_cpp_variable_name(FILE* out, VariableName& name, u32 flags);
static void print_cpp_offset(FILE* out, const ast::Node& node, const CppPrinter& printer);
static void indent(FILE* out, s32 level);

void CppPrinter::comment_block_beginning(const fs::path& input_file) {
	fprintf(out, "// File written by stdump");
	time_t cftime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	tm* t = std::localtime(&cftime);
	if(t) {
		fprintf(out, " on %04d-%02d-%02d", 1900 + t->tm_year, t->tm_mon + 1, t->tm_mday);
	}
	fprintf(out, "\n// \n");
	fprintf(out, "// Input file:\n");
	fprintf(out, "//   %s\n", input_file.filename().string().c_str());
}

void CppPrinter::comment_block_compiler_version_info(const mdebug::SymbolTable& symbol_table) {
	std::set<std::string> compiler_version_info;
	for(const mdebug::SymFileDescriptor& fd : symbol_table.files) {
		bool known = false;
		for(const mdebug::Symbol& symbol : fd.symbols) {
			if(symbol.storage_class == mdebug::SymbolClass::INFO && strcmp(symbol.string, "@stabs") != 0) {
				known = true;
				compiler_version_info.emplace(symbol.string);
			}
		}
		if(!known) {
			compiler_version_info.emplace("unknown");
		}
	}
	
	fprintf(out, "// Toolchain version(s):\n");
	for(const std::string& string : compiler_version_info) {
		fprintf(out, "//   %s\n", string.c_str());
	}
}

void CppPrinter::comment_block_builtin_types(const std::vector<std::unique_ptr<ast::Node>>& ast_nodes) {
	std::set<std::pair<std::string, BuiltInClass>> builtins;
	for(const std::unique_ptr<ast::Node>& node : ast_nodes) {
		if(node->descriptor == ast::BUILTIN) {
			builtins.emplace(node->name, node->as<ast::BuiltIn>().bclass);
		}
	}
	
	if(!builtins.empty()) {
		fprintf(out, "// Built-in types:\n");
		
		for(const auto& [type, bclass] : builtins) {
			fprintf(out, "//   %-25s%s\n", type.c_str(), builtin_class_to_string(bclass));
		}
	}
}

bool CppPrinter::top_level_type(const ast::Node& node, bool is_last) {
	if(node.descriptor == ast::BUILTIN) {
		return false;
	}
	bool multiline =
		node.descriptor == ast::INLINE_ENUM ||
		node.descriptor == ast::INLINE_STRUCT_OR_UNION;
	if(!last_type_was_multiline && multiline) {
		fprintf(out, "\n");
	}
	if(node.conflict) {
		fprintf(out, "// warning: multiple differing types with the same name (#%d, %s not equal)\n", node.files.at(0), node.compare_fail_reason);
	}
	if(node.descriptor == ast::NodeDescriptor::TYPE_NAME && node.as<ast::TypeName>().source == ast::TypeNameSource::ERROR) {
		fprintf(out, "// warning: this type name was generated to handle an error\n");
	}
	if(verbose && node.symbol != nullptr) {
		fprintf(out, "// symbol: %s\n", node.symbol->raw->string);
	}
	VariableName name;
	if(node.descriptor == ast::INLINE_STRUCT_OR_UNION && node.size_bits > 0) {
		digits_for_offset = (s32) ceilf(log2(node.size_bits / 8.f) / 4.f);
	}
	ast_node(node, name, 0);
	fprintf(out, ";\n");
	
	if(multiline && !is_last) {
		fprintf(out, "\n");
	}
	last_type_was_multiline = multiline;
	
	return true;
}

bool CppPrinter::ast_node(const ast::Node& node, VariableName& parent_name, s32 indentation_level) {
	VariableName this_name{&node.name};
	VariableName& name = node.name.empty() ? parent_name : this_name;
	
	if(skip_statics && node.storage_class == ast::SC_STATIC) {
		return false;
	}
	
	if(node.descriptor == ast::FUNCTION_DEFINITION) {
		const ast::FunctionDefinition& func_def = node.as<ast::FunctionDefinition>();
		if(print_storage_information && func_def.address_range.valid()) {
			fprintf(out, "/* %08x %08x */ ", func_def.address_range.low, func_def.address_range.high);
		}
	} else if(node.descriptor == ast::FUNCTION_TYPE) {
		const ast::FunctionType& func_type = node.as<ast::FunctionType>();
		if(func_type.vtable_index > -1) {
			fprintf(out, "/* vtable[%d] */ ", func_type.vtable_index);
		}
	} else if(node.descriptor == ast::VARIABLE) {
		const ast::Variable& variable = node.as<ast::Variable>();
		print_variable_storage_comment(variable.storage);
	}
	
	ast::StorageClass storage_class = (ast::StorageClass) node.storage_class;
	if(force_extern && storage_class != ast::SC_STATIC && node.descriptor == ast::VARIABLE) {
		storage_class = ast::SC_EXTERN; // For printing out header files.
	}
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
			assert(array.element_type.get());
			name.array_indices.emplace_back(array.element_count);
			ast_node(*array.element_type.get(), name, indentation_level);
			break;
		}
		case ast::BITFIELD: {
			const ast::BitField& bit_field = node.as<ast::BitField>();
			assert(bit_field.underlying_type.get());
			ast_node(*bit_field.underlying_type.get(), name, indentation_level);
			fprintf(out, " : %d", bit_field.size_bits);
			break;
		}
		case ast::BUILTIN: {
			const ast::BuiltIn& builtin = node.as<ast::BuiltIn>();
			if(builtin.bclass == BuiltInClass::VOID) {
				fprintf(out, "void");
			} else {
				fprintf(out, "CCC_BUILTIN(%s)", builtin_class_to_string(builtin.bclass));
			}
			print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			break;
		}
		case ast::FUNCTION_DEFINITION: {
			const ast::FunctionDefinition& func_def = node.as<ast::FunctionDefinition>();
			ast_node(*func_def.type.get(), name, indentation_level);
			if(print_function_bodies) {
				fprintf(out, " ");
				const std::span<char>* body = nullptr;
				if(function_bodies) {
					auto body_iter = function_bodies->find(func_def.address_range.low);
					if(body_iter != function_bodies->end()) {
						body = &body_iter->second;
					}
				}
				if(!func_def.locals.empty() || body) {
					fprintf(out, "{\n");
					for(const std::unique_ptr<ast::Variable>& variable : func_def.locals) {
						indent(out, indentation_level + 1);
						ast_node(*variable.get(), name, indentation_level + 1);
						fprintf(out, ";\n");
					}
					if(body) {
						if(!func_def.locals.empty()) {
							indent(out, indentation_level + 1);
							fprintf(out, "\n");
						}
						fwrite(body->data(), body->size(), 1, out);
					}
					indent(out, indentation_level);
					fprintf(out, "}");
				} else {
					fprintf(out, "{}");
				}
			} else {
				fprintf(out, ";");
			}
			break;
		}
		case ast::FUNCTION_TYPE: {
			const ast::FunctionType& function = node.as<ast::FunctionType>();
			if(function.modifier == MemberFunctionModifier::STATIC) {
				fprintf(out, "static ");
			} else if(function.modifier == MemberFunctionModifier::VIRTUAL) {
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
				for(size_t i = 0; i < function.parameters->size(); i++) {
					VariableName dummy;
					ast_node(*(*function.parameters)[i].get(), dummy, indentation_level);
					if(i != function.parameters->size() - 1) {
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
			const ast::InitializerList& init_list = node.as<ast::InitializerList>();
			fprintf(out, "{\n");
			for(size_t i = 0; i < init_list.children.size(); i++) {
				indent(out, indentation_level + 1);
				VariableName dummy;
				ast_node(*init_list.children[i].get(), dummy, indentation_level + 1);
				if(i != init_list.children.size() - 1) {
					fprintf(out, ",");
				}
				fprintf(out, "\n");
			}
			indent(out, indentation_level);
			fprintf(out, "}");
			break;
		}
		case ast::INLINE_ENUM: {
			const ast::InlineEnum& inline_enum = node.as<ast::InlineEnum>();
			fprintf(out, "enum");
			bool name_on_top = (indentation_level == 0) && (inline_enum.storage_class != ast::SC_TYPEDEF);
			if(name_on_top) {
				print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			}
			fprintf(out, " {");
			if(inline_enum.size_bits > -1) {
				fprintf(out, " // 0x%x", inline_enum.size_bits / 8);
			}
			fprintf(out, "\n");
			for(size_t i = 0; i < inline_enum.constants.size(); i++) {
				s32 value = inline_enum.constants[i].first;
				const std::string& name = inline_enum.constants[i].second;
				bool is_last = i == inline_enum.constants.size() - 1;
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
		case ast::INLINE_STRUCT_OR_UNION: {
			const ast::InlineStructOrUnion& struct_or_union = node.as<ast::InlineStructOrUnion>();
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
					assert(base_class.descriptor == ast::TypeName::DESCRIPTOR);
					print_cpp_offset(out, base_class, *this);
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
			if(print_offsets_and_sizes) {
				fprintf(out, " // 0x%x", struct_or_union.size_bits / 8);
			}
			fprintf(out, "\n");
			
			// Print fields.
			for(const std::unique_ptr<ast::Node>& field : struct_or_union.fields) {
				assert(field.get());
				if(access_specifier != field->access_specifier) {
					indent(out, indentation_level);
					fprintf(out, "%s:\n", ast::access_specifier_to_string((ast::AccessSpecifier) field->access_specifier));
					access_specifier = field->access_specifier;
				}
				indent(out, indentation_level + 1);
				print_cpp_offset(out, *field.get(), *this);
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
		case ast::LITERAL: {
			const ast::Literal& literal = node.as<ast::Literal>();
			switch(literal.literal_type) {
				case ast::LiteralType::BOOLEAN: {
					fprintf(out, "%s", literal.value.boolean ? "true" : "false");
					break;
				}
				case ast::LiteralType::FLOAT_SINGLE: {
					fprintf(out, "%.9g", literal.value.float_single);
					break;
				}
				case ast::LiteralType::FLOAT_DOUBLE: {
					fprintf(out, "%.17g", literal.value.float_double);
					break;
				}
				case ast::LiteralType::INTEGER_SIGNED: {
					fprintf(out, "%" PRId64, literal.value.integer);
					break;
				}
				case ast::LiteralType::INTEGER_UNSIGNED: {
					fprintf(out, "%" PRIu64, literal.value.unsigned_integer);
					break;
				}
				case ast::LiteralType::STRING: {
					if(literal.value.string) {
						fprintf(out, "\"%s\"", literal.value.string);
					} else {
						fprintf(out, "NULL");
					}
					break;
				}
				case ast::LiteralType::VECTOR: {
					fprintf(out, "VECTOR(%.9gf, %.9gf, %.9gf, %.9gf)",
						literal.value.vector[0],
						literal.value.vector[1],
						literal.value.vector[2],
						literal.value.vector[3]);
					break;
				}
			}
			break;
		}
		case ast::POINTER: {
			const ast::Pointer& pointer = node.as<ast::Pointer>();
			assert(pointer.value_type.get());
			name.pointer_chars.emplace_back('*');
			ast_node(*pointer.value_type.get(), name, indentation_level);
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
		case ast::REFERENCE: {
			const ast::Reference& reference = node.as<ast::Reference>();
			assert(reference.value_type.get());
			name.pointer_chars.emplace_back('&');
			ast_node(*reference.value_type.get(), name, indentation_level);
			print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			break;
		}
		case ast::SOURCE_FILE: {
			const ast::SourceFile& source_file = node.as<ast::SourceFile>();
			for(const std::unique_ptr<ast::Node>& type : source_file.data_types) {
				ast_node(*type.get(), name, indentation_level);
			}
			for(const std::unique_ptr<ast::Node>& function : source_file.functions) {
				ast_node(*function.get(), name, indentation_level);
			}
			for(const std::unique_ptr<ast::Node>& global : source_file.globals) {
				ast_node(*global.get(), name, indentation_level);
			}
			break;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = node.as<ast::TypeName>();
			fprintf(out, "%s", type_name.type_name.c_str());
			print_cpp_variable_name(out, name, INSERT_SPACE_TO_LEFT);
			break;
		}
		case ast::VARIABLE: {
			const ast::Variable& variable = node.as<ast::Variable>();
			ast_node(*variable.type.get(), name, indentation_level);
			if(variable.data.get()) {
				fprintf(out, " = ");
				ast_node(*variable.data.get(), name, indentation_level);
			}
			break;
		}
	}
	
	return true;
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

static void print_cpp_offset(FILE* out, const ast::Node& node, const CppPrinter& printer) {
	if(printer.print_offsets_and_sizes && node.storage_class != ast::SC_STATIC && node.absolute_offset_bytes > -1) {
		assert(printer.digits_for_offset > -1 && printer.digits_for_offset < 100);
		fprintf(out, "/* 0x%0*x", printer.digits_for_offset, node.absolute_offset_bytes);
		if(node.descriptor == ast::BITFIELD) {
			fprintf(out, ":%d", node.as<ast::BitField>().bitfield_offset_bits);
		}
		fprintf(out, " */ ");
	}
}

void CppPrinter::print_variable_storage_comment(const ast::VariableStorage& storage) {
	if(print_storage_information) {
		fprintf(out, "/* ");
		if(storage.type == ast::VariableStorageType::GLOBAL) {
			fprintf(out, "%s", ast::global_variable_location_to_string(storage.global_location));
			if(storage.global_address != -1) {
				fprintf(out, " %x", storage.global_address);
			}
		} else if(storage.type == ast::VariableStorageType::REGISTER) {
			const char** name_table = mips::REGISTER_STRING_TABLES[(s32) storage.register_class];
			assert(storage.register_index_relative < mips::REGISTER_STRING_TABLE_SIZES[(s32) storage.register_class]);
			const char* register_name = name_table[storage.register_index_relative];
			fprintf(out, "%s %d", register_name, storage.dbx_register_number);
		} else {
			if(storage.stack_pointer_offset >= 0) {
				fprintf(out, "0x%x(sp)", storage.stack_pointer_offset);
			} else {
				fprintf(out, "-0x%x(sp)", -storage.stack_pointer_offset);
			}
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
