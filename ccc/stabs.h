#ifndef _CCC_STABS_H
#define _CCC_STABS_H

#include "util.h"
#include "mdebug.h"

namespace ccc {

static const char* ERR_END_OF_SYMBOL =
	"Unexpected end of input while parsing symbol.";

enum class StabsTypeDescriptor : s8 {
	TYPE_REFERENCE = '\0',
	ARRAY = 'a',
	ENUM = 'e',
	FUNCTION = 'f',
	RANGE = 'r',
	STRUCT = 's',
	UNION = 'u',
	CROSS_REFERENCE = 'x',
	METHOD = '#',
	REFERENCE = '&',
	POINTER = '*',
	TYPE_ATTRIBUTE = '@',
	BUILTIN = '-'
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

struct StabsBaseClass;
struct StabsField;
struct StabsMemberFunctionSet;

// Fields to be filled in before the per-descriptor code that actually allocates
// the stab runs.
struct StabsTypeInfo {
	bool anonymous = false;
	s32 type_number = -1;
	bool has_body = false;
};

// e.g. for "123=*456" 123 would be the type_number, the type descriptor would
// be of type POINTER and StabsPointerType::value_type would point to a type
// with type_number = 456.
struct StabsType : StabsTypeInfo {
	// The name field is only populated for root types and cross references.
	std::optional<std::string> name;
	bool is_typedef = false;
	bool is_root = false;
	// If !has_body, the descriptor isn't filled in.
	StabsTypeDescriptor descriptor;
	
	StabsType(const StabsTypeInfo& i, StabsTypeDescriptor d) : StabsTypeInfo(i), descriptor(d) {}
	StabsType(const StabsTypeInfo& i) : StabsTypeInfo(i) {}
	virtual ~StabsType() {}
	
	template <typename SubType>
	SubType& as() { assert(descriptor == SubType::DESCRIPTOR); return *static_cast<SubType*>(this); }
	
	template <typename SubType>
	const SubType& as() const { assert(descriptor == SubType::DESCRIPTOR); return *static_cast<const SubType*>(this); }
	
	virtual void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const {
		if(!anonymous && has_body) {
			output.emplace(type_number, this);
		}
	}
};

enum class StabsFieldVisibility : u8 {
	NONE = '\0',
	PRIVATE = '0',
	PROTECTED = '1',
	PUBLIC = '2',
	PUBLIC_OPTIMIZED_OUT = '9'
};

struct StabsBaseClass {
	StabsFieldVisibility visibility;
	s32 offset;
	std::unique_ptr<StabsType> type;
};

struct StabsField {
	std::string name;
	StabsFieldVisibility visibility = StabsFieldVisibility::NONE;
	std::unique_ptr<StabsType> type;
	bool is_static = false;
	s32 offset_bits = 0;
	s32 size_bits = 0;
	std::string type_name;
};

enum class MemberFunctionModifier {
	NONE,
	STATIC,
	VIRTUAL
};

struct StabsMemberFunction {
	std::unique_ptr<StabsType> type;
	StabsFieldVisibility visibility;
	bool is_const;
	bool is_volatile;
	MemberFunctionModifier modifier;
	s32 vtable_index = -1;
};

struct StabsMemberFunctionSet {
	std::string name;
	std::vector<StabsMemberFunction> overloads;
};

struct StabsTypeReferenceType : StabsType {
	std::unique_ptr<StabsType> type;
	
	StabsTypeReferenceType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::TYPE_REFERENCE;
	
	void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const override {
		StabsType::enumerate_numbered_types(output);
		type->enumerate_numbered_types(output);
	}
};

struct StabsArrayType : StabsType {
	std::unique_ptr<StabsType> index_type;
	std::unique_ptr<StabsType> element_type;
	
	StabsArrayType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::ARRAY;
	
	void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const override {
		StabsType::enumerate_numbered_types(output);
		index_type->enumerate_numbered_types(output);
		element_type->enumerate_numbered_types(output);
	}
};

struct StabsEnumType : StabsType {
	std::vector<std::pair<s32, std::string>> fields;
	
	StabsEnumType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::ENUM;
};

struct StabsFunctionType : StabsType {
	std::unique_ptr<StabsType> return_type;
	
	StabsFunctionType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::FUNCTION;
	
	void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const override {
		StabsType::enumerate_numbered_types(output);
		return_type->enumerate_numbered_types(output);
	}
};

struct StabsRangeType : StabsType {
	std::unique_ptr<StabsType> type;
	s64 low_maybe_wrong = 0;
	s64 high_maybe_wrong = -1; // For some zero-length arrays gcc writes out a malformed range, in which case these defaults are used.
	BuiltInClass range_class;
	
	StabsRangeType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::RANGE;
	
	void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const override {
		StabsType::enumerate_numbered_types(output);
		type->enumerate_numbered_types(output);
	}
};

struct StabsStructOrUnionType : StabsType {
	s64 size;
	std::vector<StabsBaseClass> base_classes;
	std::vector<StabsField> fields;
	std::vector<StabsMemberFunctionSet> member_functions;
	
	StabsStructOrUnionType(const StabsTypeInfo& i, StabsTypeDescriptor d) : StabsType(i, d) {}
	
	void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const override {
		StabsType::enumerate_numbered_types(output);
		for(const StabsBaseClass& base_class : base_classes) {
			base_class.type->enumerate_numbered_types(output);
		}
		for(const StabsField& field : fields) {
			field.type->enumerate_numbered_types(output);
		}
		for(const StabsMemberFunctionSet& member_function_set : member_functions) {
			for(const StabsMemberFunction& member_function : member_function_set.overloads) {
				member_function.type->enumerate_numbered_types(output);
			}
		}
	}
};

struct StabsStructType : StabsStructOrUnionType {
	StabsStructType(const StabsTypeInfo& i) : StabsStructOrUnionType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::STRUCT;
};

struct StabsUnionType : StabsStructOrUnionType {
	StabsUnionType(const StabsTypeInfo& i) : StabsStructOrUnionType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::UNION;
};


struct StabsCrossReferenceType : StabsType {
	enum {
		ENUM,
		STRUCT,
		UNION
	} type;
	std::string identifier;
	
	StabsCrossReferenceType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::CROSS_REFERENCE;
};

struct StabsMethodType : StabsType {
	std::unique_ptr<StabsType> return_type;
	std::optional<std::unique_ptr<StabsType>> class_type;
	std::vector<std::unique_ptr<StabsType>> parameter_types;
	
	StabsMethodType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::METHOD;
	
	void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const override {
		StabsType::enumerate_numbered_types(output);
		return_type->enumerate_numbered_types(output);
		if(class_type.has_value()) {
			(*class_type)->enumerate_numbered_types(output);
		}
		for(const std::unique_ptr<StabsType>& parameter_type : parameter_types) {
			parameter_type->enumerate_numbered_types(output);
		}
	}
};

struct StabsReferenceType : StabsType {
	std::unique_ptr<StabsType> value_type;
	
	StabsReferenceType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::REFERENCE;
	
	void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const override {
		StabsType::enumerate_numbered_types(output);
		value_type->enumerate_numbered_types(output);
	}
};

struct StabsPointerType : StabsType {
	std::unique_ptr<StabsType> value_type;
	
	StabsPointerType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::POINTER;
	
	void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const override {
		StabsType::enumerate_numbered_types(output);
		value_type->enumerate_numbered_types(output);
	}
};

struct StabsSizeTypeAttributeType : StabsType {
	s64 size_bits;
	std::unique_ptr<StabsType> type;
	
	StabsSizeTypeAttributeType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::TYPE_ATTRIBUTE;
	
	void enumerate_numbered_types(std::map<s32, const StabsType*>& output) const override {
		StabsType::enumerate_numbered_types(output);
		type->enumerate_numbered_types(output);
	}
};

struct StabsBuiltInType : StabsType {
	s64 type_id;
	
	StabsBuiltInType(const StabsTypeInfo& i) : StabsType(i, DESCRIPTOR) {}
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::BUILTIN;
};

std::unique_ptr<StabsType> parse_stabs_type(const char*& input);
s8 eat_s8(const char*& input);
s64 eat_s64_literal(const char*& input);
std::string eat_stabs_identifier(const char*& input);
std::string eat_dodgy_stabs_identifier(const char*& input);
void expect_s8(const char*& input, s8 expected, const char* subject);
const char* builtin_class_to_string(BuiltInClass bclass);
s32 builtin_class_size(BuiltInClass bclass);
const char* stabs_field_visibility_to_string(StabsFieldVisibility visibility);

}

#endif
