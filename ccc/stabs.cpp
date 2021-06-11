#include "ccc.h"

#include <algorithm>

static StabsType parse_type(const char*& input);
static std::vector<StabsField> parse_field_list(const char*& input);
static std::vector<StabsMemberFunction> parse_member_functions(const char*& input);
static s8 eat_s8(const char*& input);
static s64 eat_s64_literal(const char*& input);
static std::string eat_identifier(const char*& input);
static void expect_s8(const char*& input, s8 expected, const char* subject);
static void validate_symbol_descriptor(StabsSymbolDescriptor descriptor);
static void print_field(const StabsField& field);

static const char* ERR_END_OF_INPUT =
	"error: Unexpected end of input while parsing STAB type.\n";

#define STABS_DEBUG(...) //__VA_ARGS__
#define STABS_DEBUG_PRINTF(...) STABS_DEBUG(printf(__VA_ARGS__);)

std::vector<StabsSymbol> parse_stabs_symbols(const std::vector<Symbol>& input) {
	std::vector<StabsSymbol> output;
	std::string prefix;
	for(const Symbol& symbol : input) {
		if(symbol.storage_type == SymbolType::NIL && (u32) symbol.storage_class == 0) {
			if(symbol.string.size() == 0) {
				verify(prefix.size() == 0, "error: Invalid STABS continuation.\n");
				continue;
			}
			// Some STABS symbols are split between multiple strings.
			if(symbol.string[symbol.string.size() - 1] == '\\') {
				prefix += symbol.string.substr(0, symbol.string.size() - 1);
			} else {
				std::string symbol_string = prefix + symbol.string;
				prefix = "";
				if(symbol_string[0] == '$') {
					continue;
				}
				StabsSymbol stabs_symbol = parse_stabs_symbol(symbol_string.c_str());
				stabs_symbol.mdebug_symbol = symbol;
				switch(stabs_symbol.descriptor) {
					case StabsSymbolDescriptor::TYPE_NAME:
					case StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG:
						output.emplace_back(std::move(stabs_symbol));
						break;
				}
			}
		}
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
	symbol.type.name = symbol.name;
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
		case StabsTypeDescriptor::TYPE_REFERENCE: // 0..9
			type.type_reference.type = std::make_unique<StabsType>(parse_type(input));
			break;
		case StabsTypeDescriptor::ARRAY: // a
			type.array_type.index_type = std::make_unique<StabsType>(parse_type(input));
			type.array_type.element_type = std::make_unique<StabsType>(parse_type(input));
			break;
		case StabsTypeDescriptor::ENUM: // e
			STABS_DEBUG_PRINTF("enum {\n");
			while(*input != ';') {
				std::string name = eat_identifier(input);
				expect_s8(input, ':', "identifier");
				s64 value = eat_s64_literal(input);
				type.enum_type.fields.emplace_back(value, name);
				verify(eat_s8(input) == ',',
					"error: Expecting ',' while parsing enum, got '%c' (%02hhx).",
					*input, *input);
			}
			STABS_DEBUG_PRINTF("}\n");
			break;
		case StabsTypeDescriptor::FUNCTION: // f
			type.function_type.type = std::make_unique<StabsType>(parse_type(input));
			break;
		case StabsTypeDescriptor::RANGE: // r
			type.range_type.type = std::make_unique<StabsType>(parse_type(input));
			expect_s8(input, ';', "range type descriptor");
			type.range_type.low = eat_s64_literal(input);
			expect_s8(input, ';', "low range value");
			type.range_type.high = eat_s64_literal(input);
			expect_s8(input, ';', "high range value");
			break;
		case StabsTypeDescriptor::STRUCT: // s
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
		case StabsTypeDescriptor::UNION: // u
			STABS_DEBUG_PRINTF("union {\n");
			type.struct_or_union.size = eat_s64_literal(input);
			type.struct_or_union.fields = parse_field_list(input);
			type.struct_or_union.member_functions = parse_member_functions(input);
			STABS_DEBUG_PRINTF("}\n");
			break;
		case StabsTypeDescriptor::CROSS_REFERENCE: // x
			type.cross_reference.type = eat_s8(input);
			switch(type.cross_reference.type) {
				case 's': // struct
				case 'u': // union
				case 'e': // enum
					break;
				default:
					verify_not_reached("error: Invalid cross reference type '%c'.\n",
						type.cross_reference.type);
			}
			type.cross_reference.identifier = eat_identifier(input);
			type.name = type.cross_reference.identifier;
			expect_s8(input, ':', "cross reference");
			break;
		case StabsTypeDescriptor::METHOD: // #
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
		case StabsTypeDescriptor::REFERENCE: // &
		case StabsTypeDescriptor::POINTER: // *
			type.reference_or_pointer.value_type = std::make_unique<StabsType>(parse_type(input));
			break;
		case StabsTypeDescriptor::SLASH: // /
			// Not sure.
			eat_s64_literal(input);
			break;
		case StabsTypeDescriptor::MEMBER: // @
			verify(*input == 's', "error: Weird value following '@' type descriptor.\n");
			break;
		default:
			verify_not_reached("error: Invalid type descriptor '%c' (%02x).\n",
				(u32) type.descriptor, (u32) type.descriptor);
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
					verify_not_reached("error: Invalid field visibility.\n");
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
			field.offset = eat_s64_literal(input);
			expect_s8(input, ';', "field offset");
		} else if(*input == ':') {
			input++;
			field.type_name = eat_identifier(input);
			expect_s8(input, ';', "identifier");
		} else if(*input == ',') {
			input++;
			field.offset = eat_s64_literal(input);
			expect_s8(input, ',', "field offset");
			field.size = eat_s64_literal(input);
			expect_s8(input, ';', "field size");
		} else {
			verify_not_reached("error: Expected ':' or ',', got '%c' (%hhx).\n", *input, *input);
		}

		STABS_DEBUG(print_field(field);)

		fields.emplace_back(std::move(field));
	}
	return fields;
}

static std::vector<StabsMemberFunction> parse_member_functions(const char*& input) {
	if(*input == ',') {
		return {};
	}
	
	std::vector<StabsMemberFunction> member_functions;
	while(*input != '\0') {
		if(*input == ';') {
			input++;
			break;
		}
		const char* before = input;
		StabsMemberFunction member_function;
		member_function.name = eat_identifier(input);
		expect_s8(input, ':', "member function");
		expect_s8(input, ':', "member function");
		while(*input != '\0') {
			if(*input == ';') {
				input++;
				break;
			}
			
			StabsMemberFunctionField field;
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
					verify_not_reached("error: Invalid visibility for member function.\n");
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
					verify_not_reached("error: Invalid member function modifiers.\n");
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
					verify_not_reached("error: Invalid member function type.\n");
			}
			member_function.fields.emplace_back(std::move(field));
		}
		STABS_DEBUG_PRINTF("member func: %s\n", member_function.name.c_str());
		member_functions.emplace_back(std::move(member_function));
	}
	return member_functions;
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
	verify(number.size() > 0, "error: Unexpected '%c' (%02hhx).\n", *input, *input);
	try {
		return std::stol(number);
	} catch(std::out_of_range&) {
		return 0;
	}
}

static std::string eat_identifier(const char*& input) {
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
	verify_not_reached(ERR_END_OF_INPUT);
}

static void expect_s8(const char*& input, s8 expected, const char* subject) {
	verify(*input != '\0', ERR_END_OF_INPUT);
	char val = *(input++);
	verify(val == expected, "error: Expected '%c' in %s, got '%c'.\n", expected, subject, val);
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
			verify_not_reached("error: Unknown symbol descriptor: %c.\n", (s8) descriptor);
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
	printf("\t%04x %04x %04x %04x %s\n", field.offset / 8, field.size / 8, field.offset, field.size, field.name.c_str());
}

static void enumerate_numbered_types_recursive(std::map<s32, const StabsType*>& output, const StabsType& type);

static void enumerate_unique_ptr(std::map<s32, const StabsType*>& output, const std::unique_ptr<StabsType>& type) {
	if(type.get()) {
		enumerate_numbered_types_recursive(output, *type.get());
	}
}

static void enumerate_numbered_types_recursive(std::map<s32, const StabsType*>& output, const StabsType& type) {
	if(!type.anonymous) {
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
	for(const StabsMemberFunction& member_functions : type.struct_or_union.member_functions) {
		for(const StabsMemberFunctionField& field : member_functions.fields) {
			enumerate_numbered_types_recursive(output, field.type);
		}
	}
	enumerate_unique_ptr(output, type.method.return_type);
	enumerate_unique_ptr(output, type.method.class_type);
	for(const StabsType& parameter_type : type.method.parameter_types) {
		enumerate_numbered_types_recursive(output, parameter_type);
	}
	enumerate_unique_ptr(output, type.reference_or_pointer.value_type);
}

std::map<s32, const StabsType*> enumerate_numbered_types(const std::vector<StabsSymbol>& symbols) {
	std::map<s32, const StabsType*> output;
	for(const StabsSymbol& symbol : symbols) {
		enumerate_numbered_types_recursive(output, symbol.type);
	}
	return output;
}
