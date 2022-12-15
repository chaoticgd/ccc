#ifndef _CCC_AST_H
#define _CCC_AST_H

#include "util.h"
#include "stabs.h"

namespace ccc::ast {

enum class StorageClass {
	NONE,
	TYPEDEF,
	EXTERN,
	STATIC,
	AUTO,
	REGISTER
};

enum NodeDescriptor {
	ARRAY, BITFIELD, FUNCTION, INLINE_ENUM, INLINE_STRUCT_OR_UNION, POINTER, REFERENCE, TYPE_NAME
};

struct Node {
	NodeDescriptor descriptor;
	StorageClass storage_class = StorageClass::NONE;
	
	// If the name isn't populated for a given node, the name from the last
	// ancestor to have one should be used i.e. when processing the tree you
	// should pass the name down.
	std::string name;
	
	s32 relative_offset_bytes = -1; // Offset relative to start of last inline struct/union.
	s32 absolute_offset_bytes = -1; // Offset relative to outermost struct/union.
	s32 bitfield_offset_bits = -1; // Offset relative to the last byte (not the position of the underlying type!).
	s32 size_bits = -1;
	bool is_constructor = false;
	
	const StabsSymbol* symbol = nullptr;
	const char* compare_fail_reason = nullptr;
	
	Node(NodeDescriptor d) : descriptor(d) {}
	Node(const Node& rhs) = default;
	virtual ~Node() {}
	
	template <typename SubType>
	SubType& as() { assert(descriptor == SubType::DESCRIPTOR); return *static_cast<SubType*>(this); }
	
	template <typename SubType>
	const SubType& as() const { assert(descriptor == SubType::DESCRIPTOR); return *static_cast<const SubType*>(this); }
};

struct BaseClass {
	s8 visibility;
	s32 offset = -1;
	std::string type_name;
};

struct Array : Node {
	std::unique_ptr<Node> element_type;
	s32 element_count = -1;
	
	Array() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = ARRAY;
};

struct BitField : Node {
	std::unique_ptr<Node> underlying_type;
	
	BitField() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = BITFIELD;
};

struct Function : Node {
	std::unique_ptr<Node> return_type;
	std::optional<std::vector<std::unique_ptr<Node>>> parameters;
	
	Function() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = FUNCTION;
};

struct InlineEnum : Node {
	std::vector<std::pair<s32, std::string>> constants;
	
	InlineEnum() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = INLINE_ENUM;
};

struct InlineStructOrUnion : Node {
	bool is_struct = true;
	std::vector<BaseClass> base_classes;
	std::vector<std::unique_ptr<Node>> fields;
	std::vector<std::unique_ptr<Node>> member_functions;
	
	InlineStructOrUnion() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = INLINE_STRUCT_OR_UNION;
};

struct Pointer : Node {
	std::unique_ptr<Node> value_type;
	
	Pointer() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = POINTER;
};

struct Reference : Node {
	std::unique_ptr<Node> value_type;
	
	Reference() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = REFERENCE;
};

struct TypeName : Node {
	std::string type_name;
	
	TypeName() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = TYPE_NAME;
};

std::set<std::pair<std::string, RangeClass>> symbols_to_builtins(const std::vector<StabsSymbol>& symbols);
std::vector<std::unique_ptr<ast::Node>> symbols_to_ast(const std::vector<StabsSymbol>& symbols, const std::map<s32, const StabsType*>& stabs_types);
bool is_data_type(const StabsSymbol& symbol);
bool is_builtin_type(const StabsSymbol& symbol);
std::unique_ptr<Node> stabs_symbol_to_ast(const StabsSymbol& symbol, const std::map<s32, const StabsType*>& stabs_types);
std::unique_ptr<Node> stabs_type_to_ast(const StabsType& type, const std::map<s32, const StabsType*>& stabs_types, s32 absolute_parent_offset_bytes, s32 depth, bool substitute_type_name);
std::unique_ptr<Node> stabs_field_to_ast(const StabsField& field, const std::map<s32, const StabsType*>& stabs_types, s32 absolute_parent_offset_bytes, s32 depth);
void remove_duplicate_enums(std::vector<std::unique_ptr<Node>>& ast_nodes);
std::vector<std::vector<std::unique_ptr<Node>>> deduplicate_ast(std::vector<std::pair<std::string, std::vector<std::unique_ptr<ast::Node>>>>& per_file_ast);
enum class CompareFailReason {
	DESCRIPTOR,
	STORAGE_CLASS,
	NAME,
	RELATIVE_OFFSET_BYTES,
	ABSOLUTE_OFFSET_BYTES,
	BITFIELD_OFFSET_BITS,
	SIZE_BITS,
	ARRAY_ELEMENT_COUNT,
	FUNCTION_PARAMAETER_SIZE,
	FUNCTION_PARAMETERS_HAS_VALUE,
	ENUM_CONSTANTS,
	BASE_CLASS_SIZE,
	BASE_CLASS_VISIBILITY,
	BASE_CLASS_OFFSET,
	BASE_CLASS_TYPE_NAME,
	FIELDS_SIZE,
	MEMBER_FUNCTION_SIZE,
	TYPE_NAME
};
static std::optional<CompareFailReason> compare_ast_nodes(const ast::Node& lhs, const ast::Node& rhs);
static const char* compare_fail_reason_to_string(CompareFailReason reason);

}

#endif
