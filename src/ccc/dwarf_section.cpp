// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_section.h"

#include "importer_flags.h"

namespace ccc::dwarf {

Result<std::optional<DIE>> DIE::parse(std::span<const u8> debug, u32 offset, u32 importer_flags)
{
	DIE die;
	
	die.m_debug = debug;
	die.m_offset = offset;
	
	std::optional<u32> length = copy_unaligned<u32>(debug, offset);
	if (!length.has_value()) {
		return std::optional<DIE>(std::nullopt);
	}
	die.m_length = *length;
	offset += sizeof(u32);
	
	if (die.m_length < 8) {
		return std::optional<DIE>(std::nullopt);
	}
	
	std::optional<u16> tag = copy_unaligned<u16>(debug, offset);
	CCC_CHECK(tag.has_value(), "Cannot read tag for die at 0x%x.", offset);
	CCC_CHECK(tag_to_string(*tag), "Unknown tag 0x%hx for die at 0x%x.", *tag, offset);
	die.m_tag = static_cast<Tag>(*tag);
	offset += sizeof(u16);
	
	die.m_importer_flags = importer_flags;
	
	return std::optional<DIE>(die);
}

AttributeListFormat DIE::attribute_list_format(std::vector<AttributeFormat> input)
{
	AttributeListFormat output;
	
	for (u32 i = 0; i < static_cast<u32>(input.size()); i++) {
		AttributeFormat& attribute = output.emplace(input[i].attribute, input[i]).first->second;
		attribute.index = i;
	}
	
	return output;
}

AttributeFormat DIE::attribute_format(Attribute attribute, std::vector<u32> valid_forms, u32 flags)
{
	AttributeFormat result;
	result.attribute = attribute;
	result.valid_forms = 0;
	for (u32 form : valid_forms) {
		result.valid_forms |= 1 << form;
	}
	result.flags = flags;
	return result;
}

Result<std::optional<DIE>> DIE::first_child() const
{
	u32 sibling_offset = 0;
	u32 offset = m_offset + 6;
	while (offset < m_offset + m_length) {
		Result<AttributeTuple> attribute = parse_attribute(m_debug, offset, m_importer_flags);
		CCC_RETURN_IF_ERROR(attribute);
		
		if (attribute->attribute == AT_sibling && attribute->value.form() == FORM_REF) {
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
		Result<AttributeTuple> attribute = parse_attribute(m_debug, offset, m_importer_flags);
		CCC_RETURN_IF_ERROR(attribute);
		
		if (attribute->attribute == AT_sibling && attribute->value.form() == FORM_REF) {
			// Prevent infinite recursion if the file contains a cycle.
			CCC_CHECK(attribute->value.reference() > m_offset,
				"Sibling attribute of DIE at 0x%x points backwards.", m_offset);
			return DIE::parse(m_debug, attribute->value.reference(), m_importer_flags);
		}
	}
	
	return std::optional<DIE>(std::nullopt);
}

u32 DIE::offset() const
{
	return m_offset;
}

Tag DIE::tag() const
{
	return m_tag;
}

Result<void> DIE::scan_attributes(const AttributeListFormat& format, std::initializer_list<Value*> output) const
{
	u32 offset = m_offset + 6;
	while (offset < m_offset + m_length) {
		u32 attribute_offset = offset;
		Result<AttributeTuple> attribute = parse_attribute(m_debug, offset, m_importer_flags);
		CCC_RETURN_IF_ERROR(attribute);
		
		auto iterator = format.find(attribute->attribute);
		if (iterator == format.end()) {
			continue;
		}
		
		CCC_CHECK(iterator->second.valid_forms & 1 << (attribute->value.form()),
			"Attribute %x at 0x%x has an unexpected form %s.",
			attribute->attribute, attribute_offset, form_to_string(attribute->value.form()));
		
		CCC_ASSERT(iterator->second.index < output.size());
		**(output.begin() + iterator->second.index) = std::move(attribute->value);
	}
	
	// Check that we have all the required attributes.
	for (auto& [attribute, attribute_format] : format) {
		if (attribute_format.flags & AFF_REQUIRED) {
			CCC_ASSERT(attribute_format.index < output.size());
			CCC_CHECK((*(output.begin() + attribute_format.index))->valid(),
				"Missing %s attribute for DIE at 0x%x\n", attribute_to_string(attribute), m_offset);
		}
	}
	
	return Result<void>();
}

Result<std::vector<AttributeTuple>> DIE::all_attributes() const
{
	std::vector<AttributeTuple> result;
	
	u32 offset = m_offset + 6;
	while (offset < m_offset + m_length) {
		Result<AttributeTuple> attribute = parse_attribute(m_debug, offset, m_importer_flags);
		CCC_RETURN_IF_ERROR(attribute);
		
		result.emplace_back(std::move(*attribute));
	}
	
	return result;
}

// *****************************************************************************

SectionReader::SectionReader(std::span<const u8> debug, std::span<const u8> line, u32 importer_flags)
	: m_debug(debug), m_line(line), m_importer_flags(importer_flags) {}
	
Result<DIE> SectionReader::first_die() const
{
	Result<std::optional<DIE>> die = DIE::parse(m_debug, 0, m_importer_flags);
	CCC_RETURN_IF_ERROR(die);
	CCC_CHECK(die->has_value(), "DIE at offset 0x0 is null.");
	return **die;
}

Result<std::optional<DIE>> SectionReader::die_at(u32 offset) const
{
	return DIE::parse(m_debug, offset, m_importer_flags);
}

u32 SectionReader::importer_flags() const
{
	return m_importer_flags;
}

// *****************************************************************************

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
		case TAG_overlay: return "overlay";
		case TAG_format_label: return "format_label";
		case TAG_namelist: return "namelist";
		case TAG_function_template: return "function_template";
		case TAG_class_template: return "class_template";
	}
	
	return nullptr;
}

}
