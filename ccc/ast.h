#ifndef _CCC_AST_H
#define _CCC_AST_H

#include "util.h"
#include "symbols.h"
#include "registers.h"

namespace ccc::ast {

enum StorageClass {
	SC_NONE = 0,
	SC_TYPEDEF = 1,
	SC_EXTERN = 2,
	SC_STATIC = 3,
	SC_AUTO = 4,
	SC_REGISTER = 5
};

enum NodeDescriptor {
	ARRAY = 0,
	BITFIELD = 1,
	BUILTIN = 2,
	FUNCTION_DEFINITION = 3,
	FUNCTION_TYPE = 4,
	INLINE_ENUM = 5,
	INLINE_STRUCT_OR_UNION = 6,
	POINTER = 7,
	POINTER_TO_DATA_MEMBER = 8,
	REFERENCE = 9,
	SOURCE_FILE = 10,
	TYPE_NAME = 11,
	VARIABLE = 12
};

struct AddressRange {
	s32 low = -1;
	s32 high = -1;
	
	friend auto operator<=>(const AddressRange& lhs, const AddressRange& rhs) = default;
	bool valid() const { return low >= 0; }
};

enum AccessSpecifier {
	AS_PUBLIC = 0,
	AS_PROTECTED = 1,
	AS_PRIVATE = 2
};

struct Node {
	u16 descriptor : 4;
	u16 is_const : 1 = false;
	u16 is_volatile : 1 = false;
	u16 conflict : 1 = false; // Are there multiple differing types with the same name?
	u16 storage_class : 4 = SC_NONE;
	u16 access_specifier : 2 = AS_PUBLIC;
	
	// If the name isn't populated for a given node, the name from the last
	// ancestor to have one should be used i.e. when processing the tree you
	// should pass the name down.
	std::string name;
	
	std::vector<s32> files; // List of files for which a given top-level type is present.
	const ParsedSymbol* symbol = nullptr;
	const char* compare_fail_reason = "";
	s64 stabs_type_number = -1;
	
	s32 relative_offset_bytes = -1; // Offset relative to start of last inline struct/union.
	s32 absolute_offset_bytes = -1; // Offset relative to outermost struct/union.
	s32 size_bits = -1;
	
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
	s32 bitfield_offset_bits = -1; // Offset relative to the last byte (not the position of the underlying type!).
	std::unique_ptr<Node> underlying_type;
	
	BitField() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = BITFIELD;
};

struct BuiltIn : Node {
	BuiltInClass bclass;
	
	BuiltIn() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = BUILTIN;
};

struct Variable;

struct LineNumberPair {
	s32 address;
	s32 line_number;
};

struct SubSourceFile {
	s32 address;
	std::string relative_path;
};

struct FunctionDefinition : Node {
	AddressRange address_range;
	std::string relative_path;
	std::unique_ptr<Node> type;
	std::vector<std::unique_ptr<ast::Variable>> locals;
	std::vector<LineNumberPair> line_numbers;
	std::vector<SubSourceFile> sub_source_files;
	
	FunctionDefinition() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = FUNCTION_DEFINITION;
};

struct FunctionType : Node {
	std::optional<std::unique_ptr<Node>> return_type;
	std::optional<std::vector<std::unique_ptr<Node>>> parameters;
	MemberFunctionModifier modifier = MemberFunctionModifier::NONE;
	s32 vtable_index = -1;
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

struct PointerToDataMember : Node {
	std::unique_ptr<Node> class_type;
	std::unique_ptr<Node> member_type;
	
	PointerToDataMember() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = POINTER_TO_DATA_MEMBER;
};

struct Reference : Node {
	std::unique_ptr<Node> value_type;
	
	Reference() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = REFERENCE;
};

struct SourceFile : Node {
	std::string full_path;
	bool is_windows_path = false;
	std::string relative_path = "";
	u32 text_address = 0;
	std::vector<std::unique_ptr<ast::Node>> data_types;
	std::vector<std::unique_ptr<ast::Node>> functions;
	std::vector<std::unique_ptr<ast::Node>> globals;
	std::vector<ParsedSymbol> symbols;
	std::map<s64, s32> stabs_type_number_to_deduplicated_type_index;
	
	SourceFile() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = SOURCE_FILE;
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
	s32 referenced_file_index = -1;
	s64 referenced_stabs_type_number = -1;
	
	TypeName() : Node(DESCRIPTOR) {}
	static const constexpr NodeDescriptor DESCRIPTOR = TYPE_NAME;
};

enum class VariableClass {
	GLOBAL,
	LOCAL,
	PARAMETER
};

enum class VariableStorageType {
	GLOBAL,
	REGISTER,
	STACK
};

enum class GlobalVariableLocation {
	NIL,
	DATA,
	BSS,
	ABS,
	SDATA,
	SBSS,
	RDATA,
	COMMON,
	SCOMMON
};

struct VariableStorage {
	VariableStorageType type = VariableStorageType::GLOBAL;
	GlobalVariableLocation global_location = GlobalVariableLocation::NIL;
	s32 global_address = -1;
	mips::RegisterClass register_class = mips::RegisterClass::GPR;
	s32 dbx_register_number = -1;
	s32 register_index_relative = -1;
	bool is_by_reference = false;
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

struct TypeDeduplicatorOMatic {
	std::vector<std::unique_ptr<Node>> flat_nodes;
	std::vector<std::vector<s32>> deduplicated_nodes;
	std::map<std::string, size_t> name_to_deduplicated_index;
	
	void process_file(ast::SourceFile& file, s32 file_index);
	std::vector<std::unique_ptr<Node>> finish();
};

struct StabsToAstState {
	s32 file_index;
	std::map<s64, const StabsType*>* stabs_types;
};
std::unique_ptr<Node> stabs_type_to_ast_no_throw(const StabsType& type, const StabsToAstState& state, s32 absolute_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute);
std::unique_ptr<Node> stabs_symbol_to_ast(const ParsedSymbol& symbol, const StabsToAstState& state);
std::unique_ptr<Node> stabs_type_to_ast(const StabsType& type, const StabsToAstState& state, s32 absolute_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute);
std::unique_ptr<Node> stabs_field_to_ast(const StabsField& field, const StabsToAstState& state, s32 absolute_parent_offset_bytes, s32 depth);
void remove_duplicate_enums(std::vector<std::unique_ptr<Node>>& ast_nodes);
void remove_duplicate_self_typedefs(std::vector<std::unique_ptr<Node>>& ast_nodes);
enum class CompareFailReason {
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
	COMPOUND_STATEMENT_SIZE,
	FUNCTION_RETURN_TYPE_HAS_VALUE,
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
	VTABLE_GLOBAL,
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
const char* global_variable_location_to_string(GlobalVariableLocation location);
const char* access_specifier_to_string(AccessSpecifier specifier);
AccessSpecifier stabs_field_visibility_to_access_specifier(StabsFieldVisibility visibility);

}

#endif
