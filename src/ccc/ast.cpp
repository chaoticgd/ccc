// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "ast.h"

#include "symbol_database.h"
#include "symbol_table.h"

namespace ccc::ast {

static bool compare_nodes_and_merge(CompareResult& dest, const Node& node_lhs, const Node& node_rhs, const SymbolDatabase& database);
static void try_to_match_wobbly_typedefs(CompareResult& result, const Node& node_lhs, const Node& node_rhs, const SymbolDatabase& database);

void Node::set_access_specifier(AccessSpecifier specifier, u32 parser_flags) {
	if((parser_flags & NO_ACCESS_SPECIFIERS) == 0) {
		access_specifier = specifier;
	}
}

// Some enums have two symbols associated with them: One named " " and another
// one referencing the first.
void remove_duplicate_enums(std::vector<std::unique_ptr<Node>>& ast_nodes) {
	for(size_t i = 0; i < ast_nodes.size(); i++) {
		Node& node = *ast_nodes[i].get();
		if(node.descriptor == NodeDescriptor::ENUM && node.name.empty()) {
			bool match = false;
			for(std::unique_ptr<Node>& other : ast_nodes) {
				bool is_match = other.get() != &node
					&& other->descriptor == NodeDescriptor::ENUM
					&& !other->name.empty()
					&& other->as<Enum>().constants == node.as<Enum>().constants;
				if(is_match) {
					match = true;
					break;
				}
			}
			if(match) {
				ast_nodes.erase(ast_nodes.begin() + i);
				i--;
			}
		}
	}
}

void remove_duplicate_self_typedefs(std::vector<std::unique_ptr<Node>>& ast_nodes) {
	//for(size_t i = 0; i < ast_nodes.size(); i++) {
	//	Node& node = *ast_nodes[i].get();
	//	if(node.descriptor == TYPE_NAME && node.as<TypeName>().type_name == node.name) {
	//		bool match = false;
	//		for(std::unique_ptr<Node>& other : ast_nodes) {
	//			bool is_match = other.get() != &node
	//				&& (other->descriptor == ENUM
	//					|| other->descriptor == STRUCT_OR_UNION)
	//				&& other->name == node.name;
	//			if(is_match) {
	//				match = true;
	//				break;
	//			}
	//		}
	//		if(match) {
	//			ast_nodes.erase(ast_nodes.begin() + i);
	//			i--;
	//		}
	//	}
	//}
}

CompareResult compare_nodes(const Node& node_lhs, const Node& node_rhs, const SymbolDatabase& database, bool check_intrusive_fields) {
	CompareResult result = CompareResultType::MATCHES_NO_SWAP;
	if(node_lhs.descriptor != node_rhs.descriptor) return CompareFailReason::DESCRIPTOR;
	if(check_intrusive_fields) {
		if(node_lhs.storage_class != node_rhs.storage_class) return CompareFailReason::STORAGE_CLASS;
		if(node_lhs.name != node_rhs.name && !(node_lhs.is_vtable_pointer && node_rhs.is_vtable_pointer)) return CompareFailReason::NAME;
		if(node_lhs.relative_offset_bytes != node_rhs.relative_offset_bytes) return CompareFailReason::RELATIVE_OFFSET_BYTES;
		if(node_lhs.absolute_offset_bytes != node_rhs.absolute_offset_bytes) return CompareFailReason::ABSOLUTE_OFFSET_BYTES;
		if(node_lhs.size_bits != node_rhs.size_bits) return CompareFailReason::SIZE_BITS;
		if(node_lhs.is_const != node_rhs.is_const) return CompareFailReason::CONSTNESS;
	}
	// We intentionally don't compare files, conflict symbol or compare_fail_reason here.
	switch(node_lhs.descriptor) {
		case ARRAY: {
			const auto [lhs, rhs] = Node::as<Array>(node_lhs, node_rhs);
			if(compare_nodes_and_merge(result, *lhs.element_type.get(), *rhs.element_type.get(), database)) return result;
			if(lhs.element_count != rhs.element_count) return CompareFailReason::ARRAY_ELEMENT_COUNT;
			break;
		}
		case BITFIELD: {
			const auto [lhs, rhs] = Node::as<BitField>(node_lhs, node_rhs);
			if(lhs.bitfield_offset_bits != rhs.bitfield_offset_bits) return CompareFailReason::BITFIELD_OFFSET_BITS;
			if(compare_nodes_and_merge(result, *lhs.underlying_type.get(), *rhs.underlying_type.get(), database)) return result;
			break;
		}
		case BUILTIN: {
			const auto [lhs, rhs] = Node::as<BuiltIn>(node_lhs, node_rhs);
			if(lhs.bclass != rhs.bclass) return CompareFailReason::BUILTIN_CLASS;
			break;
		}
		case ENUM: {
			const auto [lhs, rhs] = Node::as<Enum>(node_lhs, node_rhs);
			if(lhs.constants != rhs.constants) return CompareFailReason::ENUM_CONSTANTS;
			break;
		}
		case FORWARD_DECLARED: {
			const auto [lhs, rhs] = Node::as<ForwardDeclared>(node_lhs, node_rhs);
			if(lhs.type != rhs.type) return CompareFailReason::DESCRIPTOR;
			break;
		}
		case FUNCTION: {
			const auto [lhs, rhs] = Node::as<Function>(node_lhs, node_rhs);
			if(lhs.return_type.has_value() != rhs.return_type.has_value()) return CompareFailReason::FUNCTION_RETURN_TYPE_HAS_VALUE;
			if(lhs.return_type.has_value()) {
				if(compare_nodes_and_merge(result, *lhs.return_type->get(), *rhs.return_type->get(), database)) return result;
			}
			if(lhs.parameters.has_value() && rhs.parameters.has_value()) {
				if(lhs.parameters->size() != rhs.parameters->size()) return CompareFailReason::FUNCTION_PARAMAETER_COUNT;
				for(size_t i = 0; i < lhs.parameters->size(); i++) {
					if(compare_nodes_and_merge(result, *(*lhs.parameters)[i].get(), *(*rhs.parameters)[i].get(), database)) return result;
				}
			} else if(lhs.parameters.has_value() != rhs.parameters.has_value()) {
				return CompareFailReason::FUNCTION_PARAMETERS_HAS_VALUE;
			}
			if(lhs.modifier != rhs.modifier) return CompareFailReason::FUNCTION_MODIFIER;
			if(lhs.is_constructor != rhs.is_constructor) return CompareFailReason::FUNCTION_IS_CONSTRUCTOR;
			break;
		}
		case POINTER_OR_REFERENCE: {
			const auto [lhs, rhs] = Node::as<PointerOrReference>(node_lhs, node_rhs);
			if(lhs.is_pointer != rhs.is_pointer) return CompareFailReason::DESCRIPTOR;
			if(compare_nodes_and_merge(result, *lhs.value_type.get(), *rhs.value_type.get(), database)) return result;
			break;
		}
		case POINTER_TO_DATA_MEMBER: {
			const auto [lhs, rhs] = Node::as<PointerToDataMember>(node_lhs, node_rhs);
			if(compare_nodes_and_merge(result, *lhs.class_type.get(), *rhs.class_type.get(), database)) return result;
			if(compare_nodes_and_merge(result, *lhs.member_type.get(), *rhs.member_type.get(), database)) return result;
			break;
		}
		case STRUCT_OR_UNION: {
			const auto [lhs, rhs] = Node::as<StructOrUnion>(node_lhs, node_rhs);
			if(lhs.is_struct != rhs.is_struct) return CompareFailReason::DESCRIPTOR;
			if(lhs.base_classes.size() != rhs.base_classes.size()) return CompareFailReason::BASE_CLASS_COUNT;
			for(size_t i = 0; i < lhs.base_classes.size(); i++) {
				if(compare_nodes_and_merge(result, *lhs.base_classes[i].get(), *rhs.base_classes[i].get(), database)) return result;
			}
			if(lhs.fields.size() != rhs.fields.size()) return CompareFailReason::FIELDS_SIZE;
			for(size_t i = 0; i < lhs.fields.size(); i++) {
				if(compare_nodes_and_merge(result, *lhs.fields[i].get(), *rhs.fields[i].get(), database)) return result;
			}
			if(lhs.member_functions.size() != rhs.member_functions.size()) return CompareFailReason::MEMBER_FUNCTION_COUNT;
			for(size_t i = 0; i < lhs.member_functions.size(); i++) {
				if(compare_nodes_and_merge(result, *lhs.member_functions[i].get(), *rhs.member_functions[i].get(), database)) return result;
			}
			break;
		}
		case TYPE_NAME: {
			const auto [lhs, rhs] = Node::as<TypeName>(node_lhs, node_rhs);
			// Don't check the source so that REFERENCE and CROSS_REFERENCE are
			// treated as the same.
			if(lhs.data_type_handle != (u32) -1) {
				if(lhs.data_type_handle != rhs.data_type_handle) return CompareFailReason::TYPE_NAME;
			} else {
				if(lhs.stabs_read_state.type_name != rhs.stabs_read_state.type_name) return CompareFailReason::TYPE_NAME;
			}
			break;
		}
	}
	return result;
}

static bool compare_nodes_and_merge(CompareResult& dest, const Node& node_lhs, const Node& node_rhs, const SymbolDatabase& database) {
	CompareResult result = compare_nodes(node_lhs, node_rhs, database, true);
	try_to_match_wobbly_typedefs(result, node_lhs, node_rhs, database);
	if(dest.type != result.type) {
		if(dest.type == CompareResultType::DIFFERS || result.type == CompareResultType::DIFFERS) {
			// If any of the inner types differ, the outer type does too.
			dest.type = CompareResultType::DIFFERS;
		} else if(dest.type == CompareResultType::MATCHES_CONFUSED || result.type == CompareResultType::MATCHES_CONFUSED) {
			// Propagate confusion.
			dest.type = CompareResultType::MATCHES_CONFUSED; 
		} else if(dest.type == CompareResultType::MATCHES_FAVOUR_LHS && result.type == CompareResultType::MATCHES_FAVOUR_RHS) {
			// One of the results favours the LHS node and the other favours the
			// RHS node so we are confused.
			dest.type = CompareResultType::MATCHES_CONFUSED; 
		} else if(dest.type == CompareResultType::MATCHES_FAVOUR_RHS && result.type == CompareResultType::MATCHES_FAVOUR_LHS) {
			// One of the results favours the LHS node and the other favours the
			// RHS node so we are confused.
			dest.type = CompareResultType::MATCHES_CONFUSED; 
		} else if(dest.type == CompareResultType::MATCHES_FAVOUR_LHS || result.type == CompareResultType::MATCHES_FAVOUR_LHS) {
			// One of the results favours the LHS node and the other is neutral
			// so go with the LHS node.
			dest.type = CompareResultType::MATCHES_FAVOUR_LHS;
		} else if(dest.type == CompareResultType::MATCHES_FAVOUR_RHS || result.type == CompareResultType::MATCHES_FAVOUR_RHS) {
			// One of the results favours the RHS node and the other is neutral
			// so go with the RHS node.
			dest.type = CompareResultType::MATCHES_FAVOUR_RHS;
		}
	}
	if(dest.fail_reason == CompareFailReason::NONE) {
		dest.fail_reason = result.fail_reason;
	}
	return dest.type == CompareResultType::DIFFERS;
}

static void try_to_match_wobbly_typedefs(CompareResult& result, const Node& node_lhs, const Node& node_rhs, const SymbolDatabase& database) {
	// Detect if one side has a typedef when the other just has the plain type.
	// This was previously a common reason why type deduplication would fail.
	const Node* type_name_node = &node_lhs;
	const Node* raw_node = &node_rhs;
	for(s32 i = 0; result.type == CompareResultType::DIFFERS && i < 2; i++) {
		if(type_name_node->descriptor == TYPE_NAME) {
			const TypeName& type_name = type_name_node->as<TypeName>();
			const TypeName::StabsReadState& read_state = type_name.stabs_read_state;
			if(read_state.referenced_file_handle != (u32) -1 && read_state.stabs_type_number_type > -1) {
				const SourceFile* source_file = database.source_files.symbol_from_handle(read_state.referenced_file_handle);
				CCC_ASSERT(source_file);
				StabsTypeNumber stabs_type_number = {
					read_state.stabs_type_number_file,
					read_state.stabs_type_number_type
				};
				auto handle = source_file->stabs_type_number_to_handle.find(stabs_type_number);
				if(handle != source_file->stabs_type_number_to_handle.end()) {
					const DataType* referenced_type = database.data_types.symbol_from_handle(handle->second);
					CCC_ASSERT(referenced_type && referenced_type->type());
					// Don't compare 'intrusive' fields e.g. the offset.
					CompareResult new_result = compare_nodes(*referenced_type->type(), *raw_node, database, false);
					if(new_result.type != CompareResultType::DIFFERS) {
						result.type = (i == 0)
							? CompareResultType::MATCHES_FAVOUR_LHS
							: CompareResultType::MATCHES_FAVOUR_RHS;
					}
				}
			}
		}
		std::swap(type_name_node, raw_node);
	}
}

const char* compare_fail_reason_to_string(CompareFailReason reason) {
	switch(reason) {
		case CompareFailReason::NONE: return "error";
		case CompareFailReason::DESCRIPTOR: return "descriptor";
		case CompareFailReason::STORAGE_CLASS: return "storage class";
		case CompareFailReason::NAME: return "name";
		case CompareFailReason::RELATIVE_OFFSET_BYTES: return "relative offset";
		case CompareFailReason::ABSOLUTE_OFFSET_BYTES: return "absolute offset";
		case CompareFailReason::BITFIELD_OFFSET_BITS: return "bitfield offset";
		case CompareFailReason::SIZE_BITS: return "size";
		case CompareFailReason::CONSTNESS: return "constness";
		case CompareFailReason::ARRAY_ELEMENT_COUNT: return "array element count";
		case CompareFailReason::BUILTIN_CLASS: return "builtin class";
		case CompareFailReason::FUNCTION_RETURN_TYPE_HAS_VALUE: return "function return type has value";
		case CompareFailReason::FUNCTION_PARAMAETER_COUNT: return "function paramaeter count";
		case CompareFailReason::FUNCTION_PARAMETERS_HAS_VALUE: return "function parameter";
		case CompareFailReason::FUNCTION_MODIFIER: return "function modifier";
		case CompareFailReason::FUNCTION_IS_CONSTRUCTOR: return "function is constructor";
		case CompareFailReason::ENUM_CONSTANTS: return "enum constant";
		case CompareFailReason::BASE_CLASS_COUNT: return "base class count";
		case CompareFailReason::FIELDS_SIZE: return "fields size";
		case CompareFailReason::MEMBER_FUNCTION_COUNT: return "member function count";
		case CompareFailReason::VTABLE_GLOBAL: return "vtable global";
		case CompareFailReason::TYPE_NAME: return "type name";
		case CompareFailReason::VARIABLE_CLASS: return "variable class";
		case CompareFailReason::VARIABLE_TYPE: return "variable type";
		case CompareFailReason::VARIABLE_STORAGE: return "variable storage";
		case CompareFailReason::VARIABLE_BLOCK: return "variable block";
	}
	return "";
}

const char* node_type_to_string(const Node& node) {
	switch(node.descriptor) {
		case ARRAY: return "array";
		case BITFIELD: return "bitfield";
		case BUILTIN: return "builtin";
		case ENUM: return "enum";
		case FORWARD_DECLARED: return "forward_declared";
		case FUNCTION: return "function";
		case POINTER_OR_REFERENCE: {
			const PointerOrReference& pointer_or_reference = node.as<PointerOrReference>();
			if(pointer_or_reference.is_pointer) {
				return "pointer";
			} else {
				return "reference";
			}
		}
		case POINTER_TO_DATA_MEMBER: return "pointer_to_data_member";
		case STRUCT_OR_UNION: {
			const StructOrUnion& struct_or_union = node.as<StructOrUnion>();
			if(struct_or_union.is_struct) {
				return "struct";
			} else {
				return "union";
			}
		}
		case TYPE_NAME: return "type_name";
	}
	return "CCC_BAD_NODE_DESCRIPTOR";
}

const char* storage_class_to_string(StorageClass storage_class) {
	switch(storage_class) {
		case SC_NONE: return "none";
		case SC_TYPEDEF: return "typedef";
		case SC_EXTERN: return "extern";
		case SC_STATIC: return "static";
		case SC_AUTO: return "auto";
		case SC_REGISTER: return "register";
	}
	return "";
}

const char* access_specifier_to_string(AccessSpecifier specifier) {
	switch(specifier) {
		case AS_PUBLIC: return "public";
		case AS_PROTECTED: return "protected";
		case AS_PRIVATE: return "private";
	}
	return "";
}

const char* builtin_class_to_string(BuiltInClass bclass) {
	switch(bclass) {
		case BuiltInClass::VOID: return "void";
		case BuiltInClass::UNSIGNED_8: return "8-bit unsigned integer";
		case BuiltInClass::SIGNED_8: return "8-bit signed integer";
		case BuiltInClass::UNQUALIFIED_8: return "8-bit integer";
		case BuiltInClass::BOOL_8: return "8-bit boolean";
		case BuiltInClass::UNSIGNED_16: return "16-bit unsigned integer";
		case BuiltInClass::SIGNED_16: return "16-bit signed integer";
		case BuiltInClass::UNSIGNED_32: return "32-bit unsigned integer";
		case BuiltInClass::SIGNED_32: return "32-bit signed integer";
		case BuiltInClass::FLOAT_32: return "32-bit floating point";
		case BuiltInClass::UNSIGNED_64: return "64-bit unsigned integer";
		case BuiltInClass::SIGNED_64: return "64-bit signed integer";
		case BuiltInClass::FLOAT_64: return "64-bit floating point";
		case BuiltInClass::UNSIGNED_128: return "128-bit unsigned integer";
		case BuiltInClass::SIGNED_128: return "128-bit signed integer";
		case BuiltInClass::UNQUALIFIED_128: return "128-bit integer";
		case BuiltInClass::FLOAT_128: return "128-bit floating point";
	}
	return "";
}

s32 builtin_class_size(BuiltInClass bclass) {
	switch(bclass) {
		case BuiltInClass::VOID: return 0;
		case BuiltInClass::UNSIGNED_8: return 1;
		case BuiltInClass::SIGNED_8: return 1;
		case BuiltInClass::UNQUALIFIED_8: return 1;
		case BuiltInClass::BOOL_8: return 1;
		case BuiltInClass::UNSIGNED_16: return 2;
		case BuiltInClass::SIGNED_16: return 2;
		case BuiltInClass::UNSIGNED_32: return 4;
		case BuiltInClass::SIGNED_32: return 4;
		case BuiltInClass::FLOAT_32: return 4;
		case BuiltInClass::UNSIGNED_64: return 8;
		case BuiltInClass::SIGNED_64: return 8;
		case BuiltInClass::FLOAT_64: return 8;
		case BuiltInClass::UNSIGNED_128: return 16;
		case BuiltInClass::SIGNED_128: return 16;
		case BuiltInClass::UNQUALIFIED_128: return 16;
		case BuiltInClass::FLOAT_128: return 16;
	}
	return 0;
}

}
