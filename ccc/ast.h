#ifndef _CCC_AST_H
#define _CCC_AST_H

#include "util.h"
#include "symbols.h"
#include "registers.h"

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
	ARRAY,
	BITFIELD,
	BUILTIN,
	COMPOUND_STATEMENT,
	FUNCTION_DEFINITION,
	FUNCTION_TYPE,
	INLINE_ENUM,
	INLINE_STRUCT_OR_UNION,
	POINTER,
	REFERENCE,
	SOURCE_FILE,
	TYPE_NAME,
	VARIABLE
};

struct AddressRange {
	s32 low = -1;
	s32 high = -1;
	
	friend auto operator<=>(const AddressRange& lhs, const AddressRange& rhs) = default;
	bool valid() const { return low >= 0; }
};


struct Node {
	const NodeDescriptor descriptor;
	
	// If the name isn't populated for a given node, the name from the last
	// ancestor to have one should be used i.e. when processing the tree you
	// should pass the name down.
	std::string name;
	StorageClass storage_class = StorageClass::NONE;
	
	s32 relative_offset_bytes = -1; // Offset relative to start of last inline struct/union.
	s32 absolute_offset_bytes = -1; // Offset relative to outermost struct/union.
	s32 bitfield_offset_bits = -1; // Offset relative to the last byte (not the position of the underlying type!).
	s32 size_bits = -1;
	
	s32 order = -1; // Used to preserve the order of children of SourceFile.
	std::vector<s32> files; // List of files for which a given top-level type is present.
	bool conflict = false; // Are there multiple differing types with the same name?
	const ParsedSymbol* symbol = nullptr;
	const char* compare_fail_reason = "";
	s32 stabs_type_number = -1; // Only populated for the root.
	
	Node(NodeDescriptor d) : descriptor(d) {}
	Node(const Node& rhs) = default;
	virtual ~Node() {}
	
	template <typename SubType>
	SubType& as() { assert(descriptor == SubType::DESCRIPTOR); return *static_cast<SubType*>(this); }
	
	template <typename SubType>
	const SubType& as() const { assert(descriptor == SubType::DESCRIPTOR); return *static_cast<const SubType*>(this); }
	
	template <typename SubType>
	static std::pair<const SubType&, const SubType&> as(const Node& lhs, const Node& rhs) {
		assert(lhs.descriptor == SubType::DESCRIPTOR && rhs.descriptor == SubType::DESCRIPTOR);
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
	std::unique_ptr<Node> underlying_type;
	
	BitField() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = BITFIELD;
};

struct BuiltIn : Node {
	BuiltInClass bclass;
	
	BuiltIn() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = BUILTIN;
};

struct CompoundStatement : Node {
	std::vector<std::unique_ptr<Node>> children;
	
	CompoundStatement() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = COMPOUND_STATEMENT;
};

struct FunctionDefinition : Node {
	AddressRange address_range;
	std::unique_ptr<Node> type;
	std::unique_ptr<Node> body;
	
	FunctionDefinition() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = FUNCTION_DEFINITION;
};

struct FunctionType : Node {
	std::unique_ptr<Node> return_type;
	std::optional<std::vector<std::unique_ptr<Node>>> parameters;
	MemberFunctionModifier modifier = MemberFunctionModifier::NONE;
	bool is_constructor = false;
	
	FunctionType() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = FUNCTION_TYPE;
};

struct InlineEnum : Node {
	std::vector<std::pair<s32, std::string>> constants;
	
	InlineEnum() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = INLINE_ENUM;
};

struct BaseClass {
	StabsFieldVisibility visibility;
	s32 offset = -1;
	std::unique_ptr<Node> type;
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

struct SourceFile : Node {
	std::string full_path;
	u32 text_address = 0;
	std::vector<std::unique_ptr<ast::Node>> functions;
	std::vector<std::unique_ptr<ast::Node>> globals;
	std::vector<std::unique_ptr<ast::Node>> types;
	std::vector<ParsedSymbol> symbols;
	s32 next_order = 0; // Next order value to set.
	std::map<s32, s32> stabs_type_number_to_deduplicated_type_index;
	
	SourceFile() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = SOURCE_FILE;
	
	// Used for iterating all the functions, globals and types in the order the
	// functions and globals were defined in the source file.
	template <typename Callback>
	void in_order(Callback callback) const {
		s32 next_function = 0;
		s32 next_global = 0;
		s32 next_types = 0;
		for(s32 i = 0; i < next_order; i++) {
			if(next_function < functions.size() && functions[next_function]->order == i) {
				callback(*functions[i].get());
				next_function++;
				continue;
			}
			if(next_global < globals.size() && globals[next_global]->order == i) {
				callback(*globals[i].get());
				next_global++;
				continue;
			}
			if(next_global < globals.size() && globals[next_global]->order == i) {
				callback(*globals[i].get());
				next_global++;
				continue;
			}
			verify_not_reached("Source file AST node has bad ordering.");
		}
	}
};

struct TypeName : Node {
	std::string type_name;
	s32 referenced_file_index = -1;
	s32 referenced_stabs_type_number = -1;
	
	TypeName() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = TYPE_NAME;
};

enum class VariableClass {
	GLOBAL,
	LOCAL,
	PARAMETER
};

enum class VariableStorageLocation {
	BSS, // uninitialized global
	DATA, // initialized global
	REGISTER,
	STACK
};

struct VariableStorage {
	VariableStorageLocation location;
	s32 bss_or_data_address = -1;
	mips::RegisterClass register_class = mips::RegisterClass::GPR;
	s32 dbx_register_number = -1;
	s32 register_index_relative = -1;
	s32 stack_pointer_offset = -1;
	
	friend auto operator<=>(const VariableStorage& lhs, const VariableStorage& rhs) = default;
};

struct Variable : Node {
	VariableClass variable_class;
	VariableStorage storage;
	AddressRange block;
	std::unique_ptr<Node> type;
	
	Variable() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = VARIABLE;
};

struct StabsToAstState {
	s32 file_index;
	std::map<s32, const StabsType*>* stabs_types;
};
std::unique_ptr<Node> stabs_type_to_ast_no_throw(const StabsType& type, const StabsToAstState& state, s32 absolute_parent_offset_bytes, s32 depth, bool substitute_type_name);
std::unique_ptr<Node> stabs_symbol_to_ast(const ParsedSymbol& symbol, const StabsToAstState& state);
std::unique_ptr<Node> stabs_type_to_ast(const StabsType& type, const StabsToAstState& state, s32 absolute_parent_offset_bytes, s32 depth, bool substitute_type_name);
std::unique_ptr<Node> stabs_field_to_ast(const StabsField& field, const StabsToAstState& state, s32 absolute_parent_offset_bytes, s32 depth);
void remove_duplicate_enums(std::vector<std::unique_ptr<Node>>& ast_nodes);
void remove_duplicate_self_typedefs(std::vector<std::unique_ptr<Node>>& ast_nodes);
std::vector<std::unique_ptr<Node>> deduplicate_types(std::vector<std::unique_ptr<ast::SourceFile>>& source_files);
enum class CompareFailReason {
	DESCRIPTOR,
	STORAGE_CLASS,
	NAME,
	RELATIVE_OFFSET_BYTES,
	ABSOLUTE_OFFSET_BYTES,
	BITFIELD_OFFSET_BITS,
	SIZE_BITS,
	ARRAY_ELEMENT_COUNT,
	BUILTIN_CLASS,
	COMPOUND_STATEMENT_SIZE,
	FUNCTION_PARAMAETER_SIZE,
	FUNCTION_PARAMETERS_HAS_VALUE,
	FUNCTION_MODIFIER,
	FUNCTION_IS_CONSTRUCTOR,
	ENUM_CONSTANTS,
	BASE_CLASS_SIZE,
	BASE_CLASS_VISIBILITY,
	BASE_CLASS_OFFSET,
	FIELDS_SIZE,
	MEMBER_FUNCTION_SIZE,
	SOURCE_FILE_SIZE,
	TYPE_NAME,
	VARIABLE_CLASS,
	VARIABLE_TYPE,
	VARIABLE_STORAGE,
	VARIABLE_BLOCK
};
std::optional<CompareFailReason> compare_ast_nodes(const ast::Node& lhs, const ast::Node& rhs);
const char* compare_fail_reason_to_string(CompareFailReason reason);
const char* node_type_to_string(const Node& node);
const char* storage_class_to_string(StorageClass storage_class);

}

#endif
