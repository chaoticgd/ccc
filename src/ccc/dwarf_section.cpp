// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_section.h"

#include "importer_flags.h"

namespace ccc::dwarf {

Value::Value() = default;

Value::Value(const Value& rhs)
{
	memcpy(this, &rhs, sizeof(Value));
}

Value::~Value() = default;

Value& Value::operator=(const Value& rhs)
{
	memcpy(this, &rhs, sizeof(Value));
	return *this;
}

bool Value::valid() const
{
	return form_to_string(m_form) != nullptr;
}

Value Value::from_address(u32 address)
{
	Value result;
	result.m_form = FORM_ADDR;
	result.m_value.address = address;
	return result;
}

Value Value::from_reference(u32 reference)
{
	Value result;
	result.m_form = FORM_REF;
	result.m_value.reference = reference;
	return result;
}

Value Value::from_constant(u64 constant)
{
	Value result;
	result.m_form = FORM_DATA8;
	result.m_value.constant = constant;
	return result;
}

Value Value::from_block(std::span<const u8> block)
{
	Value result;
	result.m_form = FORM_BLOCK4;
	result.m_value.block.begin = block.data();
	result.m_value.block.end = block.data() + block.size();
	return result;
}

Value Value::from_string(const char* string)
{
	Value result;
	result.m_form = FORM_STRING;
	result.m_value.string = string;
	return result;
}

u32 Value::address() const
{
	CCC_ASSERT(m_form == FORM_ADDR);
	return m_value.address;
}

u32 Value::reference() const
{
	CCC_ASSERT(m_form == FORM_REF);
	return m_value.reference;
}

u64 Value::constant() const
{
	CCC_ASSERT(m_form == FORM_DATA8);
	return m_value.constant;
}

std::span<const u8> Value::block() const
{
	CCC_ASSERT(m_form == FORM_BLOCK4);
	return std::span<const u8>(m_value.block.begin, m_value.block.end);
}

const char* Value::string() const
{
	CCC_ASSERT(m_form == FORM_STRING);
	return m_value.string;
}

// *****************************************************************************

Result<std::optional<DIE>> DIE::parse(std::span<const u8> debug, u32 offset, u32 importer_flags)
{
	DIE die;
	
	die.m_debug = debug;
	die.m_offset = offset;
	
	std::optional<u32> length = copy_unaligned<u32>(debug, offset);
	CCC_CHECK(length.has_value(), "Cannot read length for die at 0x%x.", offset);
	die.m_length = *length;
	offset += sizeof(u32);
	
	if (die.m_length < 8) {
		return std::optional<DIE>(std::nullopt);
	}
	
	std::optional<u16> tag = copy_unaligned<u16>(debug, offset);
	CCC_CHECK(tag.has_value(), "Cannot read tag for die at 0x%x.", offset);
	die.m_tag = static_cast<Tag>(*tag);
	offset += sizeof(u16);
	
	die.m_importer_flags = importer_flags;
	
	return std::optional<DIE>(die);
}

RequiredAttributes DIE::require_attributes(std::span<const RequiredAttribute> input)
{
	RequiredAttributes output;
	
	for (u32 i = 0; i < static_cast<u32>(input.size()); i++) {
		RequiredAttribute& attribute = output.emplace(input[i].attribute, input[i]).first->second;
		attribute.index = i;
	}
	
	return output;
}

Result<std::optional<DIE>> DIE::first_child() const
{
	u32 sibling_offset = 0;
	u32 offset = m_offset + 6;
	while (offset < m_offset + m_length) {
		Result<AttributeTuple> attribute = parse_attribute(offset);
		CCC_RETURN_IF_ERROR(attribute);
		
		if (attribute->attribute == AT_sibling && attribute->form == FORM_REF) {
			sibling_offset = attribute->value.reference();
		}
	}
	
	if (m_offset + m_length == sibling_offset) {
		return std::optional<DIE>(std::nullopt);
	}
	
	return DIE::parse(m_debug, m_offset + m_length, m_importer_flags);
}

Result<std::optional<DIE>> DIE::sibling() const
{
	u32 offset = m_offset + 6;
	while (offset < m_offset + m_length) {
		Result<AttributeTuple> attribute = parse_attribute(offset);
		CCC_RETURN_IF_ERROR(attribute);
		
		if (attribute->attribute == AT_sibling && attribute->form == FORM_REF) {
			return DIE::parse(m_debug, attribute->value.reference(), m_importer_flags);
		}
	}
	
	return std::optional<DIE>(std::nullopt);
}

Tag DIE::tag() const
{
	return m_tag;
}

Result<void> DIE::attributes(std::span<Value*> output, const RequiredAttributes& required) const
{
	u32 offset = m_offset + 6;
	while (offset < m_offset + m_length) {
		Result<AttributeTuple> attribute = parse_attribute(offset);
		CCC_RETURN_IF_ERROR(attribute);
		
		auto iterator = required.find(attribute->attribute);
		if (iterator == required.end()) {
			continue;
		}
		
		CCC_CHECK(iterator->second.valid_forms & 1 << (attribute->form),
			"Attribute %s has an unexpected form %s.",
			form_to_string(attribute->form),
			attribute_to_string(attribute->attribute));
		
		*output[iterator->second.index] = std::move(attribute->value);
	}
	
	return Result<void>();
}

Result<std::vector<AttributeTuple>> DIE::all_attributes() const
{
	std::vector<AttributeTuple> result;
	
	u32 offset = m_offset + 6;
	while (offset < m_offset + m_length) {
		Result<AttributeTuple> attribute = parse_attribute(offset);
		CCC_RETURN_IF_ERROR(attribute);
		
		result.emplace_back(std::move(*attribute));
	}
	
	return result;
}

#define ATTRIBUTE_PARSER_CHECK(condition, message) \
	CCC_CHECK(condition, message " at 0x%x inside DIE at 0x%x.", offset, m_offset);
#define ATTRIBUTE_PARSER_CHECK_ARG(condition, message, arg) \
	CCC_CHECK(condition, message " at 0x%x inside DIE at 0x%x.", arg, offset, m_offset);

Result<AttributeTuple> DIE::parse_attribute(u32& offset) const
{
	AttributeTuple result;
	
	const std::optional<u16> name = copy_unaligned<u16>(m_debug, offset);
	ATTRIBUTE_PARSER_CHECK(name.has_value(), "Cannot read attribute name");
	offset += sizeof(u16);
	
	u8 form = *name & 0xf;
	ATTRIBUTE_PARSER_CHECK_ARG(form_to_string(form) != nullptr, "Unknown attribute form 0x%hhx", form);
	
	u16 attribute = *name >> 4;
	bool known_attribute = attribute_to_string(attribute);
	if (!known_attribute) {
		const char* uknown_attribute_error_message =
			"Unknown user attribute name 0x%03hx at 0x%x inside DIE at 0x%x.";
		if ((m_importer_flags & STRICT_PARSING) == 0 && attribute >= AT_lo_user && attribute <= AT_hi_user) {
			CCC_WARN(uknown_attribute_error_message, *name, offset, m_offset);
		} else {
			return CCC_FAILURE(uknown_attribute_error_message, *name, offset, m_offset);
		}
	}
	
	result.form = static_cast<Form>(form);
	result.attribute = static_cast<Attribute>(attribute);
	
	switch (form) {
		case FORM_ADDR: {
			std::optional<u32> address = copy_unaligned<u32>(m_debug, offset);
			ATTRIBUTE_PARSER_CHECK(address.has_value(), "Cannot read address attribute");
			result.value = Value::from_address(*address);
			offset += sizeof(u32);
			break;
		}
		case FORM_REF: {
			std::optional<u32> reference = copy_unaligned<u32>(m_debug, offset);
			ATTRIBUTE_PARSER_CHECK(reference.has_value(), "Cannot read reference attribute");
			result.value = Value::from_reference(*reference);
			offset += sizeof(u32);
			break;
		}
		case FORM_BLOCK2: {
			std::optional<u16> size = copy_unaligned<u16>(m_debug, offset);
			ATTRIBUTE_PARSER_CHECK(size.has_value(), "Cannot read block attribute size");
			offset += sizeof(u16);
			
			ATTRIBUTE_PARSER_CHECK((u64) offset + *size <= m_debug.size(), "Cannot read block attribute data");
			result.value = Value::from_block(m_debug.subspan(offset, *size));
			offset += *size;
			
			break;
		}
		case FORM_BLOCK4: {
			std::optional<u32> size = copy_unaligned<u32>(m_debug, offset);
			ATTRIBUTE_PARSER_CHECK(size.has_value(), "Cannot read block attribute size");
			offset += sizeof(u32);
			
			ATTRIBUTE_PARSER_CHECK((u64) offset + *size <= m_debug.size(), "Cannot read block attribute data");
			result.value = Value::from_block(m_debug.subspan(offset, *size));
			offset += *size;
			
			break;
		}
		case FORM_DATA2: {
			std::optional<u16> constant = copy_unaligned<u16>(m_debug, offset);
			ATTRIBUTE_PARSER_CHECK(constant.has_value(), "Cannot read constant attribute");
			result.value = Value::from_constant(*constant);
			offset += sizeof(u16);
			break;
		}
		case FORM_DATA4: {
			std::optional<u32> constant = copy_unaligned<u32>(m_debug, offset);
			ATTRIBUTE_PARSER_CHECK(constant.has_value(), "Cannot read constant attribute");
			result.value = Value::from_constant(*constant);
			offset += sizeof(u32);
			break;
		}
		case FORM_DATA8: {
			std::optional<u64> constant = copy_unaligned<u64>(m_debug, offset);
			ATTRIBUTE_PARSER_CHECK(constant.has_value(), "Cannot read constant attribute");
			result.value = Value::from_constant(*constant);
			offset += sizeof(u64);
			break;
		}
		case FORM_STRING: {
			const char* string = get_string(m_debug, offset);
			ATTRIBUTE_PARSER_CHECK(string, "Cannot read string attribute");
			result.value = Value::from_string(string);
			offset += strlen(string) + 1;
			break;
		}
	}
	
	return result;
}


// *****************************************************************************

SectionReader::SectionReader(std::span<const u8> debug, std::span<const u8> line)
	: m_debug(debug), m_line(line) {}
	
Result<DIE> SectionReader::first_die(u32 importer_flags) const
{
	Result<std::optional<DIE>> die = DIE::parse(m_debug, 0, importer_flags);
	CCC_RETURN_IF_ERROR(die);
	CCC_CHECK(die->has_value(), "DIE at offset 0x0 is null.");
	return **die;
}

static void indent(FILE* out, s32 depth)
{
	for (s32 i = 0; i < depth; i++) {
		fputc('\t', out);
	}
}

Result<void> SectionReader::print_dies(FILE* out, DIE die, s32 depth) const
{
	std::optional<DIE> current_die = std::move(die);
	
	while (current_die.has_value()) {
		indent(out, depth);
		fprintf(out, "%s ", tag_to_string(current_die->tag()));
		
		Result<void> result = print_attributes(out, *current_die);
		CCC_RETURN_IF_ERROR(result);
		
		Result<std::optional<DIE>> child = current_die->first_child();
		CCC_RETURN_IF_ERROR(child);
		
		if (*child != std::nullopt) {
			Result<void> child_result = print_dies(out, **child, depth + 1);
			CCC_RETURN_IF_ERROR(child_result);
		}
		
		Result<std::optional<DIE>> next = current_die->sibling();
		CCC_RETURN_IF_ERROR(next);
		current_die = *next;
	}
	
	return Result<void>();
}

Result<void> SectionReader::print_attributes(FILE* out, const DIE& die) const
{
	Result<std::vector<AttributeTuple>> attributes = die.all_attributes();
	CCC_RETURN_IF_ERROR(attributes);
	
	for (const auto& [attribute, form, value] : *attributes) {
		fprintf(out, "%s=", attribute_to_string(attribute));
		switch (form) {
			case FORM_ADDR: fprintf(out, "0x%x", value.address()); break;
			case FORM_REF: fprintf(out, "DIE@0x%x", value.reference()); break;
			case FORM_BLOCK2: fprintf(out, "(block2)"); break;
			case FORM_BLOCK4: fprintf(out, "(block4)"); break;
			case FORM_DATA2: fprintf(out, "0x%hx", (short) value.constant()); break;
			case FORM_DATA4: fprintf(out, "0x%x", (int) value.constant()); break;
			case FORM_DATA8: fprintf(out, "0x%llx", (long long) value.constant()); break;
			case FORM_STRING: fprintf(out, "\"%s\"", value.string()); break;
		}
		fprintf(out, " ");
	}
	fprintf(out, "\n");
	
	return Result<void>();
}

const char* tag_to_string(u32 tag)
{
	switch (tag) {
		case TAG_padding: return "padding";
		case TAG_array_type: return "array_type";
		case TAG_class_type: return "class_type";
		case TAG_entry_point: return "entry_point";
		case TAG_enumeration_type: return "enumeration_type";
		case TAG_formal_parameter: return "formal_parameter";
		case TAG_global_subroutine: return "global_subroutine";
		case TAG_global_variable: return "global_variable";
		case TAG_label: return "label";
		case TAG_lexical_block: return "lexical_block";
		case TAG_local_variable: return "local_variable";
		case TAG_member: return "member";
		case TAG_pointer_type: return "pointer_type";
		case TAG_reference_type: return "reference_type";
		case TAG_compile_unit: return "compile_unit";
		case TAG_string_type: return "string_type";
		case TAG_structure_type: return "structure_type";
		case TAG_subroutine: return "subroutine";
		case TAG_subroutine_type: return "subroutine_type";
		case TAG_typedef: return "typedef";
		case TAG_union_type: return "union_type";
		case TAG_unspecified_parameters: return "unspecified_parameters";
		case TAG_variant: return "variant";
		case TAG_common_block: return "common_block";
		case TAG_common_inclusion: return "common_inclusion";
		case TAG_inheritance: return "inheritance";
		case TAG_inlined_subroutine: return "inlined_subroutine";
		case TAG_module: return "module";
		case TAG_ptr_to_member_type: return "ptr_to_member_type";
		case TAG_set_type: return "set_type";
		case TAG_subrange_type: return "subrange_type";
		case TAG_with_stmt: return "with_stmt";
		case TAG_format_label: return "format_label";
		case TAG_namelist: return "namelist";
		case TAG_function_template: return "function_template";
		case TAG_class_template: return "class_template";
	}
	
	return "unknown";
}

const char* form_to_string(u32 form)
{
	switch (form) {
		case FORM_ADDR: return "addr";
		case FORM_REF: return "ref";
		case FORM_BLOCK2: return "block2";
		case FORM_BLOCK4: return "block4";
		case FORM_DATA2: return "data2";
		case FORM_DATA4: return "data4";
		case FORM_DATA8: return "data8";
		case FORM_STRING: return "string";
	}
	
	return nullptr;
}

const char* attribute_to_string(u32 attribute)
{
	switch (attribute) {
		case AT_sibling: return "sibling";
		case AT_location: return "location";
		case AT_name: return "name";
		case AT_fund_type: return "fund_type";
		case AT_mod_fund_type: return "mod_fund_type";
		case AT_user_def_type: return "user_def_type";
		case AT_mod_u_d_type: return "mod_u_d_type";
		case AT_ordering: return "ordering";
		case AT_subscr_data: return "subscr_data";
		case AT_byte_size: return "byte_size";
		case AT_bit_offset: return "bit_offset";
		case AT_bit_size: return "bit_size";
		case AT_element_list: return "element_list";
		case AT_stmt_list: return "stmt_list";
		case AT_low_pc: return "low_pc";
		case AT_high_pc: return "high_pc";
		case AT_language: return "language";
		case AT_member: return "member";
		case AT_discr: return "discr";
		case AT_discr_value: return "discr_value";
		case AT_string_length: return "string_length";
		case AT_common_reference: return "common_reference";
		case AT_comp_dir: return "comp_dir";
		case AT_const_value: return "const_value";
		case AT_containing_type: return "containing_type";
		case AT_default_value: return "default_value";
		case AT_friends: return "friends";
		case AT_inline: return "inline";
		case AT_is_optional: return "is_optional";
		case AT_lower_bound: return "lower_bound";
		case AT_program: return "program";
		case AT_private: return "private";
		case AT_producer: return "producer";
		case AT_protected: return "protected";
		case AT_prototyped: return "prototyped";
		case AT_public: return "public";
		case AT_pure_virtual: return "pure_virtual";
		case AT_return_addr: return "return_addr";
		case AT_specification: return "specification";
		case AT_start_scope: return "start_scope";
		case AT_stride_size: return "stride_size";
		case AT_upper_bound: return "upper_bound";
		case AT_virtual: return "virtual";
	}
	
	return nullptr;
}

}
