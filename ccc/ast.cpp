#include "ast.h"

namespace ccc {

static TypeName resolve_c_type_name(const std::map<s32, const StabsType*>& types, const StabsType* type_ptr);
static AstNode stabs_field_to_ast(FieldInfo field, const std::map<s32, TypeName>& type_names);
static AstNode leaf_node(bool is_static, s32 offset, s32 size, const std::string& type, const std::string& name, const std::vector<s32>& array_indices);
static AstNode enum_node(bool is_static, s32 offset, s32 size, const EnumFields& fields, const std::string& name);
static AstNode typedef_node(const std::string& type, const std::string& name);
static AstNode struct_or_union_node(
		bool is_static, s32 offset, s32 size, bool is_struct,
		const std::vector<AstBaseClass>& base_classes,
		const std::vector<AstNode>& fields,
		const std::string& name,
		const std::vector<s32>& array_indices);
static bool compare_ast_nodes(const AstNode& lhs, const AstNode& rhs);

s32 type_number_of(const StabsType* type) {
	verify(type && !type->anonymous, "Tried to access type number of anonymous or null type.");
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
	verify(iterator != types.end(), "Tried to lookup undeclared type.");
	assert(iterator->second);
	return iterator->second;
}

// FIXME: Detect indirect recusion e.g. type mappings 1 -> 2, 2 -> 1.
static TypeName resolve_c_type_name(const std::map<s32, const StabsType*>& types, const StabsType* type_ptr) {
	if(!type_ptr) {
		return {"/* error: null type */ void*", {}};
	}
	const StabsType& type = *type_ptr;
	
	if(type.name.has_value() && type.descriptor != StabsTypeDescriptor::CROSS_REFERENCE) {
		TypeName name;
		switch(type.descriptor) {
			case StabsTypeDescriptor::ENUM: name.first_part += "enum "; break;
			case StabsTypeDescriptor::STRUCT: name.first_part += "struct "; break;
			case StabsTypeDescriptor::UNION: name.first_part += "union "; break;
		}
		name.first_part += *type.name;
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
				"Invalid index type for array.");
			name.array_indices.push_back(index->range_type.high + 1);
			return name;
		}
		case StabsTypeDescriptor::FUNCTION: {
			return {"/* function */ void*", {}};
		}
		case StabsTypeDescriptor::RANGE: {
			return {"/* range */ void*", {}};
		}
		case StabsTypeDescriptor::STRUCT: {
			return {"/* struct */ void*", {}};
		}
		case StabsTypeDescriptor::UNION: {
			return {"/* union */ void*", {}};
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
			verify_not_reached("Unexpected type descriptor.");
	}
}

static const TypeName& lookup_type_name(s32 type_number, const std::map<s32, TypeName>& type_names) {
	auto iterator = type_names.find(type_number);
	verify(iterator != type_names.end(), "Undeclared type referenced: %d.", type_number);
	return iterator->second;
}

std::optional<AstNode> stabs_symbol_to_ast(const StabsSymbol& symbol, const std::map<s32, TypeName>& type_names) {
	if(symbol.type.has_body) {
		if(symbol.type.descriptor == StabsTypeDescriptor::TYPE_REFERENCE) {
			StabsType* referenced_type = symbol.type.type_reference.type.get();
			if(!referenced_type || referenced_type->type_number == symbol.type.type_number) {
				return std::nullopt;
			}
			verify(!symbol.type.anonymous && referenced_type && !referenced_type->anonymous,
				"error: Invalid type name: %s.\n", symbol.raw.c_str());
			auto type_name = lookup_type_name(referenced_type->type_number, type_names);
			return typedef_node(type_name.first_part, symbol.name);
		} else {
		return stabs_field_to_ast({false, 0, 0, symbol.type, symbol.name}, type_names);
		}
	} else {
		return std::nullopt;
	}
}

static AstNode stabs_field_to_ast(FieldInfo field, const std::map<s32, TypeName>& type_names) {
	s32 offset = field.offset;
	s32 size = field.size;
	const StabsType& type = field.type;
	const std::string& name = field.name;
	
	if(!type.has_body) {
		const TypeName& type_name = lookup_type_name(type.type_number, type_names);
		return leaf_node(field.is_static, offset, size, type_name.first_part, name, type_name.array_indices);
	}
	
	switch(type.descriptor) {
		case StabsTypeDescriptor::ENUM:
			return enum_node(field.is_static, offset, size, type.enum_type.fields, name);
		case StabsTypeDescriptor::STRUCT:
		case StabsTypeDescriptor::UNION: {
			bool is_struct = type.descriptor == StabsTypeDescriptor::STRUCT;
			std::vector<AstBaseClass> base_classes;
			for(const StabsBaseClass& stabs_base_class : type.struct_or_union.base_classes) {
				AstBaseClass base_class;
				base_class.visibility = stabs_base_class.visibility;
				base_class.offset = stabs_base_class.offset;
				s32 base_class_type_number = type_number_of(&stabs_base_class.type);
				base_class.type_name = lookup_type_name(base_class_type_number, type_names).first_part;
				base_classes.emplace_back(std::move(base_class));
			}
			std::vector<AstNode> fields;
			for(const StabsField& child : type.struct_or_union.fields) {
				fields.emplace_back(stabs_field_to_ast({child.is_static, child.offset, child.size, child.type, child.name}, type_names));
			}
			return struct_or_union_node(field.is_static, offset, size, is_struct, base_classes, fields, name, {});
		}
		default: {
			const TypeName& type_name = lookup_type_name(type.type_number, type_names);
			return leaf_node(field.is_static, offset, size, type_name.first_part, name, type_name.array_indices);
		}
	}
}

static AstNode leaf_node(bool is_static, s32 offset, s32 size, const std::string& type, const std::string& name, const std::vector<s32>& array_indices) {
	AstNode node;
	node.is_static = is_static;
	node.offset = offset;
	node.size = size;
	node.name = name;
	node.descriptor = AstNodeDescriptor::LEAF;
	node.array_indices = array_indices;
	node.leaf.type_name = type;
	return node;
}

static AstNode enum_node(bool is_static, s32 offset, s32 size, const EnumFields& fields, const std::string& name) {
	AstNode node;
	node.is_static = is_static;
	node.offset = offset;
	node.size = size;
	node.name = name;
	node.descriptor = AstNodeDescriptor::ENUM;
	node.array_indices = {};
	node.enum_type.fields = fields;
	return node;
}

static AstNode struct_or_union_node(
		bool is_static, s32 offset, s32 size, bool is_struct,
		const std::vector<AstBaseClass>& base_classes,
		const std::vector<AstNode>& fields,
		const std::string& name,
		const std::vector<s32>& array_indices) {
	AstNode node;
	node.is_static = is_static;
	node.offset = offset;
	node.size = size;
	node.name = name;
	node.descriptor = is_struct ? AstNodeDescriptor::STRUCT : AstNodeDescriptor::UNION;
	node.array_indices = {};
	node.struct_or_union.base_classes = base_classes;
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

std::vector<AstNode> deduplicate_ast(const std::vector<std::pair<std::string, std::vector<AstNode>>>& per_file_ast) {
	std::vector<AstNode> deduplicated_ast;
	for(const auto& [file, ast] : per_file_ast) {
		for(const AstNode& node : ast) {
			auto iterator = std::find_if(BEGIN_END(deduplicated_ast),
				[&](AstNode& other) { return other.name == node.name; });
			if(iterator != deduplicated_ast.end()) {
				AstNode& other = *iterator;
				if(!compare_ast_nodes(node, other)) {
					other.conflicting_types = true;
				}
				other.source_files.emplace(file);
			} else {
				AstNode new_node = node;
				new_node.source_files.emplace(file);
				deduplicated_ast.emplace_back(std::move(new_node));
			}
		}
	}
	return deduplicated_ast;
}

static bool compare_ast_nodes(const AstNode& lhs, const AstNode& rhs) {
	if(lhs.offset != rhs.offset) return false;
	if(lhs.size != rhs.size) return false;
	if(lhs.name != rhs.name) return false;
	if(lhs.descriptor != rhs.descriptor) return false;
	switch(lhs.descriptor) {
		case AstNodeDescriptor::LEAF:
			if(lhs.leaf.type_name != rhs.leaf.type_name) return false;
			break;
		case AstNodeDescriptor::ENUM:
			if(lhs.enum_type.fields != rhs.enum_type.fields) return false;
			break;
		case AstNodeDescriptor::STRUCT:
		case AstNodeDescriptor::UNION:
			if(lhs.struct_or_union.fields.size() != rhs.struct_or_union.fields.size()) return false;
			for(size_t i = 0; i < lhs.struct_or_union.fields.size(); i++) {
				const AstNode& lhs_field = lhs.struct_or_union.fields[i];
				const AstNode& rhs_field = rhs.struct_or_union.fields[i];
				if(!compare_ast_nodes(lhs_field, rhs_field)) return false;
			}
			break;
		case AstNodeDescriptor::TYPEDEF:
			if(lhs.typedef_type.type_name != rhs.typedef_type.type_name) return false;
			break;
	}
	return true;
}

}
