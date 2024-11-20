// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_section.h"

#include "importer_flags.h"

namespace ccc::dwarf {

#define DIE_CHECK(condition, message) \
	CCC_CHECK(condition, message " at 0x%x inside DIE at 0x%x.", offset, m_offset);
#define DIE_CHECK_ARGS(condition, message, ...) \
	CCC_CHECK(condition, message " at 0x%x inside DIE at 0x%x.", __VA_ARGS__, offset, m_offset);

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

Result<std::optional<DIE>> DIE::first_child() const
{
	u32 sibling_offset = 0;
	u32 offset = m_offset + 6;
	while (offset < m_offset + m_length) {
		Result<AttributeTuple> attribute = parse_attribute(offset);
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
		Result<AttributeTuple> attribute = parse_attribute(offset);
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

Result<void> DIE::attributes(const AttributesSpec& spec, std::vector<Value*> output) const
{
	// Parse the attributes and save the ones specified.
	u32 offset = m_offset + 6;
	while (offset < m_offset + m_length) {
		Result<AttributeTuple> attribute = parse_attribute(offset);
		CCC_RETURN_IF_ERROR(attribute);
		
		auto iterator = spec.find(attribute->attribute);
		if (iterator == spec.end()) {
			continue;
		}
		
		DIE_CHECK_ARGS(iterator->second.valid_forms & 1 << (attribute->value.form()),
			"Attribute %x has an unexpected form %s", attribute->attribute, form_to_string(attribute->value.form()));
		
		CCC_ASSERT(iterator->second.index < output.size());
		*output[iterator->second.index] = std::move(attribute->value);
	}
	
	// Check that we have all the required attributes.
	for (auto& [attribute, attribute_spec] : spec) {
		if (attribute_spec.required) {
			CCC_ASSERT(attribute_spec.index < output.size());
			CCC_CHECK(output[attribute_spec.index]->valid(),
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
		Result<AttributeTuple> attribute = parse_attribute(offset);
		CCC_RETURN_IF_ERROR(attribute);
		
		result.emplace_back(std::move(*attribute));
	}
	
	return result;
}

Result<AttributeTuple> DIE::parse_attribute(u32& offset) const
{
	AttributeTuple result;
	
	result.offset = offset;
	
	const std::optional<u16> name = copy_unaligned<u16>(m_debug, offset);
	DIE_CHECK(name.has_value(), "Cannot read attribute name");
	offset += sizeof(u16);
	
	u8 form = *name & 0xf;
	DIE_CHECK_ARGS(form_to_string(form) != nullptr, "Unknown attribute form 0x%hhx", form);
	
	u16 attribute = *name >> 4;
	bool known_attribute = attribute_to_string(attribute);
	if (!known_attribute && (m_importer_flags & STRICT_PARSING)) {
		CCC_WARN("Unknown attribute name 0x%03hx at 0x%x inside DIE at 0x%x.", *name, offset, m_offset);
	}
	
	result.attribute = static_cast<Attribute>(attribute);
	
	switch (form) {
		case FORM_ADDR: {
			std::optional<u32> address = copy_unaligned<u32>(m_debug, offset);
			DIE_CHECK(address.has_value(), "Cannot read address attribute");
			result.value = Value::from_address(*address);
			offset += sizeof(u32);
			break;
		}
		case FORM_REF: {
			std::optional<u32> reference = copy_unaligned<u32>(m_debug, offset);
			DIE_CHECK(reference.has_value(), "Cannot read reference attribute");
			result.value = Value::from_reference(*reference);
			offset += sizeof(u32);
			break;
		}
		case FORM_BLOCK2: {
			std::optional<u16> size = copy_unaligned<u16>(m_debug, offset);
			DIE_CHECK(size.has_value(), "Cannot read block attribute size");
			offset += sizeof(u16);
			
			DIE_CHECK((u64) offset + *size <= m_debug.size(), "Cannot read block attribute data");
			result.value = Value::from_block_2(m_debug.subspan(offset, *size));
			offset += *size;
			
			break;
		}
		case FORM_BLOCK4: {
			std::optional<u32> size = copy_unaligned<u32>(m_debug, offset);
			DIE_CHECK(size.has_value(), "Cannot read block attribute size");
			offset += sizeof(u32);
			
			DIE_CHECK((u64) offset + *size <= m_debug.size(), "Cannot read block attribute data");
			result.value = Value::from_block_4(m_debug.subspan(offset, *size));
			offset += *size;
			
			break;
		}
		case FORM_DATA2: {
			std::optional<u16> constant = copy_unaligned<u16>(m_debug, offset);
			DIE_CHECK(constant.has_value(), "Cannot read constant attribute");
			result.value = Value::from_constant_2(*constant);
			offset += sizeof(u16);
			break;
		}
		case FORM_DATA4: {
			std::optional<u32> constant = copy_unaligned<u32>(m_debug, offset);
			DIE_CHECK(constant.has_value(), "Cannot read constant attribute");
			result.value = Value::from_constant_4(*constant);
			offset += sizeof(u32);
			break;
		}
		case FORM_DATA8: {
			std::optional<u64> constant = copy_unaligned<u64>(m_debug, offset);
			DIE_CHECK(constant.has_value(), "Cannot read constant attribute");
			result.value = Value::from_constant_8(*constant);
			offset += sizeof(u64);
			break;
		}
		case FORM_STRING: {
			std::optional<std::string_view> string = get_string(m_debug, offset);
			DIE_CHECK(string.has_value(), "Cannot read string attribute");
			result.value = Value::from_string(*string);
			offset += static_cast<u32>(string->size()) + 1;
			break;
		}
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
		fprintf(out, "%8x:", current_die->offset());
		indent(out, depth + 1);
		
		const char* tag = tag_to_string(current_die->tag());
		if (tag) {
			fprintf(out, "%s", tag);
		} else {
			fprintf(out, "unknown(%hx)", current_die->tag());
		}
		
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
	
	for (const auto& [offset, attribute, value] : *attributes) {
		// The sibling attributes are just used to represent the structure of
		// the graph, which is displayed anyway, so skip over them for the sake
		// of readability.
		if (attribute == AT_sibling) {
			continue;
		}
		
		const char* name = attribute_to_string(attribute);
		if (name) {
			fprintf(out, " %s=", name);
		} else {
			fprintf(out, " unknown(%x)=", attribute);
		}
		
		switch (value.form()) {
			case FORM_ADDR: {
				fprintf(out, "0x%x", value.address());
				break;
			}
			case FORM_REF: {
				Result<void> print_result = print_reference(out, value.reference());
				CCC_RETURN_IF_ERROR(print_result);
				break;
			}
			case FORM_BLOCK2:
			case FORM_BLOCK4: {
				Result<void> print_result = print_block(out, offset, attribute, value);
				CCC_RETURN_IF_ERROR(print_result);
				break;
			}
			case FORM_DATA2:
			case FORM_DATA4:
			case FORM_DATA8: {
				Result<void> print_result = print_constant(out, attribute, value);
				CCC_RETURN_IF_ERROR(print_result);
				break;
			}
			case FORM_STRING: {
				fprintf(out, "\"%s\"", value.string().data());
				break;
			}
		}
	}
	fprintf(out, "\n");
	
	return Result<void>();
}

Result<void> SectionReader::print_reference(FILE* out, u32 reference) const
{
	Result<std::optional<DIE>> referenced_die = DIE::parse(m_debug, reference, NO_IMPORTER_FLAGS);
	CCC_RETURN_IF_ERROR(referenced_die);
	
	if (referenced_die->has_value()) {
		const char* referenced_die_tag = tag_to_string((*referenced_die)->tag());
		if (referenced_die_tag) {
			fprintf(out, "%s", referenced_die_tag);
		} else {
			fprintf(out, "unknown(%hx)", (*referenced_die)->tag());
		}
	} else {
		fprintf(out, "null");
	}
	
	fprintf(out, "@%x", reference);
	
	return Result<void>();
}

Result<void> SectionReader::print_block(FILE* out, u32 offset, Attribute attribute, const Value& value) const
{
	std::span<const u8> block = value.block();
	
	switch (attribute) {
		case AT_location: {
			LocationDescription location(value.block());
			Result<void> print_result = location.print(out);
			CCC_RETURN_IF_ERROR(print_result);
			break;
		}
		case AT_mod_fund_type: {
			Type type = Type::from_mod_fund_type(value);
			Result<void> print_result = print_type(out, type);
			CCC_RETURN_IF_ERROR(print_result);
			break;
		}
		case AT_mod_u_d_type: {
			Type type = Type::from_mod_u_d_type(value);
			Result<void> print_result = print_type(out, type);
			CCC_RETURN_IF_ERROR(print_result);
			break;
		}
		default: {
			fprintf(out, "{");
			
			size_t max_bytes_to_display = 3;
			
			for (size_t i = 0; i < std::min(block.size(), max_bytes_to_display); i++) {
				if (i != 0) {
					fprintf(out, ",");
				}
				fprintf(out, "%02hhx", block[i]);
			}
			
			if (block.size() > max_bytes_to_display) {
				fprintf(out, ",...");
			}
			
			fprintf(out, "}@%x", offset);
		}
	}
	
	return Result<void>();
}

Result<void> SectionReader::print_constant(FILE* out, Attribute attribute, const Value& value) const
{
	switch (attribute) {
		case AT_fund_type: {
			Type type = Type::from_fund_type(value);
			Result<void> print_result = print_type(out, type);
			CCC_RETURN_IF_ERROR(print_result);
			break;
		}
		case AT_user_def_type: {
			Type type = Type::from_user_def_type(value);
			Result<void> print_result = print_type(out, type);
			CCC_RETURN_IF_ERROR(print_result);
			break;
		}
		default: {
			fprintf(out, "0x%llx", (long long) value.constant());
		}
	}
	
	return Result<void>();
}

Result<void> SectionReader::print_type(FILE* out, const Type& type) const
{
	Result<std::span<const TypeModifier>> mods = type.modifiers();
	CCC_RETURN_IF_ERROR(mods);
	
	if (!mods->empty()) {
		fprintf(out, "{");
	}
	
	for (TypeModifier mod : *mods) {
		fprintf(out, "%s,", type_modifier_to_string(mod));
	}
	
	switch (type.attribute()) {
		case AT_fund_type:
		case AT_mod_fund_type: {
			Result<FundamentalType> fund_type = type.fund_type();
			CCC_RETURN_IF_ERROR(fund_type);
			
			fprintf(out, "%s", fundamental_type_to_string(*fund_type));
			
			break;
		}
		case AT_user_def_type:
		case AT_mod_u_d_type: {
			Result<u32> reference = type.user_def_type();
			CCC_RETURN_IF_ERROR(reference);
			
			Result<void> print_result = print_reference(out, *reference);
			CCC_RETURN_IF_ERROR(print_result);
			
			break;
		}
	}
	
	if (!mods->empty()) {
		fprintf(out, "}");
	}
	
	return Result<void>();
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
