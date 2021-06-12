#include "ccc.h"

static TypeName resolve_c_type_name(const std::map<s32, const StabsType*>& types, const StabsType* type_ptr);
static AstNode stabs_field_to_ast(FieldInfo field, const std::map<s32, TypeName>& type_names);
static AstNode leaf_node(s32 offset, s32 size, const std::string& type, const std::string& name, const std::vector<s32>& array_indices);
static AstNode enum_node(s32 offset, s32 size, const EnumFields& fields, const std::string& name);
static AstNode typedef_node(const std::string& type, const std::string& name);
static AstNode struct_or_union_node(
		s32 offset, s32 size, bool is_struct,
		const std::vector<AstNode>& fields,
		const std::string& name,
		const std::vector<s32>& array_indices);
static void indent(FILE* output, s32 depth);

s32 type_number_of(StabsType* type) {
	verify(type && !type->anonymous, "error: Tried to access type number of anonymous or null type.\n");
	return type->type_number;
}

std::map<s32, TypeName> resolve_c_type_names(const std::map<s32, const StabsType*>& types) {
	std::map<s32, TypeName> type_names;
	for(auto& [type_number, type] : types) {
		assert(type);
		type_names[type_number] = resolve_c_type_name(types, type);
	}
	return type_names;
}

const StabsType* find_type(StabsType* type, const std::map<s32, const StabsType*>& types, s32 outer_type_number) {
	assert(type && !type->anonymous);
	if(type->type_number == outer_type_number) {
		return nullptr;
	}
	auto iterator = types.find(type->type_number);
	verify(iterator != types.end(), "error: Tried to lookup undeclared type.\n");
	assert(iterator->second);
	return iterator->second;
}

// FIXME: Detect indirect recusion e.g. type mappings 1 -> 2, 2 -> 1.
static TypeName resolve_c_type_name(const std::map<s32, const StabsType*>& types, const StabsType* type_ptr) {
	if(!type_ptr) {
		return {"/* error: null type */ void*", {}};
	}
	const StabsType& type = *type_ptr;
	
	if(type.name.has_value()) {
		TypeName name;
		name.first_part = *type.name;
		return name;
	}
	
	if(!type.has_body) {
		TypeName name;
		name.first_part = "/* error: no body */ void*";
		return name;
	}
	
	switch(type.descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE: {
			auto inner_type = find_type(type.type_reference.type.get(), types, type.type_number);
			return resolve_c_type_name(types, inner_type);
		}
		case StabsTypeDescriptor::ARRAY: {
			auto inner_type = find_type(type.array_type.element_type.get(), types, type.type_number);
			TypeName name = resolve_c_type_name(types, inner_type);
			StabsType* index = type.array_type.index_type.get();
			assert(index);
			verify(index->descriptor == StabsTypeDescriptor::RANGE && index->range_type.low == 0,
				"error: Invalid index type for array.\n");
			name.array_indices.push_back(index->range_type.high + 1);
			return name;
		}
		case StabsTypeDescriptor::FUNCTION: {
			return {"/* function */ void", {}};
		}
		case StabsTypeDescriptor::RANGE: {
			return {"/* range */ void", {}};
		}
		case StabsTypeDescriptor::STRUCT: {
			return {"/* struct */ void", {}};
		}
		case StabsTypeDescriptor::UNION: {
			return {"/* union */ void", {}};
		}
		case StabsTypeDescriptor::CROSS_REFERENCE: {
			TypeName type_name;
			type_name.first_part = type.cross_reference.identifier;
			return type_name;
		}
		case StabsTypeDescriptor::METHOD: {
			TypeName type_name;
			type_name.first_part = std::string("<err method>");
			return type_name;
		}
		case StabsTypeDescriptor::REFERENCE:
		case StabsTypeDescriptor::POINTER: {
			auto inner_type = find_type(type.reference_or_pointer.value_type.get(), types, type.type_number);
			TypeName name = resolve_c_type_name(types, inner_type);
			name.first_part += (s8) type.descriptor;
			return name;
		}
		case StabsTypeDescriptor::MEMBER: {
			TypeName type_name;
			type_name.first_part = std::string("<err member>");
			return type_name;
		}
		default:
			verify_not_reached("error: Unexpected type descriptor.\n");
	}
}

static const TypeName& lookup_type_name(s32 type_number, const std::map<s32, TypeName>& type_names) {
	auto iterator = type_names.find(type_number);
	verify(iterator != type_names.end(), "error: Undeclared type referenced: %d.\n", type_number);
	return iterator->second;
}


std::optional<AstNode> stabs_symbol_to_ast(const StabsSymbol& symbol, const std::map<s32, TypeName>& type_names) {
	switch(symbol.descriptor) {
		case StabsSymbolDescriptor::TYPE_NAME: {
			StabsType* referenced_type = symbol.type.type_reference.type.get();
			if(!referenced_type || referenced_type->type_number == symbol.type.type_number) {
				return std::nullopt;
			}
			verify(!symbol.type.anonymous && referenced_type && !referenced_type->anonymous,
				"error: Invalid type name: %s.\n", symbol.raw.c_str());
			auto type_name = lookup_type_name(referenced_type->type_number, type_names);
			return typedef_node(type_name.first_part, symbol.name);
		}
		case StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG:
			if(!symbol.type.has_body) {
				return std::nullopt;
			}
			return stabs_field_to_ast({0, 0, symbol.type, symbol.name}, type_names);
		default:
			verify_not_reached("error: Unexpected symbol descriptor: %c.\n", (s8) symbol.descriptor);
	}
}

static AstNode stabs_field_to_ast(FieldInfo field, const std::map<s32, TypeName>& type_names) {
	s32 offset = field.offset;
	s32 size = field.size;
	const StabsType& type = field.type;
	const std::string& name = field.name;
	
	if(!type.has_body) {
		const TypeName& type_name = lookup_type_name(type.type_number, type_names);
		return leaf_node(offset, size, type_name.first_part, name, type_name.array_indices);
	}
	
	switch(type.descriptor) {
		case StabsTypeDescriptor::STRUCT:
		case StabsTypeDescriptor::UNION: {
			bool is_struct = type.descriptor == StabsTypeDescriptor::STRUCT;
			std::vector<AstNode> fields;
			for(const StabsField& child : type.struct_or_union.fields) {
				fields.emplace_back(stabs_field_to_ast({child.offset, child.size, child.type, child.name}, type_names));
			}
			return struct_or_union_node(0, 0, is_struct, fields, name, {});
		}
		default: {
			const TypeName& type_name = lookup_type_name(type.type_number, type_names);
			return leaf_node(offset, size, type_name.first_part, name, type_name.array_indices);
		}
	}
}

static AstNode leaf_node(s32 offset, s32 size, const std::string& type, const std::string& name, const std::vector<s32>& array_indices) {
	AstNode node;
	node.offset = offset;
	node.size = size;
	node.name = name;
	node.descriptor = AstNodeDescriptor::LEAF;
	node.array_indices = array_indices;
	node.leaf.type_name = type;
	return node;
}

static AstNode enum_node(s32 offset, s32 size, const EnumFields& fields, const std::string& name) {
	AstNode node;
	node.offset = offset;
	node.size = size;
	node.name = name;
	node.descriptor = AstNodeDescriptor::ENUM;
	node.array_indices = {};
	node.enum_type.fields = fields;
	return node;
}

static AstNode struct_or_union_node(
		s32 offset, s32 size, bool is_struct,
		const std::vector<AstNode>& fields,
		const std::string& name,
		const std::vector<s32>& array_indices) {
	AstNode node;
	node.offset = offset;
	node.size = size;
	node.name = name;
	node.descriptor = is_struct ? AstNodeDescriptor::STRUCT : AstNodeDescriptor::UNION;
	node.array_indices = {};
	node.struct_or_union.fields = fields;
	return node;
}

static AstNode typedef_node(const std::string& type, const std::string& name) {
	AstNode node;
	node.offset = 0;
	node.size = 0;
	node.name = name;
	node.descriptor = AstNodeDescriptor::TYPEDEF;
	node.typedef_type.type_name = type;
	return node;
}

void print_ast_node(FILE* output, const AstNode& node, int depth) {
	switch(node.descriptor) {
		case AstNodeDescriptor::LEAF: {
			indent(output, depth);
			if(node.leaf.type_name.size() > 0) {
				fprintf(output, "%s", node.leaf.type_name.c_str());
			} else {
				fprintf(output, "/* error: empty type string */ int");
			}
			break;
		}
		case AstNodeDescriptor::ENUM: {
			indent(output, depth);
			fprintf(output, "enum {\n");
			for(auto& [value, name] : node.enum_type.fields) {
				bool is_last = value == node.enum_type.fields.back().first;
				indent(output, depth + 1);
				fprintf(output, "%s = %d%s\n", name.c_str(), value, is_last ? "" : ",");
			}
			indent(output, depth);
			printf("}");
			break;
		}
		case AstNodeDescriptor::STRUCT:
		case AstNodeDescriptor::UNION: {
			indent(output, depth);
			if(node.descriptor == AstNodeDescriptor::STRUCT) {
				fprintf(output, "struct");
			} else {
				fprintf(output, "enum");
			}
			fprintf(output, " %s {\n", node.name.c_str());
			for(const AstNode& child : node.struct_or_union.fields) {
				print_ast_node(output, child, depth + 1);
			}
			indent(output, depth);
			printf("}");
			break;
		}
		case AstNodeDescriptor::TYPEDEF: {
			indent(output, depth);
			printf("typedef %s", node.typedef_type.type_name.c_str());
			break;
		}
	}
	fprintf(output, " %s", node.name.c_str());
	for(s32 index : node.array_indices) {
		fprintf(output, "[%d]", index);
	}
	fprintf(output, ";\n");
}

static void indent(FILE* output, s32 depth) {
	for(s32 i = 0; i < depth; i++) {
		fprintf(output, "\t");
	}
}

void print_ast_node_test(FILE* output, const char* result_variable, const char* parent_struct, const AstNode& node, int depth) {
	switch(node.descriptor) {
		case AstNodeDescriptor::LEAF: {
			fprintf(output, "\t%s &= offsetof(%s, %s) == 0x%x;\n", result_variable, parent_struct, node.name.c_str(), node.offset);
			//fprintf(output, "\t%s &= sizeof(%s, %s) == 0x%x;\n", result_variable, parent_struct, node.name.c_str(), node.size);
		}
		case AstNodeDescriptor::STRUCT:
		case AstNodeDescriptor::UNION: {
			if(depth == 0) {
				for(const AstNode& child : node.struct_or_union.fields) {
					print_ast_node_test(output, result_variable, parent_struct, child, depth + 1);
				}
			}
		}
	}
}
