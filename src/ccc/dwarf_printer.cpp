// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_printer.h"

namespace ccc::dwarf {

static void indent(FILE* out, s32 depth)
{
	for (s32 i = 0; i < depth; i++) {
		fputc('\t', out);
	}
}

SymbolPrinter::SymbolPrinter(SectionReader& reader)
	: m_reader(reader) {}

Result<void> SymbolPrinter::print_dies(FILE* out, DIE die, s32 depth) const
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

Result<void> SymbolPrinter::print_attributes(FILE* out, const DIE& die) const
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

Result<void> SymbolPrinter::print_reference(FILE* out, u32 reference) const
{
	Result<std::optional<DIE>> referenced_die = m_reader.die_at(reference);
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

Result<void> SymbolPrinter::print_block(FILE* out, u32 offset, Attribute attribute, const Value& value) const
{
	std::span<const u8> block = value.block();
	
	switch (attribute) {
		case AT_location: {
			LocationDescription location = LocationDescription::from_block(value.block());
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
		case AT_subscr_data: {
			ArraySubscriptData subscript_data = ArraySubscriptData::from_block(value.block());
			Result<void> print_result = print_subscr_data(out, subscript_data);
			CCC_RETURN_IF_ERROR(print_result);
			break;
		}
		case AT_element_list: {
			EnumerationElementList element_list = EnumerationElementList::from_block(value.block());
			Result<void> print_result = print_enumeration_element_list(out, element_list);
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

Result<void> SymbolPrinter::print_constant(FILE* out, Attribute attribute, const Value& value) const
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
		case AT_ordering: {
			const char* ordering = array_ordering_to_string(value.constant());
			CCC_CHECK(ordering, "Unknown array ordering 0x%x.\n", static_cast<u32>(value.constant()));
			fprintf(out, "%s", ordering);
			break;
		}
		case AT_language: {
			const char* language = language_to_string(value.constant());
			CCC_CHECK(language, "Unknown language 0x%x.", static_cast<u32>(value.constant()));
			fprintf(out, "%s", language);
			break;
		}
		default: {
			fprintf(out, "0x%llx", static_cast<long long>(value.constant()));
		}
	}
	
	return Result<void>();
}

Result<void> SymbolPrinter::print_type(FILE* out, const Type& type) const
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

Result<void> SymbolPrinter::print_subscr_data(FILE* out, const ArraySubscriptData& subscript_data) const
{
	fprintf(out, "{");
	
	u32 offset = 0;
	while (offset < subscript_data.size()) {
		if (offset > 0) {
			fprintf(out, ",");
		}
		
		Result<ArraySubscriptItem> subscript = subscript_data.parse_subscript(offset, m_reader.importer_flags());
		CCC_RETURN_IF_ERROR(subscript);
		
		if (subscript->specifier == FMT_ET) {
			Result<void> type_result = print_type(out, subscript->element_type);
			CCC_RETURN_IF_ERROR(type_result);
		} else {
			fprintf(out, "[");
			
			Result<void> type_result = print_type(out, subscript->subscript_index_type);
			CCC_RETURN_IF_ERROR(type_result);
			
			fprintf(out, ",");
			
			Result<void> lower_result = subscript->lower_bound.print(out);
			CCC_RETURN_IF_ERROR(lower_result);
			
			fprintf(out, ",");
			
			Result<void> upper_result = subscript->upper_bound.print(out);
			CCC_RETURN_IF_ERROR(upper_result);
			
			fprintf(out, "]");
		}
	}
	
	fprintf(out, "}");
	
	return Result<void>();
}

Result<void> SymbolPrinter::print_enumeration_element_list(FILE* out, const EnumerationElementList& element_list) const
{
	fprintf(out, "{");
	
	u32 offset = 0;
	while (offset < element_list.size()) {
		if (offset > 0) {
			fprintf(out, ",");
		}
		
		Result<EnumerationElement> element = element_list.parse_element(offset);
		CCC_RETURN_IF_ERROR(element);
		
		fprintf(out, "%s=%d", element->name.c_str(), element->value);
	}
	
	fprintf(out, "}");
	
	return Result<void>();
}

}
