// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_to_ast.h"

#include "importer_flags.h"

namespace ccc::dwarf {

static std::unique_ptr<ast::Node> not_yet_implemented(const char* name);

TypeImporter::TypeImporter(
	SymbolDatabase& database,
	const SectionReader& dwarf,
	SymbolGroup group,
	u32 importer_flags,
	std::map<u32, ReferenceCounts>& die_reference_counts)
	: m_database(database)
	, m_dwarf(dwarf)
	, m_group(group)
	, m_importer_flags(importer_flags)
	, m_die_reference_counts(die_reference_counts) {}

static const AttributeListFormat type_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_fund_type, {FORM_DATA2}),
	DIE::attribute_format(AT_mod_fund_type, {FORM_BLOCK2}),
	DIE::attribute_format(AT_user_def_type, {FORM_REF}),
	DIE::attribute_format(AT_mod_u_d_type, {FORM_BLOCK2})
});

Result<std::unique_ptr<ast::Node>> TypeImporter::type_attribute_to_ast(const DIE& die)
{
	Value fund_type;
	Value mod_fund_type;
	Value user_def_type;
	Value mod_u_d_type;
	Result<void> attribute_result = die.scan_attributes(
		type_attributes, {&fund_type, &mod_fund_type, &user_def_type, &mod_u_d_type});
	CCC_RETURN_IF_ERROR(attribute_result);
	
	std::optional<Type> type = Type::from_attributes(fund_type, mod_fund_type, user_def_type, mod_u_d_type);
	CCC_CHECK(type.has_value(), "DIE at 0x%x has no type attributes.", die.offset());
	
	return type_to_ast(*type);
}

static const AttributeListFormat type_to_ast_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_name, {FORM_STRING})
});

Result<std::unique_ptr<ast::Node>> TypeImporter::type_to_ast(const Type& type)
{
	std::unique_ptr<ast::Node> node;
	switch (type.attribute()) {
		case AT_fund_type:
		case AT_mod_fund_type: {
			Result<FundamentalType> fund_type = type.fund_type();
			CCC_RETURN_IF_ERROR(fund_type);
			
			Result<std::unique_ptr<ast::Node>> fund_type_node = fundamental_type_to_ast(*fund_type);
			CCC_RETURN_IF_ERROR(fund_type_node);
			node = std::move(*fund_type_node);
			
			break;
		}
		case AT_user_def_type:
		case AT_mod_u_d_type: {
			Result<u32> die_offset = type.user_def_type();
			CCC_RETURN_IF_ERROR(die_offset);
			
			if(m_currently_importing_die[*die_offset]) {
				auto error_node = std::make_unique<ast::Error>();
				error_node->message = "TODO: Circular reference.";
				return std::unique_ptr<ast::Node>(std::move(error_node));
			}
			
			Result<std::optional<DIE>> referenced_die = m_dwarf.die_at(*die_offset);
			CCC_RETURN_IF_ERROR(referenced_die);
			CCC_CHECK(referenced_die->has_value(), "User-defined type is null.");
			
			Value name;
			Result<void> attribute_result = (*referenced_die)->scan_attributes(
				type_to_ast_attributes, {&name});
			CCC_RETURN_IF_ERROR(attribute_result);
			
			ReferenceCounts& counts = m_die_reference_counts[*die_offset];
			
			if (name.valid() || counts.references_from_types != 1 || counts.references_not_from_types != 0) {
				auto error_node = std::make_unique<ast::Error>();
				error_node->message = "TODO: Type name.";
				return std::unique_ptr<ast::Node>(std::move(error_node));
			}
			
			Result<std::unique_ptr<ast::Node>> user_def_node = die_to_ast(**referenced_die);
			CCC_RETURN_IF_ERROR(user_def_node);
			node = std::move(*user_def_node);
			
			break;
		}
	}
	
	Result<std::span<const TypeModifier>> modifiers = type.modifiers();
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
	std::optional<ast::BuiltInClass> bclass = fundamental_type_to_builtin_class(fund_type);
	if (!bclass.has_value()) {
		if (fund_type == FT_pointer) {
			Result<std::unique_ptr<ast::Node>> value_type = fundamental_type_to_ast(FT_void);
			CCC_RETURN_IF_ERROR(value_type);
			
			auto pointer = std::make_unique<ast::PointerOrReference>();
			pointer->is_pointer = true;
			pointer->value_type = std::move(*value_type);
			pointer->size_bytes = 4;
			return std::unique_ptr<ast::Node>(std::move(pointer));
		} else {
			return CCC_FAILURE("Unhandled fundamental type %s.", fundamental_type_to_string(fund_type));
		}
	}
	
	auto symbol_handle = m_fundamental_types.find(fund_type);
	if (symbol_handle == m_fundamental_types.end()) {
		std::string name;
		
		const char* string = fundamental_type_to_pretty_string(fund_type);
		if (string) {
			name = string;
		}
		
		Result<DataType*> data_type = m_database.data_types.create_symbol(
			std::move(name), m_group.source, m_group.module_symbol);
		CCC_RETURN_IF_ERROR(data_type);
		
		auto built_in = std::make_unique<ast::BuiltIn>();
		built_in->bclass = *bclass;
		built_in->size_bytes = ast::builtin_class_size(built_in->bclass);
		(*data_type)->set_type(std::move(built_in));
		
		symbol_handle = m_fundamental_types.emplace(fund_type, (*data_type)->handle()).first;
	}
	
	auto type_name = std::make_unique<ast::TypeName>();
	type_name->data_type_handle = symbol_handle->second;
	return std::unique_ptr<ast::Node>(std::move(type_name));
}

class DieLocker {
public:
	DieLocker(std::map<u32, bool>& currently_importing_die, u32 offset)
		: m_currently_importing_die(currently_importing_die)
		, m_offset(offset)
	{
		m_currently_importing_die[m_offset] = true;
	}
	
	~DieLocker()
	{
		m_currently_importing_die[m_offset] = false;
	}
	
private:
	std::map<u32, bool>& m_currently_importing_die;
	u32 m_offset;
};

Result<std::unique_ptr<ast::Node>> TypeImporter::die_to_ast(const DIE& die)
{
	std::unique_ptr<ast::Node> node;
	
	// Mark the DIE as currently being processed so we can detect cycles. This
	// needs to be a class so that the flag is unset if we return early due to
	// an error.
	DieLocker locker(m_currently_importing_die, die.offset());
	
	switch (die.tag()) {
		case TAG_array_type: {
			Result<std::unique_ptr<ast::Node>> array_type = array_type_to_ast(die);
			CCC_RETURN_IF_ERROR(array_type);
			node = std::move(*array_type);
			break;
		}
		case TAG_class_type: {
			Result<std::unique_ptr<ast::Node>> class_type = class_type_to_ast(die);
			CCC_RETURN_IF_ERROR(class_type);
			node = std::move(*class_type);
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

static const AttributeListFormat array_type_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_ordering, {FORM_DATA2}),
	DIE::attribute_format(AT_subscr_data, {FORM_BLOCK2}, AFF_REQUIRED)
});

Result<std::unique_ptr<ast::Node>> TypeImporter::array_type_to_ast(const DIE& die)
{
	Value ordering;
	Value subscr_data;
	Result<void> attribute_result = die.scan_attributes(array_type_attributes, {&ordering, &subscr_data});
	CCC_RETURN_IF_ERROR(attribute_result);
	
	ArraySubscriptData subscript_data = ArraySubscriptData::from_block(subscr_data.block());
	
	u32 offset = 0;
	
	Result<ArraySubscriptItem> subscript = subscript_data.parse_item(offset, m_importer_flags);
	CCC_RETURN_IF_ERROR(subscript);
	CCC_CHECK(subscript->specifier == FMT_FT_C_C, "First array subscript item with specifier other than FMT_FT_C_C.");
	CCC_CHECK(subscript->lower_bound.constant() == 0, "Lower bound of array subscript is non-zero.");
	
	Result<ArraySubscriptItem> et = subscript_data.parse_item(offset, m_importer_flags);
	CCC_RETURN_IF_ERROR(et);
	CCC_CHECK(et->specifier == FMT_ET, "Second array subscript item with specifier other than FMT_ET.");
	
	Result<std::unique_ptr<ast::Node>> element_type = type_to_ast(et->element_type);
	CCC_RETURN_IF_ERROR(element_type);
	
	auto array = std::make_unique<ast::Array>();
	array->element_type = std::move(*element_type);
	array->element_count = subscript->upper_bound.constant() + 1;
	
	return std::unique_ptr<ast::Node>(std::move(array));
}

static const AttributeListFormat class_type_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_name, {FORM_STRING}),
	DIE::attribute_format(AT_byte_size, {FORM_DATA4})
});

static const AttributeListFormat member_attributes = DIE::attribute_list_format({
	DIE::attribute_format(AT_name, {FORM_STRING})
});

Result<std::unique_ptr<ast::Node>> TypeImporter::class_type_to_ast(const DIE& die)
{
	Value name;
	Value byte_size;
	Result<void> attribute_result = die.scan_attributes(class_type_attributes, {&name, &byte_size});
	CCC_RETURN_IF_ERROR(attribute_result);
	
	auto struct_or_union = std::make_unique<ast::StructOrUnion>();
	
	if (name.valid()) {
		struct_or_union->name = name.string();
	}
	
	if (byte_size.valid()) {
		struct_or_union->size_bytes = static_cast<s32>(byte_size.constant());
	}
	
	Result<std::optional<DIE>> first_member = die.first_child();
	CCC_RETURN_IF_ERROR(first_member);
	
	std::optional<DIE> member = *first_member;
	while (member.has_value()) {
		if (member->tag() == TAG_member) {
			Result<std::unique_ptr<ast::Node>> field = type_attribute_to_ast(*member);
			CCC_RETURN_IF_ERROR(field);
			
			Value member_name;
			Result<void> member_attribute_result = member->scan_attributes(member_attributes, {&member_name});
			CCC_RETURN_IF_ERROR(member_attribute_result);
			
			if (member_name.valid()) {
				(*field)->name = member_name.string();
			}
			
			struct_or_union->fields.emplace_back(std::move(*field));
		}
		
		Result<std::optional<DIE>> next_member = member->sibling();
		CCC_RETURN_IF_ERROR(next_member);
		member = *next_member;
	}
	
	return std::unique_ptr<ast::Node>(std::move(struct_or_union));
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

std::optional<ast::BuiltInClass> fundamental_type_to_builtin_class(FundamentalType fund_type)
{
	switch (fund_type) {
		case FT_char:               return ast::BuiltInClass::UNQUALIFIED_8;
		case FT_signed_char:        return ast::BuiltInClass::SIGNED_8;
		case FT_unsigned_char:      return ast::BuiltInClass::UNSIGNED_8;
		case FT_short:              return ast::BuiltInClass::SIGNED_16;
		case FT_signed_short:       return ast::BuiltInClass::SIGNED_16;
		case FT_unsigned_short:     return ast::BuiltInClass::UNSIGNED_16;
		case FT_integer:            return ast::BuiltInClass::SIGNED_32;
		case FT_signed_integer:     return ast::BuiltInClass::SIGNED_32;
		case FT_unsigned_integer:   return ast::BuiltInClass::UNSIGNED_32;
		case FT_long:               return ast::BuiltInClass::SIGNED_64;
		case FT_signed_long:        return ast::BuiltInClass::SIGNED_64;
		case FT_unsigned_long:      return ast::BuiltInClass::UNSIGNED_64;
		// case FT_pointer:            return ast::BuiltInClass::UNSIGNED_32;
		case FT_float:              return ast::BuiltInClass::FLOAT_32;
		case FT_dbl_prec_float:     return ast::BuiltInClass::FLOAT_64;
		case FT_ext_prec_float:     return ast::BuiltInClass::FLOAT_64;
		// case FT_complex:            return ast::BuiltInClass::FLOAT_32;
		// case FT_dbl_prec_complex:   return ast::BuiltInClass::FLOAT_64;
		case FT_void:               return ast::BuiltInClass::VOID_TYPE;
		case FT_boolean:            return ast::BuiltInClass::BOOL_8;
		// case FT_ext_prec_complex:   return ast::BuiltInClass::FLOAT_64;
		// case FT_label:              return ast::BuiltInClass::VOID_TYPE;
		case FT_long_long:          return ast::BuiltInClass::SIGNED_64;
		case FT_signed_long_long:   return ast::BuiltInClass::SIGNED_64;
		case FT_unsigned_long_long: return ast::BuiltInClass::UNSIGNED_64;
		case FT_int128:             return ast::BuiltInClass::UNQUALIFIED_128;
		default: {}
	}
	
	return std::nullopt;
}

static std::unique_ptr<ast::Node> not_yet_implemented(const char* name)
{
	auto error = std::make_unique<ast::Error>();
	error->message = std::string(name) + " support not yet implemented.";
	return error;
}

}
