#include "stabs.h"

#include <algorithm>
#include <string>

namespace ccc {

#define STABS_DEBUG(...) //__VA_ARGS__
#define STABS_DEBUG_PRINTF(...) STABS_DEBUG(printf(__VA_ARGS__);)

// parse_stabs_type
static std::vector<StabsField> parse_field_list(const char*& input);
static std::vector<StabsMemberFunctionSet> parse_member_functions(const char*& input);
static BuiltInClass classify_range(const std::string& low, const std::string& high);
// eat_s8
// eat_s64_literal
// eat_stabs_identifier
// eat_dodgy_stabs_identifier
// expect_s8
static void print_field(const StabsField& field);

std::unique_ptr<StabsType> parse_stabs_type(const char*& input) {
	StabsTypeInfo info;
	verify(*input != '\0', ERR_END_OF_SYMBOL);
	if(*input >= '0' && *input <= '9') {
		info.anonymous = false;
		info.type_number = eat_s64_literal(input);
		if(*input != '=') {
			info.has_body = false;
			return std::make_unique<StabsType>(info);
		}
		input++;
	} else {
		info.anonymous = true;
	}
	info.has_body = true;
	verify(*input != '\0', ERR_END_OF_SYMBOL);
	
	StabsTypeDescriptor descriptor;
	if(*input >= '0' && *input <= '9') {
		descriptor = StabsTypeDescriptor::TYPE_REFERENCE;
	} else {
		descriptor = (StabsTypeDescriptor) eat_s8(input);
	}
	
	std::unique_ptr<StabsType> type;
	
	switch(descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE: { // 0..9
			auto type_reference = std::make_unique<StabsTypeReferenceType>(info);
			type_reference->type = parse_stabs_type(input);
			type = std::move(type_reference);
			break;
		}
		case StabsTypeDescriptor::ARRAY: { // a
			auto array = std::make_unique<StabsArrayType>(info);
			array->index_type = parse_stabs_type(input);
			array->element_type = parse_stabs_type(input);
			type = std::move(array);
			break;
		}
		case StabsTypeDescriptor::ENUM: { // e
			auto enum_type = std::make_unique<StabsEnumType>(info);
			STABS_DEBUG_PRINTF("enum {\n");
			while(*input != ';') {
				std::string name = eat_dodgy_stabs_identifier(input);
				expect_s8(input, ':', "identifier");
				s64 value = eat_s64_literal(input);
				enum_type->fields.emplace_back(value, name);
				verify(eat_s8(input) == ',',
					"Expecting ',' while parsing enum, got '%c' (%02hhx)",
					*input, *input);
			}
			input++;
			STABS_DEBUG_PRINTF("}\n");
			type = std::move(enum_type);
			break;
		}
		case StabsTypeDescriptor::FUNCTION: { // f
			auto function = std::make_unique<StabsFunctionType>(info);
			function->return_type = parse_stabs_type(input);
			type = std::move(function);
			break;
		}
		case StabsTypeDescriptor::RANGE: { // r
			auto range = std::make_unique<StabsRangeType>(info);
			range->type = parse_stabs_type(input);
			expect_s8(input, ';', "range type descriptor");
			std::string low = eat_dodgy_stabs_identifier(input);
			expect_s8(input, ';', "low range value");
			std::string high = eat_dodgy_stabs_identifier(input);
			expect_s8(input, ';', "high range value");
			try {
				range->low_maybe_wrong = std::stoi(low);
				range->high_maybe_wrong = std::stoi(high);
			} catch(std::out_of_range&) { /* this case doesn't matter */ }
			range->range_class = classify_range(low, high);
			type = std::move(range);
			break;
		}
		case StabsTypeDescriptor::STRUCT: { // s
			auto struct_type = std::make_unique<StabsStructType>(info);
			STABS_DEBUG_PRINTF("struct {\n");
			struct_type->size = eat_s64_literal(input);
			if(*input == '!') {
				input++;
				s64 base_class_count = eat_s64_literal(input);
				expect_s8(input, ',', "base class section");
				for(s64 i = 0; i < base_class_count; i++) {
					StabsBaseClass base_class;
					eat_s8(input);
					base_class.visibility = eat_s8(input);
					base_class.offset = eat_s64_literal(input);
					expect_s8(input, ',', "base class section");
					base_class.type = parse_stabs_type(input);
					expect_s8(input, ';', "base class section");
					struct_type->base_classes.emplace_back(std::move(base_class));
				}
			}
			struct_type->fields = parse_field_list(input);
			struct_type->member_functions = parse_member_functions(input);
			STABS_DEBUG_PRINTF("}\n");
			type = std::move(struct_type);
			break;
		}
		case StabsTypeDescriptor::UNION: { // u
			auto union_type = std::make_unique<StabsUnionType>(info);
			STABS_DEBUG_PRINTF("union {\n");
			union_type->size = eat_s64_literal(input);
			union_type->fields = parse_field_list(input);
			union_type->member_functions = parse_member_functions(input);
			STABS_DEBUG_PRINTF("}\n");
			type = std::move(union_type);
			break;
		}
		case StabsTypeDescriptor::CROSS_REFERENCE: { // x
			auto cross_reference = std::make_unique<StabsCrossReferenceType>(info);
			cross_reference->type = eat_s8(input);
			switch(cross_reference->type) {
				case 's': // struct
				case 'u': // union
				case 'e': // enum
					break;
				default:
					verify_not_reached("Invalid cross reference type '%c'.",
						cross_reference->type);
			}
			cross_reference->identifier = eat_dodgy_stabs_identifier(input);
			cross_reference->name = cross_reference->identifier;
			expect_s8(input, ':', "cross reference");
			type = std::move(cross_reference);
			break;
		}
		case StabsTypeDescriptor::METHOD: { // #
			auto method = std::make_unique<StabsMethodType>(info);
			if(*input == '#') {
				input++;
				method->return_type = parse_stabs_type(input);
				expect_s8(input, ';', "method");
			} else {
				method->class_type = parse_stabs_type(input);
				expect_s8(input, ',', "method");
				method->return_type = parse_stabs_type(input);
				while(*input != '\0') {
					if(*input == ';') {
						input++;
						break;
					}
					expect_s8(input, ',', "method");
					method->parameter_types.emplace_back(parse_stabs_type(input));
				}
			}
			type = std::move(method);
			break;
		}
		case StabsTypeDescriptor::REFERENCE: { // &
			auto reference = std::make_unique<StabsReferenceType>(info);
			reference->value_type = parse_stabs_type(input);
			type = std::move(reference);
			break;
		}
		case StabsTypeDescriptor::POINTER: { // *
			auto pointer = std::make_unique<StabsPointerType>(info);
			pointer->value_type = parse_stabs_type(input);
			type = std::move(pointer);
			break;
		}
		case StabsTypeDescriptor::TYPE_ATTRIBUTE: { // @
			auto type_attribute = std::make_unique<StabsSizeTypeAttributeType>(info);
			verify(*input == 's', "Weird value following '@' type descriptor. Please submit a bug report!");
			input++;
			type_attribute->size_bits = eat_s64_literal(input);
			expect_s8(input, ';', "type attribute");
			type_attribute->type = parse_stabs_type(input);
			type = std::move(type_attribute);
			break;
		}
		case StabsTypeDescriptor::BUILTIN: { // -
			auto built_in = std::make_unique<StabsBuiltInType>(info);
			built_in->type_id = eat_s64_literal(input);
			type = std::move(built_in);
			break;
		}
		default: {
			verify_not_reached("Invalid type descriptor '%c' (%02x). Please file a bug report!",
				(u32) descriptor, (u32) descriptor);
		}
	}
	return type;
}

static std::vector<StabsField> parse_field_list(const char*& input) {
	std::vector<StabsField> fields;
	while(*input != '\0') {
		if(*input == ';') {
			input++;
			break;
		}
		
		const char* before_field = input;
		StabsField field;
		field.name = eat_dodgy_stabs_identifier(input);
		expect_s8(input, ':', "identifier");
		if(*input == '/') {
			input++;
			field.visibility = (StabsFieldVisibility) eat_s8(input);
			switch(field.visibility) {
				case StabsFieldVisibility::NONE:
				case StabsFieldVisibility::PRIVATE:
				case StabsFieldVisibility::PROTECTED:
				case StabsFieldVisibility::PUBLIC:
				case StabsFieldVisibility::IGNORE:
					break;
				default:
					verify_not_reached("Invalid field visibility.");
			}
		}
		if(*input == ':') {
			input = before_field;
			break;
		}
		field.type = parse_stabs_type(input);
		if(field.name.size() >= 1 && field.name[0] == '$') {
			// Not sure.
			expect_s8(input, ',', "field type");
			field.offset_bits = eat_s64_literal(input);
			expect_s8(input, ';', "field offset");
		} else if(*input == ':') {
			input++;
			field.is_static = true;
			field.type_name = eat_dodgy_stabs_identifier(input);
			expect_s8(input, ';', "identifier");
		} else if(*input == ',') {
			input++;
			field.offset_bits = eat_s64_literal(input);
			expect_s8(input, ',', "field offset");
			field.size_bits = eat_s64_literal(input);
			expect_s8(input, ';', "field size");
		} else {
			verify_not_reached("Expected ':' or ',', got '%c' (%hhx).", *input, *input);
		}

		STABS_DEBUG(print_field(field);)

		fields.emplace_back(std::move(field));
	}
	return fields;
}

static std::vector<StabsMemberFunctionSet> parse_member_functions(const char*& input) {
	if(*input == ',') {
		return {};
	}
	
	std::vector<StabsMemberFunctionSet> member_functions;
	while(*input != '\0') {
		if(*input == ';') {
			input++;
			break;
		}
		const char* before = input;
		StabsMemberFunctionSet member_function_set;
		member_function_set.name = eat_stabs_identifier(input);
		expect_s8(input, ':', "member function");
		expect_s8(input, ':', "member function");
		while(*input != '\0') {
			if(*input == ';') {
				input++;
				break;
			}
			
			StabsMemberFunctionOverload function;
			function.type = parse_stabs_type(input);
			
			expect_s8(input, ':', "member function");
			eat_dodgy_stabs_identifier(input);
			expect_s8(input, ';', "member function");
			function.visibility = (StabsFieldVisibility) eat_s8(input);
			switch(function.visibility) {
				case StabsFieldVisibility::PRIVATE:
				case StabsFieldVisibility::PROTECTED:
				case StabsFieldVisibility::PUBLIC:
				case StabsFieldVisibility::IGNORE:
					break;
				default:
					verify_not_reached("Invalid visibility for member function.");
			}
			switch(eat_s8(input)) {
				case 'A':
					function.is_const = false;
					function.is_volatile = false;
					break;
				case 'B':
					function.is_const = true;
					function.is_volatile = false;
					break;
				case 'C':
					function.is_const = false;
					function.is_volatile = true;
					break;
				case 'D':
					function.is_const = true;
					function.is_volatile = true;
					break;
				case '?':
				case '.':
					break;
				default:
					verify_not_reached("Invalid member function modifiers.");
			}
			switch(eat_s8(input)) {
				case '.': // normal member function
					function.modifier = MemberFunctionModifier::NORMAL;
					break;
				case '?': // static member function
					function.modifier = MemberFunctionModifier::STATIC;
					break;
				case '*': // virtual member function
					eat_s64_literal(input);
					expect_s8(input, ';', "virtual member function");
					parse_stabs_type(input);
					expect_s8(input, ';', "virtual member function");
					function.modifier = MemberFunctionModifier::VIRTUAL;
					break;
				default:
					verify_not_reached("Invalid member function type.");
			}
			member_function_set.overloads.emplace_back(std::move(function));
		}
		STABS_DEBUG_PRINTF("member func: %s\n", member_function_set.name.c_str());
		member_functions.emplace_back(std::move(member_function_set));
	}
	return member_functions;
}

static BuiltInClass classify_range(const std::string& low, const std::string& high) {
	// Handle some special cases and values that are too large to easily store
	// in a 64-bit integer.
	static const struct { const char* low; const char* high; BuiltInClass classification; } strings[] = {
		{"4", "0", BuiltInClass::FLOAT_32},
		{"000000000000000000000000", "001777777777777777777777", BuiltInClass::UNSIGNED_64},
		{"00000000000000000000000000000000000000000000", "00000000000000000000001777777777777777777777", BuiltInClass::UNSIGNED_64},
		{"001000000000000000000000", "000777777777777777777777", BuiltInClass::SIGNED_64},
		{"00000000000000000000001000000000000000000000", "00000000000000000000000777777777777777777777", BuiltInClass::SIGNED_64},
		{"8", "0", BuiltInClass::FLOAT_64},
		{"00000000000000000000000000000000000000000000", "03777777777777777777777777777777777777777777", BuiltInClass::UNSIGNED_128},
		{"02000000000000000000000000000000000000000000", "01777777777777777777777777777777777777777777", BuiltInClass::SIGNED_128},
		{"000000000000000000000000", "0377777777777777777777777777777777", BuiltInClass::UNQUALIFIED_128},
		{"16", "0", BuiltInClass::FLOAT_128}
	};
	
	for(const auto& range : strings) {
		if(strcmp(range.low, low.c_str()) == 0 && strcmp(range.high, high.c_str()) == 0) {
			return range.classification;
		}
	}
	
	// For smaller values we actually parse the bounds as integers.
	s64 low_value = 0;
	s64 high_value = 0;
	try {
		low_value = std::stol(low, nullptr, low[0] == '0' ? 8 : 10);
		high_value = std::stol(high, nullptr, high[0] == '0' ? 8 : 10);
	} catch(std::out_of_range&) {
		return BuiltInClass::UNKNOWN_PROBABLY_ARRAY;
	}
	
	static const struct { s64 low; s64 high; BuiltInClass classification; } integers[] = {
		{0, 255, BuiltInClass::UNSIGNED_8},
		{-128, 127, BuiltInClass::SIGNED_8},
		{0, 127, BuiltInClass::UNQUALIFIED_8},
		{0, 65535, BuiltInClass::UNSIGNED_16},
		{-32768, 32767, BuiltInClass::SIGNED_16},
		{0, 4294967295, BuiltInClass::UNSIGNED_32},
		{-2147483648, 2147483647, BuiltInClass::SIGNED_32},
	};
	
	// Then compare those integers.
	for(const auto& range : integers) {
		if((range.low == low_value || range.low == -low_value) && range.high == high_value) {
			return range.classification;
		}
	}
	
	return BuiltInClass::UNKNOWN_PROBABLY_ARRAY;
}

s8 eat_s8(const char*& input) {
	verify(*input != '\0', ERR_END_OF_SYMBOL);
	return *(input++);
}

s64 eat_s64_literal(const char*& input) {
	std::string number;
	if(*input == '-') {
		number = "-";
		input++;
	}
	for(; *input != '\0'; input++) {
		if(*input < '0' || *input > '9') {
			break;
		}
		number += *input;
	}
	verify(number.size() > 0, "Unexpected '%c' (%02hhx).", *input, *input);
	try {
		return std::stol(number);
	} catch(std::out_of_range&) {
		return 0;
	}
}

std::string eat_stabs_identifier(const char*& input) {
	std::string identifier;
	bool first = true;
	for(; *input != '\0'; input++) {
		bool valid_char = false;
		valid_char |= isprint(*input) && *input != ':' && *input != ';';
		valid_char |= !first && isalnum(*input);
		if(valid_char) {
			identifier += *input;
		} else {
			return identifier;
		}
		first = false;
	}
	verify_not_reached(ERR_END_OF_SYMBOL);
}

// The complexity here is because the input may contain an unescaped namespace
// separator '::' even if the field terminator is supposed to be a colon.
std::string eat_dodgy_stabs_identifier(const char*& input) {
	std::string identifier;
	bool first = true;
	s32 template_depth = 0;
	for(; *input != '\0'; input++) {
		if(*input == '<') {
			template_depth++;
		}
		if(*input == '>') {
			template_depth--;
		}
		bool valid_char = false;
		valid_char |= isprint(*input) && (*input != ':' || template_depth != 0) && *input != ';';
		valid_char |= !first && isalnum(*input);
		if(valid_char) {
			identifier += *input;
		} else {
			return identifier;
		}
		first = false;
	}
	verify_not_reached(ERR_END_OF_SYMBOL);
}


void expect_s8(const char*& input, s8 expected, const char* subject) {
	verify(*input != '\0', ERR_END_OF_SYMBOL);
	char val = *(input++);
	verify(val == expected, "Expected '%c' in %s, got '%c'.", expected, subject, val);
}

static void print_field(const StabsField& field) {
	printf("\t%04x %04x %04x %04x %s\n", field.offset_bits / 8, field.size_bits / 8, field.offset_bits, field.size_bits, field.name.c_str());
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
		case BuiltInClass::UNKNOWN_PROBABLY_ARRAY: return "error";
	}
	return "";
}

}
