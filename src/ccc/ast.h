// This file is part of the Chaos Compiler Collection.
//
// SPDX-License-Identifier: MIT

#pragma once

#include "util.h"

namespace ccc {

// TODO: Figure out what to do about this.

// These are used to reference STABS types from other types within a single
// translation unit. For most games these will just be a single number, the type
// number. In some cases, for example with the homebrew SDK, type numbers are a
// pair of two numbers surrounded by round brackets e.g. (1,23) where the first
// number is the index of the include file to use (includes are listed for each
// translation unit separately), and the second number is the type number.
struct StabsTypeNumber {
	s32 file = -1;
	s32 type = -1;
	
	friend auto operator<=>(const StabsTypeNumber& lhs, const StabsTypeNumber& rhs) = default;
};

struct SymbolTable;

}

namespace ccc::ast {

enum StorageClass {
	SC_NONE = 0,
	SC_TYPEDEF = 1,
	SC_EXTERN = 2,
	SC_STATIC = 3,
	SC_AUTO = 4,
	SC_REGISTER = 5
};

enum NodeDescriptor : u8 {
	ARRAY,
	BITFIELD,
	BUILTIN,
	DATA,
	ENUM,
	FUNCTION_TYPE,
	INITIALIZER_LIST,
	POINTER_OR_REFERENCE,
	POINTER_TO_DATA_MEMBER,
	STRUCT_OR_UNION,
	TYPE_NAME
};

enum AccessSpecifier {
	AS_PUBLIC = 0,
	AS_PROTECTED = 1,
	AS_PRIVATE = 2
};

// To add a new type of node:
//  1. Add it to the NodeDescriptor enum.
//  2. Create a struct for it.
//  3. Add support for it in for_each_node.
//  4. Add support for it in compute_size_bytes_recursive.
//  5. Add support for it in compare_nodes.
//  6. Add support for it in node_type_to_string.
//  7. Add support for it in CppPrinter::ast_node.
//  8. Add support for it in print_json_ast_node.
//  9. Add support for it in refine_global_variable.
struct Node {
	NodeDescriptor descriptor;
	u8 is_const : 1 = false;
	u8 is_volatile : 1 = false;
	u8 is_base_class : 1 = false;
	u8 cannot_compute_size : 1 = false;
	u8 is_member_function_ish : 1 = false; // Filled in by fill_in_pointers_to_member_function_definitions.
	mutable u8 is_currently_processing : 1 = false; // Used for preventing infinite recursion.
	u8 storage_class : 4 = SC_NONE;
	u8 access_specifier : 2 = AS_PUBLIC;
	
	s32 computed_size_bytes = -1; // Calculated by compute_size_bytes_recursive.
	
	// If the name isn't populated for a given node, the name from the last
	// ancestor to have one should be used i.e. when processing the tree you
	// should pass the name down.
	std::string name;
	
	StabsTypeNumber stabs_type_number;
	
	s32 relative_offset_bytes = -1; // Offset relative to start of last inline struct/union.
	s32 absolute_offset_bytes = -1; // Offset relative to outermost struct/union.
	s32 size_bits = -1; // Size stored in the symbol table.
	
	Node(NodeDescriptor d) : descriptor(d) {}
	Node(const Node& rhs) = default;
	virtual ~Node() {}
	
	template <typename SubType>
	SubType& as() {
		CCC_ASSERT(descriptor == SubType::DESCRIPTOR);
		return *static_cast<SubType*>(this);
	}
	
	template <typename SubType>
	const SubType& as() const {
		CCC_ASSERT(descriptor == SubType::DESCRIPTOR);
		return *static_cast<const SubType*>(this);
	}
	
	template <typename SubType>
	static std::pair<const SubType&, const SubType&> as(const Node& lhs, const Node& rhs) {
		CCC_ASSERT(lhs.descriptor == SubType::DESCRIPTOR && rhs.descriptor == SubType::DESCRIPTOR);
		return std::pair<const SubType&, const SubType&>(static_cast<const SubType&>(lhs), static_cast<const SubType&>(rhs));
	}
};

struct Array : Node {
	std::unique_ptr<Node> element_type;
	s32 element_count = -1;
	
	Array() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = ARRAY;
};

struct BitField : Node {
	s32 bitfield_offset_bits = -1; // Offset relative to the last byte (not the position of the underlying type!).
	std::unique_ptr<Node> underlying_type;
	
	BitField() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = BITFIELD;
};

enum class BuiltInClass {
	VOID,
	UNSIGNED_8, SIGNED_8, UNQUALIFIED_8, BOOL_8,
	UNSIGNED_16, SIGNED_16,
	UNSIGNED_32, SIGNED_32, FLOAT_32,
	UNSIGNED_64, SIGNED_64, FLOAT_64,
	UNSIGNED_128, SIGNED_128, UNQUALIFIED_128, FLOAT_128,
	UNKNOWN_PROBABLY_ARRAY
};

struct BuiltIn : Node {
	BuiltInClass bclass;
	
	BuiltIn() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = BUILTIN;
};

// Used for printing out the values of global variables. Not supported by the
// JSON format!
struct Data : Node {
	std::string field_name;
	std::string string;
	
	Data() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = DATA;
};

struct Enum : Node {
	std::vector<std::pair<s32, std::string>> constants;
	
	Enum() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = ENUM;
};

enum class MemberFunctionModifier {
	NONE,
	STATIC,
	VIRTUAL
};

struct FunctionType : Node {
	std::optional<std::unique_ptr<Node>> return_type;
	std::optional<std::vector<std::unique_ptr<Node>>> parameters;
	MemberFunctionModifier modifier = MemberFunctionModifier::NONE;
	s32 vtable_index = -1;
	bool is_constructor = false;
	s32 definition_handle = -1; // Filled in by fill_in_pointers_to_member_function_definitions.
	
	FunctionType() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = FUNCTION_TYPE;
};

// Used for printing out the values of global variables. Not supported by the
// JSON format!
struct InitializerList : Node {
	std::vector<std::unique_ptr<Node>> children;
	std::string field_name;
	
	InitializerList() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = INITIALIZER_LIST;
};

struct PointerOrReference : Node {
	bool is_pointer = true;
	std::unique_ptr<Node> value_type;
	
	PointerOrReference() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = POINTER_OR_REFERENCE;
};

struct PointerToDataMember : Node {
	std::unique_ptr<Node> class_type;
	std::unique_ptr<Node> member_type;
	
	PointerToDataMember() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = POINTER_TO_DATA_MEMBER;
};

struct StructOrUnion : Node {
	bool is_struct = true;
	std::vector<std::unique_ptr<Node>> base_classes;
	std::vector<std::unique_ptr<Node>> fields;
	std::vector<std::unique_ptr<Node>> member_functions;
	
	StructOrUnion() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = STRUCT_OR_UNION;
};

enum class TypeNameSource {
	REFERENCE,
	CROSS_REFERENCE,
	ANONYMOUS_REFERENCE,
	ERROR
};

struct TypeName : Node {
	TypeNameSource source = TypeNameSource::ERROR;
	std::string type_name;
	u32 referenced_file_handle = (u32) -1;
	StabsTypeNumber referenced_stabs_type_number;
	
	TypeName() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = TYPE_NAME;
};

void remove_duplicate_enums(std::vector<std::unique_ptr<Node>>& ast_nodes);
void remove_duplicate_self_typedefs(std::vector<std::unique_ptr<Node>>& ast_nodes);

enum class CompareResultType {
	MATCHES_NO_SWAP,    // Both lhs and rhs are identical.
	MATCHES_CONFUSED,   // Both lhs and rhs are almost identical, and we don't which is better.
	MATCHES_FAVOUR_LHS, // Both lhs and rhs are almost identical, but lhs is better.
	MATCHES_FAVOUR_RHS, // Both lhs and rhs are almost identical, but rhs is better.
	DIFFERS,            // The two nodes differ substantially.
};

enum class CompareFailReason {
	NONE,
	DESCRIPTOR,
	STORAGE_CLASS,
	NAME,
	RELATIVE_OFFSET_BYTES,
	ABSOLUTE_OFFSET_BYTES,
	BITFIELD_OFFSET_BITS,
	SIZE_BITS,
	CONSTNESS,
	ARRAY_ELEMENT_COUNT,
	BUILTIN_CLASS,
	FUNCTION_RETURN_TYPE_HAS_VALUE,
	FUNCTION_PARAMAETER_COUNT,
	FUNCTION_PARAMETERS_HAS_VALUE,
	FUNCTION_MODIFIER,
	FUNCTION_IS_CONSTRUCTOR,
	ENUM_CONSTANTS,
	BASE_CLASS_COUNT,
	FIELDS_SIZE,
	MEMBER_FUNCTION_COUNT,
	VTABLE_GLOBAL,
	TYPE_NAME,
	VARIABLE_CLASS,
	VARIABLE_TYPE,
	VARIABLE_STORAGE,
	VARIABLE_BLOCK
};

struct CompareResult {
	CompareResult(CompareResultType type) : type(type), fail_reason(CompareFailReason::NONE) {}
	CompareResult(CompareFailReason reason) : type(CompareResultType::DIFFERS), fail_reason(reason) {}
	CompareResultType type;
	CompareFailReason fail_reason;
};

CompareResult compare_nodes(const Node& lhs, const Node& rhs, const SymbolTable& symbol_table, bool check_intrusive_fields);
const char* compare_fail_reason_to_string(CompareFailReason reason);
const char* node_type_to_string(const Node& node);
const char* storage_class_to_string(StorageClass storage_class);
const char* access_specifier_to_string(AccessSpecifier specifier);
const char* builtin_class_to_string(BuiltInClass bclass);
s32 builtin_class_size(BuiltInClass bclass);

enum TraversalOrder {
	PREORDER_TRAVERSAL,
	POSTORDER_TRAVERSAL
};

enum ExplorationMode {
	EXPLORE_CHILDREN,
	DONT_EXPLORE_CHILDREN
};

template <typename ThisNode, typename Callback>
void for_each_node(ThisNode& node, TraversalOrder order, Callback callback) {
	if(order == PREORDER_TRAVERSAL && callback(node) == DONT_EXPLORE_CHILDREN) {
		return;
	}
	switch(node.descriptor) {
		case ARRAY: {
			auto& array = node.template as<Array>();
			for_each_node(*array.element_type.get(), order, callback);
			break;
		}
		case BITFIELD: {
			auto& bitfield = node.template as<BitField>();
			for_each_node(*bitfield.underlying_type.get(), order, callback);
			break;
		}
		case BUILTIN: {
			break;
		}
		case DATA: {
			break;
		}
		case ENUM: {
			break;
		}
		case FUNCTION_TYPE: {
			auto& func = node.template as<FunctionType>();
			if(func.return_type.has_value()) {
				for_each_node(*func.return_type->get(), order, callback);
			}
			if(func.parameters.has_value()) {
				for(auto& child : *func.parameters) {
					for_each_node(*child.get(), order, callback);
				}
			}
			break;
		}
		case INITIALIZER_LIST: {
			auto& init_list = node.template as<InitializerList>();
			for(auto& child : init_list.children) {
				for_each_node(*child.get(), order, callback);
			}
			break;
		}
		case POINTER_OR_REFERENCE: {
			auto& pointer_or_reference = node.template as<PointerOrReference>();
			for_each_node(*pointer_or_reference.value_type.get(), order, callback);
			break;
		}
		case POINTER_TO_DATA_MEMBER: {
			auto& pointer = node.template as<PointerToDataMember>();
			for_each_node(*pointer.class_type.get(), order, callback);
			for_each_node(*pointer.member_type.get(), order, callback);
			break;
		}
		case STRUCT_OR_UNION: {
			auto& struct_or_union = node.template as<StructOrUnion>();
			for(auto& child : struct_or_union.base_classes) {
				for_each_node(*child.get(), order, callback);
			}
			for(auto& child : struct_or_union.fields) {
				for_each_node(*child.get(), order, callback);
			}
			for(auto& child : struct_or_union.member_functions) {
				for_each_node(*child.get(), order, callback);
			}
			break;
		}
		case TYPE_NAME: {
			break;
		}
	}
	if(order == POSTORDER_TRAVERSAL) {
		callback(node);
	}
}

}