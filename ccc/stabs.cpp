#include "stabs.h"

#include <algorithm>
#include <string>

namespace ccc {

static StabsType parse_type(const char*& input);
static std::vector<StabsField> parse_field_list(const char*& input);
static std::vector<StabsMemberFunctionSet> parse_member_functions(const char*& input);
static RangeClass classify_range(const std::string& low, const std::string& high);
static s8 eat_s8(const char*& input);
static s64 eat_s64_literal(const char*& input);
static std::string eat_identifier(const char*& input);
static void expect_s8(const char*& input, s8 expected, const char* subject);
static void validate_symbol_descriptor(StabsSymbolDescriptor descriptor);
static void print_field(const StabsField& field);

static const char* ERR_END_OF_INPUT =
	"Unexpected end of input while parsing STAB type.";

#define STABS_DEBUG(...) //__VA_ARGS__
#define STABS_DEBUG_PRINTF(...) STABS_DEBUG(printf(__VA_ARGS__);)

std::vector<StabsSymbol> parse_stabs_symbols(const std::vector<Symbol>& input, SourceLanguage detected_language) {
	std::vector<StabsSymbol> output;
	std::string prefix;
	bool last_symbol_was_end = false;
	for(const Symbol& symbol : input) {
		bool correct_type =
			   (symbol.storage_type == SymbolType::NIL
				&& symbol.storage_class != SymbolClass::COMPILER_VERSION_INFO)
			|| (last_symbol_was_end
				&& symbol.storage_type == SymbolType::LABEL
				&& (detected_language == SourceLanguage::C
					|| detected_language == SourceLanguage::CPP));
		if(correct_type) {
			// Some STABS symbols are split between multiple strings.
			if(!symbol.string.empty()) {
				if(symbol.string[symbol.string.size() - 1] == '\\') {
					prefix += symbol.string.substr(0, symbol.string.size() - 1);
				} else {
					std::string symbol_string = prefix + symbol.string;
					prefix.clear();
					bool stab_valid = true;
					stab_valid &= symbol_string[0] != '$';
					stab_valid &= symbol_string != "gcc2_compiled.";
					stab_valid &= symbol_string != "__gnu_compiled_c";
					stab_valid &= symbol_string != "__gnu_compiled_cpp";
					if(stab_valid) {
						StabsSymbol stabs_symbol = parse_stabs_symbol(symbol_string.c_str());
						stabs_symbol.mdebug_symbol = symbol;
						output.emplace_back(std::move(stabs_symbol));
					}
				}
			} else {
				verify(prefix.empty(), "Invalid STABS continuation.");
			}
		}
		last_symbol_was_end = (symbol.storage_type == SymbolType::END);
	}
	return output;
}

StabsSymbol parse_stabs_symbol(const char* input) {
	STABS_DEBUG_PRINTF("PARSING %s\n", input);
	
	StabsSymbol symbol;
	symbol.raw = std::string(input);
	symbol.name = eat_identifier(input);
	expect_s8(input, ':', "identifier");
	verify(*input != '\0', ERR_END_OF_INPUT);
	if(*input >= '0' && *input <= '9') {
		symbol.descriptor = StabsSymbolDescriptor::LOCAL_VARIABLE;
	} else {
		symbol.descriptor = (StabsSymbolDescriptor) eat_s8(input);
	}
	validate_symbol_descriptor(symbol.descriptor);
	verify(*input != '\0', ERR_END_OF_INPUT);
	if(*input == 't') {
		input++;
	}
	symbol.type = parse_type(input);
	// This is a bit of hack to make it so variable names aren't used as type
	// names e.g.the STABS symbol "somevar:P123=*456" may be referenced by the
	// type number 123, but the type name is not "somevar".
	bool is_type = symbol.descriptor == StabsSymbolDescriptor::TYPE_NAME
		|| symbol.descriptor == StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG; 
	if(is_type) {
		symbol.type.name = symbol.name;
	}
	symbol.type.is_typedef = symbol.descriptor == StabsSymbolDescriptor::TYPE_NAME;
	symbol.type.is_root = true;
	return symbol;
}

static StabsType parse_type(const char*& input) {
	StabsType type;
	verify(*input != '\0', ERR_END_OF_INPUT);
	if(*input >= '0' && *input <= '9') {
		type.anonymous = false;
		type.type_number = eat_s64_literal(input);
		if(*input != '=') {
			type.has_body = false;
			return type;
		}
		input++;
	} else {
		type.anonymous = true;
	}
	type.has_body = true;
	verify(*input != '\0', ERR_END_OF_INPUT);
	if(*input >= '0' && *input <= '9') {
		type.descriptor = StabsTypeDescriptor::TYPE_REFERENCE;
	} else {
		type.descriptor = (StabsTypeDescriptor) eat_s8(input);
	}
	switch(type.descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE: { // 0..9
			type.type_reference.type = std::make_unique<StabsType>(parse_type(input));
			break;
		}
		case StabsTypeDescriptor::ARRAY: { // a
			type.array_type.index_type = std::make_unique<StabsType>(parse_type(input));
			type.array_type.element_type = std::make_unique<StabsType>(parse_type(input));
			break;
		}
		case StabsTypeDescriptor::ENUM: { // e
			STABS_DEBUG_PRINTF("enum {\n");
			while(*input != ';') {
				std::string name = eat_identifier(input);
				expect_s8(input, ':', "identifier");
				s64 value = eat_s64_literal(input);
				type.enum_type.fields.emplace_back(value, name);
				verify(eat_s8(input) == ',',
					"Expecting ',' while parsing enum, got '%c' (%02hhx)",
					*input, *input);
			}
			input++;
			STABS_DEBUG_PRINTF("}\n");
			break;
		}
		case StabsTypeDescriptor::FUNCTION: { // f
			type.function_type.type = std::make_unique<StabsType>(parse_type(input));
			break;
		}
		case StabsTypeDescriptor::RANGE: { // r
			type.range_type.type = std::make_unique<StabsType>(parse_type(input));
			expect_s8(input, ';', "range type descriptor");
			std::string low = eat_identifier(input);
			expect_s8(input, ';', "low range value");
			std::string high = eat_identifier(input);
			expect_s8(input, ';', "high range value");
			try {
				type.range_type.low_maybe_wrong = std::stoi(low);
				type.range_type.high_maybe_wrong = std::stoi(high);
			} catch(std::out_of_range&) {/* this case doesn't matter */}
			type.range_type.range_class = classify_range(low, high);
			break;
		}
		case StabsTypeDescriptor::STRUCT: { // s
			STABS_DEBUG_PRINTF("struct {\n");
			type.struct_or_union.size = eat_s64_literal(input);
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
					base_class.type = parse_type(input);
					expect_s8(input, ';', "base class section");
					type.struct_or_union.base_classes.emplace_back(std::move(base_class));
				}
			}
			type.struct_or_union.fields = parse_field_list(input);
			type.struct_or_union.member_functions = parse_member_functions(input);
			STABS_DEBUG_PRINTF("}\n");
			break;
		}
		case StabsTypeDescriptor::UNION: { // u
			STABS_DEBUG_PRINTF("union {\n");
			type.struct_or_union.size = eat_s64_literal(input);
			type.struct_or_union.fields = parse_field_list(input);
			type.struct_or_union.member_functions = parse_member_functions(input);
			STABS_DEBUG_PRINTF("}\n");
			break;
		}
		case StabsTypeDescriptor::CROSS_REFERENCE: { // x
			type.cross_reference.type = eat_s8(input);
			switch(type.cross_reference.type) {
				case 's': // struct
				case 'u': // union
				case 'e': // enum
					break;
				default:
					verify_not_reached("Invalid cross reference type '%c'.",
						type.cross_reference.type);
			}
			type.cross_reference.identifier = eat_identifier(input);
			type.name = type.cross_reference.identifier;
			expect_s8(input, ':', "cross reference");
			break;
		}
		case StabsTypeDescriptor::METHOD: { // #
			if(*input == '#') {
				input++;
				type.method.return_type = std::make_unique<StabsType>(parse_type(input));
				expect_s8(input, ';', "method");
			} else {
				type.method.class_type = std::make_unique<StabsType>(parse_type(input));
				expect_s8(input, ',', "method");
				type.method.return_type = std::make_unique<StabsType>(parse_type(input));
				while(*input != '\0') {
					if(*input == ';') {
						input++;
						break;
					}
					expect_s8(input, ',', "method");
					type.method.parameter_types.emplace_back(parse_type(input));
				}
			}
			break;
		}
		case StabsTypeDescriptor::REFERENCE: // &
		case StabsTypeDescriptor::POINTER: { // *
			type.reference_or_pointer.value_type = std::make_unique<StabsType>(parse_type(input));
			break;
		}
		case StabsTypeDescriptor::TYPE_ATTRIBUTE: { // @
			verify(*input == 's', "Weird value following '@' type descriptor. Please submit a bug report!");
			input++;
			type.size_type_attribute.size_bits = eat_s64_literal(input);
			expect_s8(input, ';', "type attribute");
			type.size_type_attribute.type = std::make_unique<StabsType>(parse_type(input));
			break;
		}
		case StabsTypeDescriptor::BUILT_IN: { // -
			type.built_in.type_id = eat_s64_literal(input);
			break;
		}
		default: {
			verify_not_reached("Invalid type descriptor '%c' (%02x). Please file a bug report!",
				(u32) type.descriptor, (u32) type.descriptor);
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
		field.name = eat_identifier(input);
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
		field.type = parse_type(input);
		if(field.name.size() >= 1 && field.name[0] == '$') {
			// Not sure.
			expect_s8(input, ',', "field type");
			field.offset_bits = eat_s64_literal(input);
			expect_s8(input, ';', "field offset");
		} else if(*input == ':') {
			input++;
			field.is_static = true;
			field.type_name = eat_identifier(input);
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
		member_function_set.name = eat_identifier(input);
		expect_s8(input, ':', "member function");
		expect_s8(input, ':', "member function");
		while(*input != '\0') {
			if(*input == ';') {
				input++;
				break;
			}
			
			StabsMemberFunctionOverload field;
			field.type = parse_type(input);
			
			expect_s8(input, ':', "member function");
			eat_identifier(input);
			expect_s8(input, ';', "member function");
			field.visibility = (StabsFieldVisibility) eat_s8(input);
			switch(field.visibility) {
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
					field.is_const = false;
					field.is_volatile = false;
					break;
				case 'B':
					field.is_const = true;
					field.is_volatile = false;
					break;
				case 'C':
					field.is_const = false;
					field.is_volatile = true;
					break;
				case 'D':
					field.is_const = true;
					field.is_volatile = true;
					break;
				case '?':
				case '.':
					break;
				default:
					verify_not_reached("Invalid member function modifiers.");
			}
			switch(eat_s8(input)) {
				case '*': // virtual member function
					eat_s64_literal(input);
					expect_s8(input, ';', "virtual member function");
					parse_type(input);
					expect_s8(input, ';', "virtual member function");
					break;
				case '?': // static member function
					break;
				case '.': // normal member function
					break;
				default:
					verify_not_reached("Invalid member function type.");
			}
			member_function_set.overloads.emplace_back(std::move(field));
		}
		STABS_DEBUG_PRINTF("member func: %s\n", member_function_set.name.c_str());
		member_functions.emplace_back(std::move(member_function_set));
	}
	return member_functions;
}

static RangeClass classify_range(const std::string& low, const std::string& high) {
	// Handle some special cases and values that are too large to easily store
	// in a 64-bit integer.
	static const struct { const char* low; const char* high; RangeClass classification; } strings[] = {
		{"4", "0", RangeClass::FLOAT_32},
		{"000000000000000000000000", "001777777777777777777777", RangeClass::UNSIGNED_64},
		{"00000000000000000000000000000000000000000000", "00000000000000000000001777777777777777777777", RangeClass::UNSIGNED_64},
		{"001000000000000000000000", "000777777777777777777777", RangeClass::SIGNED_64},
		{"00000000000000000000001000000000000000000000", "00000000000000000000000777777777777777777777", RangeClass::SIGNED_64},
		{"8", "0", RangeClass::FLOAT_64},
		{"00000000000000000000000000000000000000000000", "03777777777777777777777777777777777777777777", RangeClass::UNSIGNED_128},
		{"02000000000000000000000000000000000000000000", "01777777777777777777777777777777777777777777", RangeClass::SIGNED_128}
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
		return RangeClass::UNKNOWN_PROBABLY_ARRAY;
	}
	
	static const struct { s64 low; s64 high; RangeClass classification; } integers[] = {
		{0, 255, RangeClass::UNSIGNED_8},
		{-128, 127, RangeClass::SIGNED_8},
		{0, 65535, RangeClass::UNSIGNED_16},
		{-32768, 32767, RangeClass::SIGNED_16},
		{0, 4294967295, RangeClass::UNSIGNED_32},
		{-2147483648, 2147483647, RangeClass::SIGNED_32},
	};
	
	// Then compare those integers.
	for(const auto& range : integers) {
		if((range.low == low_value || range.low == -low_value) && range.high == high_value) {
			return range.classification;
		}
	}
	
	return RangeClass::UNKNOWN_PROBABLY_ARRAY;
}

static s8 eat_s8(const char*& input) {
	verify(*input != '\0', ERR_END_OF_INPUT);
	return *(input++);
}

static s64 eat_s64_literal(const char*& input) {
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

// The complexity here is because the input may contain an unescaped namespace
// separator '::' even if the field terminator is supposed to be a colon.
static std::string eat_identifier(const char*& input) {
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
	verify_not_reached(ERR_END_OF_INPUT);
}


static void expect_s8(const char*& input, s8 expected, const char* subject) {
	verify(*input != '\0', ERR_END_OF_INPUT);
	char val = *(input++);
	verify(val == expected, "Expected '%c' in %s, got '%c'.", expected, subject, val);
}

static void validate_symbol_descriptor(StabsSymbolDescriptor descriptor) {
	switch(descriptor) {
		case StabsSymbolDescriptor::LOCAL_VARIABLE:
		case StabsSymbolDescriptor::A:
		case StabsSymbolDescriptor::LOCAL_FUNCTION:
		case StabsSymbolDescriptor::GLOBAL_FUNCTION:
		case StabsSymbolDescriptor::GLOBAL_VARIABLE:
		case StabsSymbolDescriptor::REGISTER_PARAMETER:
		case StabsSymbolDescriptor::VALUE_PARAMETER:
		case StabsSymbolDescriptor::REGISTER_VARIABLE:
		case StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE:
		case StabsSymbolDescriptor::TYPE_NAME:
		case StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG:
		case StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE:
			break;
		default:
			verify_not_reached("Unknown symbol descriptor: %c.", (s8) descriptor);
	}
}

void print_stabs_type(const StabsType& type) {
	printf("type descriptor: %c\n", (s8) type.descriptor);
	printf("fields (offset, size, offset in bits, size in bits, name):\n");
	for(const StabsField& field : type.struct_or_union.fields) {
		print_field(field);
	}
}

static void print_field(const StabsField& field) {
	printf("\t%04x %04x %04x %04x %s\n", field.offset_bits / 8, field.size_bits / 8, field.offset_bits, field.size_bits, field.name.c_str());
}

static void enumerate_numbered_types_recursive(std::map<s32, const StabsType*>& output, const StabsType& type);

static void enumerate_unique_ptr(std::map<s32, const StabsType*>& output, const std::unique_ptr<StabsType>& type) {
	if(type.get()) {
		enumerate_numbered_types_recursive(output, *type.get());
	}
}

static void enumerate_numbered_types_recursive(std::map<s32, const StabsType*>& output, const StabsType& type) {
	if(!type.anonymous && type.has_body) {
		output.emplace(type.type_number, &type);
	}
	enumerate_unique_ptr(output, type.type_reference.type);
	enumerate_unique_ptr(output, type.array_type.index_type);
	enumerate_unique_ptr(output, type.array_type.element_type);
	enumerate_unique_ptr(output, type.function_type.type);
	enumerate_unique_ptr(output, type.range_type.type);
	for(const StabsBaseClass& base_class : type.struct_or_union.base_classes) {
		enumerate_numbered_types_recursive(output, base_class.type);
	}
	for(const StabsField& field : type.struct_or_union.fields) {
		enumerate_numbered_types_recursive(output, field.type);
	}
	for(const StabsMemberFunctionSet& member_functions : type.struct_or_union.member_functions) {
		for(const StabsMemberFunctionOverload& overload : member_functions.overloads) {
			enumerate_numbered_types_recursive(output, overload.type);
		}
	}
	enumerate_unique_ptr(output, type.method.return_type);
	enumerate_unique_ptr(output, type.method.class_type);
	for(const StabsType& parameter_type : type.method.parameter_types) {
		enumerate_numbered_types_recursive(output, parameter_type);
	}
	enumerate_unique_ptr(output, type.reference_or_pointer.value_type);
	enumerate_unique_ptr(output, type.size_type_attribute.type);
}

std::map<s32, const StabsType*> enumerate_numbered_types(const std::vector<StabsSymbol>& symbols) {
	std::map<s32, const StabsType*> output;
	for(const StabsSymbol& symbol : symbols) {
		enumerate_numbered_types_recursive(output, symbol.type);
	}
	return output;
}

}
