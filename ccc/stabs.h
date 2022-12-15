#ifndef _CCC_STABS_H
#define _CCC_STABS_H

#include "util.h"
#include "mdebug.h"

namespace ccc {

enum class StabsSymbolDescriptor : s8 {
	LOCAL_VARIABLE = '\0',
	A = 'a',
	LOCAL_FUNCTION = 'f',
	GLOBAL_FUNCTION = 'F',
	GLOBAL_VARIABLE = 'G',
	REGISTER_PARAMETER = 'P',
	VALUE_PARAMETER = 'p',
	REGISTER_VARIABLE = 'r',
	STATIC_GLOBAL_VARIABLE = 's',
	TYPE_NAME = 't',
	ENUM_STRUCT_OR_TYPE_TAG = 'T',
	STATIC_LOCAL_VARIABLE = 'V'
};

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
	BUILT_IN = '-'
};

enum class RangeClass {
	UNSIGNED_8, SIGNED_8,
	UNSIGNED_16, SIGNED_16,
	UNSIGNED_32, SIGNED_32, FLOAT_32,
	UNSIGNED_64, SIGNED_64, FLOAT_64,
	UNSIGNED_128, SIGNED_128,
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

enum class StabsFieldVisibility : s8 {
	NONE = '\0',
	PRIVATE = '0',
	PROTECTED = '1',
	PUBLIC = '2',
	IGNORE = '9'
};

struct StabsBaseClass {
	s8 visibility;
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

struct StabsMemberFunctionOverload {
	std::unique_ptr<StabsType> type;
	StabsFieldVisibility visibility;
	bool is_const;
	bool is_volatile;
};

struct StabsMemberFunctionSet {
	std::string name;
	std::vector<StabsMemberFunctionOverload> overloads;
};

struct StabsSymbol {
	std::string raw;
	std::string name;
	StabsSymbolDescriptor descriptor;
	std::unique_ptr<StabsType> type;
	Symbol mdebug_symbol;
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
	s64 high_maybe_wrong = 0;
	RangeClass range_class;
	
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
			for(const StabsMemberFunctionOverload& member_function : member_function_set.overloads) {
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
	char type;
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
	static const constexpr StabsTypeDescriptor DESCRIPTOR = StabsTypeDescriptor::BUILT_IN;
};

std::vector<StabsSymbol> parse_stabs_symbols(const std::vector<Symbol>& input, SourceLanguage detected_language);
StabsSymbol parse_stabs_symbol(const char* input);
std::map<s32, const StabsType*> enumerate_numbered_types(const std::vector<StabsSymbol>& symbols);

}

#endif
