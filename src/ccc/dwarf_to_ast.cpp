// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_to_ast.h"

#include "importer_flags.h"

namespace ccc::dwarf {

static std::unique_ptr<ast::Node> not_yet_implemented(const char* name);

TypeImporter::TypeImporter(SymbolDatabase& database, const SectionReader& dwarf, u32 importer_flags)
	: m_database(database)
	, m_dwarf(dwarf)
	, m_importer_flags(importer_flags) {}

Result<std::unique_ptr<ast::Node>> TypeImporter::type_attribute_to_ast(const DIE& die)
{
	// DWARF 1 has 4 different types of type attributes, but each DIE should
	// only have one of them.
	static const AttributesSpec type_attributes = DIE::specify_attributes({
		DIE::optional_attribute(AT_fund_type, {FORM_DATA2}),
		DIE::optional_attribute(AT_mod_fund_type, {FORM_BLOCK2}),
		DIE::optional_attribute(AT_user_def_type, {FORM_REF}),
		DIE::optional_attribute(AT_mod_u_d_type, {FORM_BLOCK2})
	});
	
	Value fund_type;
	Value mod_fund_type;
	Value user_def_type;
	Value mod_u_d_type;
	Result<void> attribute_result = die.attributes(
		type_attributes, {&fund_type, &mod_fund_type, &user_def_type, &mod_u_d_type});
	CCC_RETURN_IF_ERROR(attribute_result);
	
	std::optional<Type> type = Type::from_attributes(fund_type, mod_fund_type, user_def_type, mod_u_d_type);
	CCC_CHECK(type.has_value(), "DIE at 0x%x has no type attributes.", die.offset());
	
	std::unique_ptr<ast::Node> node;
	switch (type->attribute()) {
		case AT_fund_type:
		case AT_mod_fund_type: {
			Result<FundamentalType> fund_type = type->fund_type();
			CCC_RETURN_IF_ERROR(fund_type);
			
			Result<std::unique_ptr<ast::Node>> fund_type_node = fundamental_type_to_ast(*fund_type);
			CCC_RETURN_IF_ERROR(fund_type_node);
			node = std::move(*fund_type_node);
			
			break;
		}
		case AT_user_def_type:
		case AT_mod_u_d_type: {
			Result<u32> die_offset = type->user_def_type();
			CCC_RETURN_IF_ERROR(die_offset);
			
			Result<std::optional<DIE>> referenced_die = m_dwarf.die_at(*die_offset);
			CCC_RETURN_IF_ERROR(referenced_die);
			CCC_CHECK(referenced_die->has_value(), "DIE at 0x%x has null user-defined type.", die.offset());
			
			Result<std::unique_ptr<ast::Node>> user_def_node = die_to_ast(**referenced_die);
			CCC_RETURN_IF_ERROR(user_def_node);
			node = std::move(*user_def_node);
			
			break;
		}
	}
	
	Result<std::span<const TypeModifier>> modifiers = type->modifiers();
	CCC_RETURN_IF_ERROR(modifiers);
	
	// DWARF 1 type modifiers are stored in the same order as they would appear
	// in an English sentence e.g. "volatile pointer to a constant character" so
	// we need to read them in the reverse order to build an AST.
	for (size_t i = 0; i < modifiers->size(); i++) {
		TypeModifier modifier = (*modifiers)[modifiers->size() - i - 1];
		switch (modifier) {
			case MOD_pointer_to: {
				auto pointer = std::make_unique<ast::PointerOrReference>();
				pointer->is_pointer = true;
				pointer->value_type = std::move(node);
				node = std::move(pointer);
				break;
			}
			case MOD_reference_to: {
				auto reference = std::make_unique<ast::PointerOrReference>();
				reference->is_pointer = false;
				reference->value_type = std::move(node);
				node = std::move(reference);
				break;
			}
			case MOD_const: {
				node->is_const = true;
				break;
			}
			case MOD_volatile: {
				node->is_volatile = true;
				break;
			}
		}
	}
	
	return node;
}

Result<std::unique_ptr<ast::Node>> TypeImporter::fundamental_type_to_ast(FundamentalType fund_type)
{
	return not_yet_implemented("Fundamental type");
}

Result<std::unique_ptr<ast::Node>> TypeImporter::die_to_ast(const DIE& die)
{
	std::unique_ptr<ast::Node> node;
	
	switch (die.tag()) {
		case TAG_array_type: {
			node = not_yet_implemented("TAG_array_type");
			break;
		}
		case TAG_class_type: {
			node = not_yet_implemented("TAG_class_type");
			break;
		}
		case TAG_enumeration_type: {
			node = not_yet_implemented("TAG_enumeration_type");
			break;
		}
		case TAG_pointer_type: {
			node = not_yet_implemented("TAG_pointer_type");
			break;
		}
		case TAG_reference_type: {
			node = not_yet_implemented("TAG_reference_type");
			break;
		}
		case TAG_string_type: {
			node = not_yet_implemented("TAG_string_type");
			break;
		}
		case TAG_structure_type: {
			node = not_yet_implemented("TAG_structure_type");
			break;
		}
		case TAG_subroutine_type: {
			node = not_yet_implemented("TAG_subroutine_type");
			break;
		}
		case TAG_typedef: {
			node = not_yet_implemented("TAG_typedef");
			break;
		}
		case TAG_union_type: {
			node = not_yet_implemented("TAG_union_type");
			break;
		}
		case TAG_ptr_to_member_type: {
			node = not_yet_implemented("TAG_ptr_to_member_type");
			break;
		}
		case TAG_set_type: {
			node = not_yet_implemented("TAG_set_type");
			break;
		}
		case TAG_subrange_type: {
			node = not_yet_implemented("TAG_subrange_type");
			break;
		}
		default: {
			return CCC_FAILURE("DIE at 0x%x is not a type.", die.offset());
		}
	}
	
	if (node->descriptor == ast::ERROR_NODE && (m_importer_flags & STRICT_PARSING)) {
		return CCC_FAILURE("%s", node->as<ast::Error>().message.c_str());
	}
	
	return node;
}

bool die_is_type(const DIE& die)
{
	bool is_type;
	
	switch (die.tag()) {
		case TAG_array_type:
		case TAG_class_type:
		case TAG_enumeration_type:
		case TAG_pointer_type:
		case TAG_reference_type:
		case TAG_string_type:
		case TAG_structure_type:
		case TAG_subroutine_type:
		case TAG_typedef:
		case TAG_union_type:
		case TAG_ptr_to_member_type:
		case TAG_set_type:
		case TAG_subrange_type:
			is_type = true;
			break;
		default:
			is_type = false;
			break;
	}
	
	return is_type;
}

static std::unique_ptr<ast::Node> not_yet_implemented(const char* name)
{
	auto error = std::make_unique<ast::Error>();
	error->message = std::string(name) + " support not yet implemented.";
	return error;
}

}
