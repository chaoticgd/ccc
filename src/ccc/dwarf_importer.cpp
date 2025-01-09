// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_importer.h"

#include "dwarf_to_ast.h"

namespace ccc::dwarf {

static Result<std::map<u32, u32>> parse_overlays(SymbolDatabase& database, const DIE& first_die);
static std::string get_name(const Value& name, const Value& mangled_name);

SymbolTableImporter::SymbolTableImporter(
	SymbolDatabase& database,
	const SectionReader& dwarf,
	u32 importer_flags,
	const DemanglerFunctions& demangler,
	const std::atomic_bool* interrupt)
	: m_database(database)
	, m_dwarf(dwarf)
	, m_importer_flags(importer_flags)
	, m_demangler(demangler)
	, m_interrupt(interrupt) {}

Result<void> SymbolTableImporter::import_symbol_table(SymbolGroup group)
{
	return import_compile_units(std::nullopt, group);
}

Result<void> SymbolTableImporter::import_overlay(u32 overlay_id, SymbolGroup group)
{
	return import_compile_units(overlay_id, group);
}

Result<void> SymbolTableImporter::import_compile_units(std::optional<u32> overlay_id, SymbolGroup group)
{
	Result<DIE> first_die = m_dwarf.first_die();
	CCC_RETURN_IF_ERROR(first_die);
	
	Result<std::map<u32, u32>> compile_unit_offset_to_overlay_id = parse_overlays(m_database, *first_die);
	CCC_RETURN_IF_ERROR(compile_unit_offset_to_overlay_id);
	
	m_group = group;
	m_source_file = nullptr;
	
	Result<void> reference_counts_result = compute_reference_counts(*first_die, false);
	CCC_RETURN_IF_ERROR(reference_counts_result);
	
	std::optional<DIE> die = *first_die;
	while (die.has_value()) {
		CCC_CHECK(!m_interrupt || !*m_interrupt, "Operation interrupted by user.");
		
		bool process_compile_unit = false;
		if (die->tag() == TAG_compile_unit) {
			auto overlay_iterator = compile_unit_offset_to_overlay_id->find(die->offset());
			if (overlay_iterator != compile_unit_offset_to_overlay_id->end()) {
				process_compile_unit = overlay_id.has_value() && overlay_iterator->second == *overlay_id;
			} else {
				process_compile_unit = !overlay_id.has_value();
			}
		}
		
		if (process_compile_unit) {
			Result<void> compile_unit_result = import_compile_unit(*die);
			CCC_RETURN_IF_ERROR(compile_unit_result);
		}
		
		Result<std::optional<DIE>> next_die = die->sibling();
		CCC_RETURN_IF_ERROR(next_die);
		die = *next_die;
	}
	
	return Result<void>();
}

static const AttributeListFormat compile_unit_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_name, {FORM_STRING}),
	DIE::attribute_format(AT_producer, {FORM_STRING}),
	DIE::attribute_format(AT_language, {FORM_DATA4}),
	DIE::attribute_format(AT_stmt_list, {FORM_DATA4}),
	DIE::attribute_format(AT_low_pc, {FORM_ADDR}),
	DIE::attribute_format(AT_high_pc, {FORM_ADDR})
});

Result<void> SymbolTableImporter::import_compile_unit(const DIE& die)
{
	Value name;
	Value producer;
	Value language;
	Value stmt_list;
	Value low_pc;
	Value high_pc;
	Result<void> attribute_result = die.scan_attributes(
		compile_unit_attributes, {&name, &producer, &language, &stmt_list, &low_pc, &high_pc});
	CCC_RETURN_IF_ERROR(attribute_result);
	
	// The Metrowerks compiler outputs multiple compile_unit DIEs for a single
	// logical source file, so we need to deduplicate them here.
	if (!m_source_file || m_source_file->name() != name.string_or_null()) {
		Result<SourceFile*> new_source_file = m_database.source_files.create_symbol(
			std::string(name.string_or_null()), Address(), m_group.source, m_group.module_symbol);
		CCC_RETURN_IF_ERROR(new_source_file);
		m_source_file = *new_source_file;
	}
	
	// Each individual compile_unit DIE seems to either correspond to a
	// collection of types or a single function, so we make the source file's
	// address and size cover all the low_pc/high_pc pairs.
	if (low_pc.valid() && high_pc.valid()) {
		if (!m_source_file->address().valid()) {
			m_database.source_files.move_symbol(m_source_file->handle(), low_pc.address());
			m_source_file->set_size(high_pc.address() - low_pc.address());
		}
		
		if (m_source_file->address().value > low_pc.address()) {
			u32 new_size = m_source_file->size() + m_source_file->address().value - low_pc.address();
			m_database.source_files.move_symbol(m_source_file->handle(), low_pc.address());
			m_source_file->set_size(new_size);
		}
		
		if (high_pc.address() > m_source_file->address().value + m_source_file->size()) {
			m_source_file->set_size(high_pc.address() - m_source_file->address().value);
		}
	}
	
	Result<std::optional<DIE>> first_child = die.first_child();
	CCC_RETURN_IF_ERROR(first_child);
	
	std::optional<DIE> child = *first_child;
	while (child.has_value()) {
		if (die_is_type(*child)) {
			Result<void> data_type_result = import_data_type(*child);
			CCC_RETURN_IF_ERROR(data_type_result);
		} else {
			switch (child->tag()) {
				case TAG_global_subroutine:
				case TAG_subroutine: {
					Result<void> subroutine_result = import_subroutine(*child);
					CCC_RETURN_IF_ERROR(subroutine_result);
					break;
				}
				default: {}
			}
		}
		
		Result<std::optional<DIE>> next_child = child->sibling();
		CCC_RETURN_IF_ERROR(next_child);
		child = *next_child;
	}
	
	return Result<void>();
}

static const AttributeListFormat type_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_name, {FORM_STRING})
});

Result<void> SymbolTableImporter::import_data_type(const DIE& die)
{
	Value name;
	Result<void> attribute_result = die.scan_attributes(
		type_attributes, {&name, &name});
	CCC_RETURN_IF_ERROR(attribute_result);
	
	ReferenceCounts& counts = m_die_reference_counts[die.offset()];
	
	// If the type doesn't have a name, and is referenced exactly once from
	// another type we can conclude that it is probably a component of another
	// type, and hence we don't have to emit it separately.
	if (!name.valid() && counts.references_from_types == 1 && counts.references_not_from_types == 0) {
		return Result<void>();
	}
	
	TypeImporter type_importer(m_database, m_dwarf, m_group, m_importer_flags, m_die_reference_counts);
	
	Result<std::unique_ptr<ast::Node>> node = type_importer.die_to_ast(die);
	CCC_RETURN_IF_ERROR(node);
	
	std::string symbol_name = name.valid() ? std::string(name.string()) : std::string("unnamed");
	
	Result<DataType*> data_type = m_database.data_types.create_symbol(symbol_name, m_group.source, m_group.module_symbol);
	CCC_RETURN_IF_ERROR(data_type);
	
	(*data_type)->set_type(std::move(*node));
	
	return Result<void>();
}

static const AttributeListFormat subroutine_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_name, {FORM_STRING}),
	DIE::attribute_format(AT_mangled_name, {FORM_STRING}),
	DIE::attribute_format(AT_low_pc, {FORM_ADDR}),
	DIE::attribute_format(AT_high_pc, {FORM_ADDR})
});

Result<void> SymbolTableImporter::import_subroutine(const DIE& die)
{	
	Value name;
	Value mangled_name;
	Value low_pc;
	Value high_pc;
	Result<void> attribute_result = die.scan_attributes(
		subroutine_attributes, {&name, &mangled_name, &low_pc, &high_pc});
	CCC_RETURN_IF_ERROR(attribute_result);
	
	Address address = low_pc.address_or_null();
	
	Result<Function*> function = m_database.functions.create_symbol(
		get_name(name, mangled_name), m_group.source, m_group.module_symbol, address, m_importer_flags, m_demangler);
	CCC_RETURN_IF_ERROR(function);
	
	if (low_pc.valid() && high_pc.valid()) {
		(*function)->set_size(high_pc.address() - low_pc.address());
	}
	
	return Result<void>();
}

static const AttributeListFormat reference_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_fund_type, {FORM_DATA2}),
	DIE::attribute_format(AT_mod_fund_type, {FORM_BLOCK2}),
	DIE::attribute_format(AT_user_def_type, {FORM_REF}),
	DIE::attribute_format(AT_mod_u_d_type, {FORM_BLOCK2}),
	DIE::attribute_format(AT_subscr_data, {FORM_BLOCK2})
});

Result<void> SymbolTableImporter::compute_reference_counts(const DIE& first_die, bool is_inside_type)
{
	std::optional<DIE> die = first_die;
	while (die.has_value()) {
		bool is_type_or_inside_type = die_is_type(*die) || is_inside_type;
		
		Value fund_type;
		Value mod_fund_type;
		Value user_def_type;
		Value mod_u_d_type;
		Value subscr_data;
		Result<void> attribute_result = die->scan_attributes(
			reference_attributes, {&fund_type, &mod_fund_type, &user_def_type, &mod_u_d_type, &subscr_data});
		CCC_RETURN_IF_ERROR(attribute_result);
		
		std::optional<Type> type = Type::from_attributes(fund_type, mod_fund_type, user_def_type, mod_u_d_type);
		if (type.has_value() && (type->attribute() == AT_user_def_type || type->attribute() == AT_mod_u_d_type)) {
			Result<u32> referenced_die = type->user_def_type();
			CCC_ASSERT(referenced_die.success());
			
			if (is_type_or_inside_type) {
				m_die_reference_counts[*referenced_die].references_from_types++;
			} else {
				m_die_reference_counts[*referenced_die].references_not_from_types++;
			}
		}
		
		if (subscr_data.valid()) {
			ArraySubscriptData subscript_data = ArraySubscriptData::from_block(subscr_data.block());
			
			u32 offset = 0;
			while (offset < subscript_data.size()) {
				Result<ArraySubscriptItem> item = subscript_data.parse_item(offset, m_importer_flags);
				CCC_RETURN_IF_ERROR(item);
				
				u32 et_attrib = item->element_type.attribute();
				
				if (item->specifier == FMT_ET && (et_attrib == AT_user_def_type || et_attrib == AT_mod_u_d_type)) {
					Result<u32> referenced_die = item->element_type.user_def_type();
					CCC_ASSERT(referenced_die.success());
					
					if (is_type_or_inside_type) {
						m_die_reference_counts[*referenced_die].references_from_types++;
					} else {
						m_die_reference_counts[*referenced_die].references_not_from_types++;
					}
				}
			}
		}
		
		Result<std::optional<DIE>> first_child = die->first_child();
		CCC_RETURN_IF_ERROR(first_child);
		
		if (first_child->has_value()) {
			Result<void> child_result = compute_reference_counts(**first_child, is_type_or_inside_type);
			CCC_RETURN_IF_ERROR(child_result);
		}
		
		Result<std::optional<DIE>> next_die = die->sibling();
		CCC_RETURN_IF_ERROR(next_die);
		die = *next_die;
	}
	
	return Result<void>();
}

static const AttributeListFormat overlay_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_overlay_id, {FORM_DATA4}),
	DIE::attribute_format(AT_overlay_name, {FORM_STRING})
});

Result<std::vector<OverlayInfo>> enumerate_overlays(const SectionReader& dwarf)
{
	Result<DIE> first_die = dwarf.first_die();
	CCC_RETURN_IF_ERROR(first_die);
	
	std::vector<OverlayInfo> overlays;
	
	std::optional<DIE> die = *first_die;
	while (die.has_value()) {
		if (die->tag() == TAG_overlay) {
			Value overlay_id;
			Value overlay_name;
			Result<void> attribute_result = die->scan_attributes(
				overlay_attributes, {&overlay_id, &overlay_name});
			CCC_RETURN_IF_ERROR(attribute_result);
			
			if (overlay_id.valid()) {
				OverlayInfo& info = overlays.emplace_back();
				info.id = static_cast<u32>(overlay_id.constant());
				info.name = overlay_name.string_or_null();
			}
		}
		
		Result<std::optional<DIE>> next_die = die->sibling();
		CCC_RETURN_IF_ERROR(next_die);
		die = *next_die;
	}
	
	return overlays;
}

static Result<std::map<u32, u32>> parse_overlays(SymbolDatabase& database, const DIE& first_die)
{
	std::map<u32, u32> compile_unit_offset_to_overlay_id;
	
	std::optional<DIE> die = first_die;
	while (die.has_value()) {
		if (die->tag() == TAG_overlay) {
			Value overlay_id;
			Value overlay_name;
			Result<void> attribute_result = die->scan_attributes(
				overlay_attributes, {&overlay_id, &overlay_name});
			CCC_RETURN_IF_ERROR(attribute_result);
			
			if (overlay_id.valid()) {
				// We need to iterate over all the attributes here rather than
				// use my fancy API because, despite what page 3 of the spec
				// says, there are multiple attributes of the same type.
				Result<std::vector<AttributeTuple>> attributes = die->all_attributes();
				CCC_RETURN_IF_ERROR(attributes);
				
				for (const auto& [offset, attribute, value] : *attributes) {
					if (attribute == AT_member && value.form() == FORM_REF) {
						compile_unit_offset_to_overlay_id.emplace(
							value.reference(), static_cast<u32>(overlay_id.constant()));
					}
				}
			}
		}
		
		Result<std::optional<DIE>> next_die = die->sibling();
		CCC_RETURN_IF_ERROR(next_die);
		die = *next_die;
	}
	
	return compile_unit_offset_to_overlay_id;
}

static std::string get_name(const Value& name, const Value& mangled_name)
{
	if (mangled_name.valid()) {
		return std::string(mangled_name.string());
	} else if (name.valid()) {
		return std::string(name.string());
	}
	
	return std::string();
}

}
