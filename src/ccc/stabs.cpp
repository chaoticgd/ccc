// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "stabs.h"

namespace ccc {

#define STABS_DEBUG(...) //__VA_ARGS__
#define STABS_DEBUG_PRINTF(...) STABS_DEBUG(printf(__VA_ARGS__);)

static Result<std::unique_ptr<StabsType>> parse_stabs_type(const char*& input);
static Result<std::vector<StabsField>> parse_field_list(const char*& input);
static Result<std::vector<StabsMemberFunctionSet>> parse_member_functions(const char*& input);
STABS_DEBUG(static void print_field(const StabsField& field);)

Result<std::unique_ptr<StabsType>> parse_top_level_stabs_type(const char*& input) {
	Result<std::unique_ptr<StabsType>> type = parse_stabs_type(input);
	CCC_RETURN_IF_ERROR(type);
	
	// Handle first base class suffixes.
	if((*type)->descriptor == StabsTypeDescriptor::STRUCT && input[0] == '~' && input[1] == '%') {
		input += 2;
		
		Result<std::unique_ptr<StabsType>> first_base_class = parse_stabs_type(input);
		CCC_RETURN_IF_ERROR(first_base_class);
		(*type)->as<StabsStructType>().first_base_class = std::move(*first_base_class);
		
		CCC_EXPECT_CHAR(input, ';', "first base class suffix");
	}
	
	// Handle extra live range information.
	if(input[0] == ';' && input[1] == 'l') {
		input += 2;
		CCC_EXPECT_CHAR(input, '(', "live range suffix");
		CCC_EXPECT_CHAR(input, '#', "live range suffix");
		std::optional<s32> start = eat_s32_literal(input);
		CCC_CHECK(start.has_value(), "Failed to parse live range suffix.");
		CCC_EXPECT_CHAR(input, ',', "live range suffix");
		CCC_EXPECT_CHAR(input, '#', "live range suffix");
		std::optional<s32> end = eat_s32_literal(input);
		CCC_CHECK(end.has_value(), "Failed to parse live range suffix.");
		CCC_EXPECT_CHAR(input, ')', "live range suffix");
	}
	
	return type;
}

static Result<std::unique_ptr<StabsType>> parse_stabs_type(const char*& input) {
	StabsTypeInfo info;
	CCC_CHECK(*input != '\0', "Unexpected end of input.");
	if(*input == '(') {
		// This file has type numbers made up of two pieces: an include file
		// index and a type number.
		
		input++;
		
		std::optional<s32> file_number = eat_s32_literal(input);
		CCC_CHECK(file_number.has_value(), "Cannot parse file number.");
		
		CCC_EXPECT_CHAR(input, ',', "type number");
		
		std::optional<s32> type_number = eat_s32_literal(input);
		CCC_CHECK(type_number.has_value(), "Cannot parse type number.");
		
		CCC_EXPECT_CHAR(input, ')', "type number");
		
		info.anonymous = false;
		info.type_number.file = *file_number;
		info.type_number.type = *type_number;
		if(*input != '=') {
			info.has_body = false;
			return std::make_unique<StabsType>(info);
		}
		input++;
	} else if(*input >= '0' && *input <= '9') {
		// This file has type numbers which are just a single number. This is
		// the more common case for games.
		
		info.anonymous = false;
		
		std::optional<s32> type_number = eat_s32_literal(input);
		CCC_CHECK(type_number.has_value(), "Cannot parse type number.");
		info.type_number.type = *type_number;
		
		if(*input != '=') {
			info.has_body = false;
			return std::make_unique<StabsType>(info);
		}
		input++;
	} else {
		info.anonymous = true;
	}
	info.has_body = true;
	
	CCC_CHECK(*input != '\0', "Unexpected end of input.");
	
	StabsTypeDescriptor descriptor;
	if((*input >= '0' && *input <= '9') || *input == '(') {
		descriptor = StabsTypeDescriptor::TYPE_REFERENCE;
	} else {
		std::optional<char> descriptor_char = eat_char(input);
		CCC_CHECK(descriptor_char.has_value(), "Cannot parse type descriptor.");
		descriptor = (StabsTypeDescriptor) *descriptor_char;
	}
	
	std::unique_ptr<StabsType> out_type;
	
	switch(descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE: { // 0..9
			auto type_reference = std::make_unique<StabsTypeReferenceType>(info);
			
			auto type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(type);
			type_reference->type = std::move(*type);
			
			out_type = std::move(type_reference);
			break;
		}
		case StabsTypeDescriptor::ARRAY: { // a
			auto array = std::make_unique<StabsArrayType>(info);
			
			auto index_type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(index_type);
			array->index_type = std::move(*index_type);
			
			auto element_type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(element_type);
			array->element_type = std::move(*element_type);
			
			out_type = std::move(array);
			break;
		}
		case StabsTypeDescriptor::ENUM: { // e
			auto enum_type = std::make_unique<StabsEnumType>(info);
			STABS_DEBUG_PRINTF("enum {\n");
			while(*input != ';') {
				std::optional<std::string> name = eat_dodgy_stabs_identifier(input);
				CCC_CHECK(name.has_value(), "Cannot parse enum field name.");
				
				CCC_EXPECT_CHAR(input, ':', "enum");
				
				std::optional<s32> value = eat_s32_literal(input);
				CCC_CHECK(value.has_value(), "Cannot parse enum value.");
				
				enum_type->fields.emplace_back(*value, std::move(*name));
				
				CCC_EXPECT_CHAR(input, ',', "enum");
			}
			input++;
			STABS_DEBUG_PRINTF("}\n");
			
			out_type = std::move(enum_type);
			break;
		}
		case StabsTypeDescriptor::FUNCTION: { // f
			auto function = std::make_unique<StabsFunctionType>(info);
			
			auto return_type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(return_type);
			function->return_type = std::move(*return_type);
			
			out_type = std::move(function);
			break;
		}
		case StabsTypeDescriptor::VOLATILE_QUALIFIER: { // k
			auto volatile_qualifier = std::make_unique<StabsVolatileQualifierType>(info);
			
			auto type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(type);
			volatile_qualifier->type = std::move(*type);
			
			out_type = std::move(volatile_qualifier);
			break;
		}
		case StabsTypeDescriptor::CONST_QUALIFIER: { // k
			auto const_qualifier = std::make_unique<StabsConstQualifierType>(info);
			
			auto type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(type);
			const_qualifier->type = std::move(*type);
			
			out_type = std::move(const_qualifier);
			break;
		}
		case StabsTypeDescriptor::RANGE: { // r
			auto range = std::make_unique<StabsRangeType>(info);
			
			auto type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(type);
			range->type = std::move(*type);
			
			CCC_EXPECT_CHAR(input, ';', "range type descriptor");
			
			std::optional<std::string> low = eat_dodgy_stabs_identifier(input);
			CCC_CHECK(low.has_value(), "Cannot parse low part of range.");
			CCC_EXPECT_CHAR(input, ';', "low range value");
			
			std::optional<std::string> high = eat_dodgy_stabs_identifier(input);
			CCC_CHECK(high.has_value(), "Cannot parse high part of range.");
			CCC_EXPECT_CHAR(input, ';', "high range value");
			
			range->low = std::move(*low);
			range->high = std::move(*high);
			
			out_type = std::move(range);
			break;
		}
		case StabsTypeDescriptor::STRUCT: { // s
			auto struct_type = std::make_unique<StabsStructType>(info);
			STABS_DEBUG_PRINTF("struct {\n");
			
			std::optional<s64> struct_size = eat_s64_literal(input);
			CCC_CHECK(struct_size.has_value(), "Cannot parse struct size.");
			struct_type->size = *struct_size;
			
			if(*input == '!') {
				input++;
				std::optional<s32> base_class_count = eat_s32_literal(input);
				CCC_CHECK(base_class_count.has_value(), "Cannot parse base class count.");
				CCC_EXPECT_CHAR(input, ',', "base class section");
				for(s64 i = 0; i < *base_class_count; i++) {
					StabsBaseClass base_class;
					eat_char(input);
					
					std::optional<char> visibility = eat_char(input);
					CCC_CHECK(visibility.has_value(), "Cannot parse base class visibility.");
					base_class.visibility = (StabsFieldVisibility) *visibility;
					
					std::optional<s32> offset = eat_s32_literal(input);
					CCC_CHECK(offset.has_value(), "Cannot parse base class offset.");
					base_class.offset = (s32) *offset;
					
					CCC_EXPECT_CHAR(input, ',', "base class section");
					
					auto base_class_type = parse_stabs_type(input);
					CCC_RETURN_IF_ERROR(base_class_type);
					base_class.type = std::move(*base_class_type);
					
					CCC_EXPECT_CHAR(input, ';', "base class section");
					struct_type->base_classes.emplace_back(std::move(base_class));
				}
			}
			
			auto fields = parse_field_list(input);
			CCC_RETURN_IF_ERROR(fields);
			struct_type->fields = std::move(*fields);
			
			auto member_functions = parse_member_functions(input);
			CCC_RETURN_IF_ERROR(member_functions);
			struct_type->member_functions = std::move(*member_functions);
			
			STABS_DEBUG_PRINTF("}\n");
			
			out_type = std::move(struct_type);
			break;
		}
		case StabsTypeDescriptor::UNION: { // u
			auto union_type = std::make_unique<StabsUnionType>(info);
			STABS_DEBUG_PRINTF("union {\n");
			
			std::optional<s64> union_size = eat_s64_literal(input);
			CCC_CHECK(union_size.has_value(), "Cannot parse struct size.");
			union_type->size = *union_size;
			
			auto fields = parse_field_list(input);
			CCC_RETURN_IF_ERROR(fields);
			union_type->fields = std::move(*fields);
			
			auto member_functions = parse_member_functions(input);
			CCC_RETURN_IF_ERROR(member_functions);
			union_type->member_functions = std::move(*member_functions);
			
			STABS_DEBUG_PRINTF("}\n");
			
			out_type = std::move(union_type);
			break;
		}
		case StabsTypeDescriptor::CROSS_REFERENCE: { // x
			auto cross_reference = std::make_unique<StabsCrossReferenceType>(info);
			
			std::optional<char> c = eat_char(input);
			CCC_CHECK(c.has_value(), "Cannot parse cross reference type.");
			
			switch(*c) {
				case 'e': cross_reference->type = ast::ForwardDeclaredType::ENUM; break;
				case 's': cross_reference->type = ast::ForwardDeclaredType::STRUCT; break;
				case 'u': cross_reference->type = ast::ForwardDeclaredType::UNION; break;
				default:
					return CCC_FAILURE("Invalid cross reference type '%c'.", cross_reference->type);
			}
			
			std::optional<std::string> identifier = eat_dodgy_stabs_identifier(input);
			CCC_CHECK(identifier.has_value(), "Cannot parse cross reference identifier.");
			cross_reference->identifier = std::move(*identifier);
			
			cross_reference->name = cross_reference->identifier;
			CCC_EXPECT_CHAR(input, ':', "cross reference");
			
			out_type = std::move(cross_reference);
			break;
		}
		case StabsTypeDescriptor::FLOATING_POINT_BUILTIN: { // R
			auto fp_builtin = std::make_unique<StabsFloatingPointBuiltInType>(info);
			
			std::optional<s32> fpclass = eat_s32_literal(input);
			CCC_CHECK(fpclass.has_value(), "Cannot parse floating point built-in class.");
			fp_builtin->fpclass = *fpclass;
			
			CCC_EXPECT_CHAR(input, ';', "floating point builtin");
			
			std::optional<s32> bytes = eat_s32_literal(input);
			CCC_CHECK(bytes.has_value(), "Cannot parse floating point built-in.");
			fp_builtin->bytes = *bytes;
			
			CCC_EXPECT_CHAR(input, ';', "floating point builtin");
			
			std::optional<s32> value = eat_s32_literal(input);
			CCC_CHECK(value.has_value(), "Cannot parse floating point built-in.");
			
			CCC_EXPECT_CHAR(input, ';', "floating point builtin");
			
			out_type = std::move(fp_builtin);
			break;
		}
		case StabsTypeDescriptor::METHOD: { // #
			auto method = std::make_unique<StabsMethodType>(info);
			
			if(*input == '#') {
				input++;
				
				auto return_type = parse_stabs_type(input);
				CCC_RETURN_IF_ERROR(return_type);
				method->return_type = std::move(*return_type);
				
				if(*input == ';') {
					input++;
				}
			} else {
				auto class_type = parse_stabs_type(input);
				CCC_RETURN_IF_ERROR(class_type);
				method->class_type = std::move(*class_type);
				
				CCC_EXPECT_CHAR(input, ',', "method");
				
				auto return_type = parse_stabs_type(input);
				CCC_RETURN_IF_ERROR(return_type);
				method->return_type = std::move(*return_type);
				
				while(*input != '\0') {
					if(*input == ';') {
						input++;
						break;
					}
					CCC_EXPECT_CHAR(input, ',', "method");
					
					auto parameter_type = parse_stabs_type(input);
					CCC_RETURN_IF_ERROR(parameter_type);
					method->parameter_types.emplace_back(std::move(*parameter_type));
				}
			}
			
			out_type = std::move(method);
			break;
		}
		case StabsTypeDescriptor::REFERENCE: { // &
			auto reference = std::make_unique<StabsReferenceType>(info);
			
			auto value_type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(value_type);
			reference->value_type = std::move(*value_type);
			
			out_type = std::move(reference);
			break;
		}
		case StabsTypeDescriptor::POINTER: { // *
			auto pointer = std::make_unique<StabsPointerType>(info);
			
			auto value_type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(value_type);
			pointer->value_type = std::move(*value_type);
			
			out_type = std::move(pointer);
			break;
		}
		case StabsTypeDescriptor::TYPE_ATTRIBUTE: { // @
			if((*input >= '0' && *input <= '9') || *input == '(') {
				auto member_pointer = std::make_unique<StabsPointerToNonStaticDataMember>(info);
				
				auto class_type = parse_stabs_type(input);
				CCC_RETURN_IF_ERROR(class_type);
				member_pointer->class_type = std::move(*class_type);
				
				CCC_EXPECT_CHAR(input, ',', "pointer to non-static data member");
				
				auto member_type = parse_stabs_type(input);
				CCC_RETURN_IF_ERROR(member_type);
				member_pointer->member_type = std::move(*member_type);
				
				out_type = std::move(member_pointer);
			} else {
				auto type_attribute = std::make_unique<StabsSizeTypeAttributeType>(info);
				CCC_CHECK(*input == 's', "Weird value following '@' type descriptor.");
				input++;
				
				std::optional<s64> size_bits = eat_s64_literal(input);
				CCC_CHECK(size_bits.has_value(), "Cannot parse type attribute.")
				type_attribute->size_bits = *size_bits;
				CCC_EXPECT_CHAR(input, ';', "type attribute");
				
				auto type = parse_stabs_type(input);
				CCC_RETURN_IF_ERROR(type);
				type_attribute->type = std::move(*type);
				
				out_type = std::move(type_attribute);
			}
			break;
		}
		case StabsTypeDescriptor::BUILTIN: { // -
			auto built_in = std::make_unique<StabsBuiltInType>(info);
			
			std::optional<s64> type_id = eat_s64_literal(input);
			CCC_CHECK(type_id.has_value(), "Cannot parse built-in.");
			built_in->type_id = *type_id;
			
			CCC_EXPECT_CHAR(input, ';', "builtin");
			
			out_type = std::move(built_in);
			break;
		}
		default: {
			return CCC_FAILURE(
				"Invalid type descriptor '%c' (%02x).",
				(u32) descriptor, (u32) descriptor);
		}
	}
	
	return out_type;
}

static Result<std::vector<StabsField>> parse_field_list(const char*& input) {
	std::vector<StabsField> fields;
	
	while(*input != '\0') {
		if(*input == ';') {
			input++;
			break;
		}
		
		const char* before_field = input;
		StabsField field;
		
		std::optional<std::string> name = eat_dodgy_stabs_identifier(input);
		CCC_CHECK(name.has_value(), "Cannot parse field name.");
		field.name = std::move(*name);
		
		CCC_EXPECT_CHAR(input, ':', "identifier");
		if(*input == '/') {
			input++;
			
			std::optional<char> visibility = eat_char(input);
			CCC_CHECK(visibility.has_value(), "Cannot parse field visibility.");
			field.visibility = (StabsFieldVisibility) *visibility;
			
			switch(field.visibility) {
				case StabsFieldVisibility::NONE:
				case StabsFieldVisibility::PRIVATE:
				case StabsFieldVisibility::PROTECTED:
				case StabsFieldVisibility::PUBLIC:
				case StabsFieldVisibility::PUBLIC_OPTIMIZED_OUT:
					break;
				default:
					return CCC_FAILURE("invalid field visibility");
			}
		}
		if(*input == ':') {
			input = before_field;
			break;
		}
		auto type = parse_stabs_type(input);
		CCC_RETURN_IF_ERROR(type);
		field.type = std::move(*type);
		
		if(field.name.size() >= 1 && field.name[0] == '$') {
			// Virtual table pointers.
			CCC_EXPECT_CHAR(input, ',', "field type");
			
			std::optional<s32> offset_bits = eat_s32_literal(input);
			CCC_CHECK(offset_bits.has_value(), "Cannot parse field offset.");
			field.offset_bits = *offset_bits;
			
			CCC_EXPECT_CHAR(input, ';', "field offset");
		} else if(*input == ':') {
			input++;
			field.is_static = true;
			
			std::optional<std::string> type_name = eat_dodgy_stabs_identifier(input);
			CCC_CHECK(type_name.has_value(), "Cannot parse static field type name.");
			field.type_name = std::move(*type_name);
			
			CCC_EXPECT_CHAR(input, ';', "identifier");
		} else if(*input == ',') {
			input++;
			
			std::optional<s32> offset_bits = eat_s32_literal(input);
			CCC_CHECK(offset_bits.has_value(), "Cannot parse field offset.");
			field.offset_bits = *offset_bits;
			
			CCC_EXPECT_CHAR(input, ',', "field offset");
			
			std::optional<s32> size_bits = eat_s32_literal(input);
			CCC_CHECK(size_bits.has_value(), "Cannot parse field size.");
			field.size_bits = *size_bits;
			
			CCC_EXPECT_CHAR(input, ';', "field size");
		} else {
			return CCC_FAILURE("Expected ':' or ',', got '%c' (%hhx).", *input, *input);
		}

		STABS_DEBUG(print_field(field);)

		fields.emplace_back(std::move(field));
	}
	
	return fields;
}

static Result<std::vector<StabsMemberFunctionSet>> parse_member_functions(const char*& input) {
	// Check for if the next character is from an enclosing field list. If this
	// is the case, the next character will be ',' for normal fields and ':' for
	// static fields (see above).
	if(*input == ',' || *input == ':') {
		return std::vector<StabsMemberFunctionSet>();
	}
	
	std::vector<StabsMemberFunctionSet> member_functions;
	while(*input != '\0') {
		if(*input == ';') {
			input++;
			break;
		}
		StabsMemberFunctionSet member_function_set;
		
		std::optional<std::string> name = eat_stabs_identifier(input);
		CCC_CHECK(name.has_value(), "Cannot parse member function name.");
		member_function_set.name = std::move(*name);
		
		CCC_EXPECT_CHAR(input, ':', "member function");
		CCC_EXPECT_CHAR(input, ':', "member function");
		while(*input != '\0') {
			if(*input == ';') {
				input++;
				break;
			}
			
			StabsMemberFunction function;
			
			auto type = parse_stabs_type(input);
			CCC_RETURN_IF_ERROR(type);
			function.type = std::move(*type);
			
			CCC_EXPECT_CHAR(input, ':', "member function");
			std::optional<std::string> identifier = eat_dodgy_stabs_identifier(input);
			CCC_CHECK(identifier.has_value(), "Invalid member function identifier.");
			CCC_EXPECT_CHAR(input, ';', "member function");
			
			std::optional<char> visibility = eat_char(input);
			CCC_CHECK(visibility.has_value(), "Cannot parse member function visibility.");
			function.visibility = (StabsFieldVisibility) *visibility;
			
			switch(function.visibility) {
				case StabsFieldVisibility::PRIVATE:
				case StabsFieldVisibility::PROTECTED:
				case StabsFieldVisibility::PUBLIC:
				case StabsFieldVisibility::PUBLIC_OPTIMIZED_OUT:
					break;
				default:
					return CCC_FAILURE("Invalid visibility for member function.");
			}
			
			std::optional<char> modifiers = eat_char(input);
			CCC_CHECK(modifiers.has_value(), "Cannot parse member function modifiers.");
			switch(*modifiers) {
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
					return CCC_FAILURE("Invalid member function modifiers.");
			}
			
			std::optional<char> flag = eat_char(input);
			CCC_CHECK(flag.has_value(), "Cannot parse member function type.");
			switch(*flag) {
				case '.': { // normal member function
					function.modifier = ast::MemberFunctionModifier::NONE;
					break;
				}
				case '?': { // static member function
					function.modifier = ast::MemberFunctionModifier::STATIC;
					break;
				}
				case '*': { // virtual member function
					std::optional<s32> vtable_index = eat_s32_literal(input);
					CCC_CHECK(vtable_index.has_value(), "Cannot parse vtable index.");
					function.vtable_index = *vtable_index;
					
					CCC_EXPECT_CHAR(input, ';', "virtual member function");
					
					auto virtual_type = parse_stabs_type(input);
					CCC_RETURN_IF_ERROR(virtual_type);
					function.virtual_type = std::move(*virtual_type);
					
					CCC_EXPECT_CHAR(input, ';', "virtual member function");
					function.modifier = ast::MemberFunctionModifier::VIRTUAL;
					break;
				}
				default:
					return CCC_FAILURE("Invalid member function type.");
			}
			member_function_set.overloads.emplace_back(std::move(function));
		}
		STABS_DEBUG_PRINTF("member func: %s\n", member_function_set.name.c_str());
		member_functions.emplace_back(std::move(member_function_set));
	}
	return member_functions;
}

std::optional<char> eat_char(const char*& input) {
	if(*input == '\0') {
		return std::nullopt;
	}
	return *(input++);
}

std::optional<s32> eat_s32_literal(const char*& input) {
	char* end;
	s64 value = strtoll(input, &end, 10);
	if(end == input) {
		return std::nullopt;
	}
	input = end;
	return (s32) value;
}

std::optional<s64> eat_s64_literal(const char*& input) {
	char* end;
	s64 value = strtoll(input, &end, 10);
	if(end == input) {
		return std::nullopt;
	}
	input = end;
	return value;
}

std::optional<std::string> eat_stabs_identifier(const char*& input) {
	std::string identifier;
	bool first = true;
	for(; *input != '\0'; input++) {
		bool valid_char = false;
		valid_char |= *input != ':' && *input != ';';
		valid_char |= !first && isalnum(*input);
		if(valid_char) {
			identifier += *input;
		} else {
			return identifier;
		}
		first = false;
	}
	return std::nullopt;
}

// The complexity here is because the input may contain an unescaped namespace
// separator '::' even if the field terminator is supposed to be a colon.
std::optional<std::string> eat_dodgy_stabs_identifier(const char*& input) {
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
		valid_char |= (*input != ':' || template_depth != 0) && *input != ';';
		valid_char |= !first && isalnum(*input);
		if(valid_char) {
			identifier += *input;
		} else {
			return identifier;
		}
		first = false;
	}
	return std::nullopt;
}

STABS_DEBUG(

static void print_field(const StabsField& field) {
	printf("\t%04x %04x %04x %04x %s\n", field.offset_bits / 8, field.size_bits / 8, field.offset_bits, field.size_bits, field.name.c_str());
}

)

const char* stabs_field_visibility_to_string(StabsFieldVisibility visibility) {
	switch(visibility) {
		case StabsFieldVisibility::PRIVATE: return "private";
		case StabsFieldVisibility::PROTECTED: return "protected";
		case StabsFieldVisibility::PUBLIC: return "public";
		case StabsFieldVisibility::PUBLIC_OPTIMIZED_OUT: return "public_optimizedout";
		default: return "none";
	}
	return "";
}

}
