#include "ccc.h"

#include <algorithm>

//#pragma optimize("", off)
static StabsType parse_type(const char*& input, bool verbose);
static std::vector<StabsField> parse_field_list(const char*& input, bool verbose);
static s8 eat_s8(const char*& input);
static s64 eat_s64_literal(const char*& input);
static std::string eat_identifier(const char*& input);
static void expect_s8(const char*& input, s8 expected, const char* subject);
static void validate_symbol_descriptor(StabsSymbolDescriptor descriptor);
static void print_field(const StabsField& field);

static const char* ERR_END_OF_INPUT =
	"error: Unexpected end of input while parsing STAB type.\n";

StabsSymbol parse_stabs_symbol(const char* input, bool verbose) {
	StabsSymbol symbol;
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
	symbol.type_number = eat_s64_literal(input);
	if(*input == '\0') {
		return symbol;
	}
	verify(eat_s8(input) == '=', "error: Expected '='.\n");
	symbol.type = parse_type(input, verbose);
	return symbol;
}

static StabsType parse_type(const char*& input, bool verbose) {
	StabsType type;
	verify(*input != '\0', ERR_END_OF_INPUT);
	if(*input >= '0' && *input <= '9') {
		type.descriptor = StabsTypeDescriptor::TYPE_REFERENCE;
	} else {
		type.descriptor = (StabsTypeDescriptor) eat_s8(input);
	}
	switch(type.descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE:
			type.type_reference.type_number = eat_s64_literal(input);
			break;
		case StabsTypeDescriptor::ARRAY:
			type.array_type.index_type = new StabsType(parse_type(input, verbose));
			type.array_type.element_type = new StabsType(parse_type(input, verbose));
			break;
		case StabsTypeDescriptor::ENUM:
			while(*input != ';') {
				std::string name = eat_identifier(input);
				expect_s8(input, ':', "identifier");
				s64 value = eat_s64_literal(input);
				type.enum_type.fields.emplace_back(name, value);
				verify(eat_s8(input) == ',',
					"error: Expecting ',' while parsing enum, got '%c' (%02hhx).",
					*input, *input);
			}
			std::sort(type.enum_type.fields.begin(), type.enum_type.fields.end(),
				[](const std::pair<std::string, s64>& f1, const std::pair<std::string, s64>& f2) {
					return f1.second < f2.second;
				}
			);
			break;
		case StabsTypeDescriptor::FUNCTION:
			eat_s64_literal(input);
			break;
		case StabsTypeDescriptor::RANGE:
			type.range_type.type = new StabsType(parse_type(input, verbose));
			expect_s8(input, ';', "range type descriptor");
			type.range_type.low = eat_s64_literal(input);
			expect_s8(input, ';', "low range value");
			type.range_type.high = eat_s64_literal(input);
			expect_s8(input, ';', "high range value");
			break;
		case StabsTypeDescriptor::STRUCT:
			type.struct_type.type_number = eat_s64_literal(input);
			if(*input == '!') {
				input++;
				eat_s64_literal(input);
				expect_s8(input, ',', "!");
				eat_s64_literal(input);
				expect_s8(input, ',', "!");
				parse_type(input, verbose);
				expect_s8(input, ';', "!");
			}
			type.struct_type.fields = parse_field_list(input, verbose);
			break;
		case StabsTypeDescriptor::UNION:
			type.union_type.type_number = eat_s64_literal(input);
			type.union_type.fields = parse_field_list(input, verbose);
			break;
		case StabsTypeDescriptor::AMPERSAND:
			// Not sure.
			eat_s64_literal(input);
			break;
		case StabsTypeDescriptor::POINTER:
			type.pointer_type.value_type = new StabsType(parse_type(input, verbose));
			break;
		case StabsTypeDescriptor::SLASH:
			// Not sure.
			eat_s64_literal(input);
			break;
		case StabsTypeDescriptor::MEMBER:
			verify(*input == 's', "error: Weird value following '@' type descriptor.\n");
			break;
		default:
			eat_identifier(input);
			expect_s8(input, ':', "identifier");
	}
	if(*input == '=') {
		input++;
		type.aux_type = new StabsType(parse_type(input, verbose));
	}
	return type;
}

static std::vector<StabsField> parse_field_list(const char*& input, bool verbose) {
	std::vector<StabsField> fields;
	while(*input != '\0') {
		StabsField field;
		field.name = eat_identifier(input);
		expect_s8(input, ':', "identifier");
		if(*input == ':') {
			// TODO: Parse the last part.
			// For now, scan until we reach a ;;.
			s8 last = 0, current = 0;
			for(; *input != '\0'; input++) {
				last = current;
				current = *input;
				if(last == ';' && current == ';') {
					input++;
					break;
				}
			}
			break;
		}
		field.type = parse_type(input, verbose);
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
			verify_not_reached("error: Expected ':' or ',', got '%c' (%hhx).", *input, *input);
		}

		if(verbose)
			print_field(field);

		fields.emplace_back(field);
		if(*input == ';') {
			input++;
			break;
		}
	}
	return fields;
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
	verify(val == expected, "error: Expected '%c' after %s, got '%c'.\n", expected, subject, val);
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
	for(const StabsField& field : type.struct_type.fields) {
		print_field(field);
	}
}

static void print_field(const StabsField& field) {
	printf("%04llx %04llx %04llx %04llx %s\n", field.offset / 8, field.size / 8, field.offset, field.size, field.name.c_str());
}
