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
	SLASH = '/',
	MEMBER = '@'
};

struct StabsBaseClass;
struct StabsField;
struct StabsMemberFunction;

// e.g. for "123=*456" 123 would be the type_number, the type descriptor would
// be of type POINTER and reference_or_pointer.value_type would point to a type
// with type_number = 456 and has_body = false.
struct StabsType {
	// The name field is only populated for root types and cross references.
	std::optional<std::string> name;
	bool anonymous = false;
	s32 type_number = -1;
	bool has_body = false;
	// If !has_body, everything below isn't filled in.
	StabsTypeDescriptor descriptor;
	// Tagged "union" based on the value of the type descriptor.
	struct {
		std::unique_ptr<StabsType> type;
	} type_reference;
	struct {
		std::unique_ptr<StabsType> index_type;
		std::unique_ptr<StabsType> element_type;
	} array_type;
	struct {
		std::vector<std::pair<s32, std::string>> fields;
	} enum_type;
	struct {
		std::unique_ptr<StabsType> type;
	} function_type;
	struct {
		std::unique_ptr<StabsType> type;
		s64 low;
		s64 high;
	} range_type;
	struct {
		s64 size;
		std::vector<StabsBaseClass> base_classes;
		std::vector<StabsField> fields;
		std::vector<StabsMemberFunction> member_functions;
	} struct_or_union;
	struct {
		char type;
		std::string identifier;
	} cross_reference;
	struct {
		std::unique_ptr<StabsType> return_type;
		std::unique_ptr<StabsType> class_type;
		std::vector<StabsType> parameter_types;
	} method;
	struct {
		std::unique_ptr<StabsType> value_type;
	} reference_or_pointer;
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
	StabsType type;
};

struct StabsField {
	std::string name;
	StabsFieldVisibility visibility = StabsFieldVisibility::NONE;
	StabsType type;
	bool is_static = false;
	s32 offset_bits = 0;
	s32 size_bits = 0;
	std::string type_name;
};

struct StabsMemberFunctionField {
	StabsType type;
	StabsFieldVisibility visibility;
	bool is_const;
	bool is_volatile;
};

struct StabsMemberFunction {
	std::string name;
	std::vector<StabsMemberFunctionField> fields;
};

struct StabsSymbol {
	std::string raw;
	std::string name;
	StabsSymbolDescriptor descriptor;
	StabsType type;
	Symbol mdebug_symbol;
};

std::vector<StabsSymbol> parse_stabs_symbols(const std::vector<Symbol>& input);
StabsSymbol parse_stabs_symbol(const char* input);
void print_stabs_type(const StabsType& type);
std::map<s32, const StabsType*> enumerate_numbered_types(const std::vector<StabsSymbol>& symbols);

}

#endif
