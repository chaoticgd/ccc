// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_attributes.h"

#include "registers.h"

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

Form Value::form() const
{
	return static_cast<Form>(m_form);
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

Value Value::from_constant_2(u16 constant)
{
	Value result;
	result.m_form = FORM_DATA2;
	result.m_value.constant = constant;
	return result;
}

Value Value::from_constant_4(u32 constant)
{
	Value result;
	result.m_form = FORM_DATA4;
	result.m_value.constant = constant;
	return result;
}

Value Value::from_constant_8(u64 constant)
{
	Value result;
	result.m_form = FORM_DATA8;
	result.m_value.constant = constant;
	return result;
}

Value Value::from_block_2(std::span<const u8> block)
{
	Value result;
	result.m_form = FORM_BLOCK2;
	result.m_value.block.begin = block.data();
	result.m_value.block.end = block.data() + block.size();
	return result;
}

Value Value::from_block_4(std::span<const u8> block)
{
	Value result;
	result.m_form = FORM_BLOCK4;
	result.m_value.block.begin = block.data();
	result.m_value.block.end = block.data() + block.size();
	return result;
}

Value Value::from_string(std::string_view string)
{
	Value result;
	result.m_form = FORM_STRING;
	result.m_value.string.begin = string.data();
	result.m_value.string.end = string.data() + string.size();
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
	CCC_ASSERT(m_form == FORM_DATA2 || m_form == FORM_DATA4 || m_form == FORM_DATA8);
	return m_value.constant;
}

std::span<const u8> Value::block() const
{
	CCC_ASSERT(m_form == FORM_BLOCK2 || m_form == FORM_BLOCK4);
	return std::span<const u8>(m_value.block.begin, m_value.block.end);
}

std::string_view Value::string() const
{
	CCC_ASSERT(m_form == FORM_STRING);
	return std::string_view(m_value.string.begin, m_value.string.end);
}

Address Value::address_or_null() const
{
	if (!valid() || m_form != FORM_ADDR) {
		return Address();
	}
	
	return m_value.address;
}

std::optional<u32> Value::reference_or_null() const
{
	if (!valid() || m_form != FORM_REF) {
		return std::nullopt;
	}
	
	return m_value.reference;
}

std::optional<u64> Value::constant_or_null() const
{
	if (!valid() || (m_form != FORM_DATA2 && m_form != FORM_DATA4 && m_form != FORM_DATA8)) {
		return std::nullopt;
	}
	
	return m_value.constant;
}

std::span<const u8> Value::block_or_null() const
{
	if (!valid() || (m_form != FORM_BLOCK2 && m_form != FORM_BLOCK4)) {
		return std::span<const u8>();
	}
	
	return std::span<const u8>(m_value.block.begin, m_value.block.end);
}

std::string_view Value::string_or_null() const
{
	if (!valid() || m_form != FORM_STRING) {
		return std::string_view();
	}
	
	return std::string_view(m_value.string.begin, m_value.string.end);
}

// *****************************************************************************

LocationDescription::LocationDescription(std::span<const u8> block)
	: m_block(block) {}

Result<void> LocationDescription::print(FILE* out)
{
	fprintf(out, "{");
	
	u32 offset = 0;
	while (offset < m_block.size()) {
		if (offset != 0) {
			fprintf(out, ",");
		}
		
		Result<LocationAtom> atom = parse_atom(offset);
		CCC_RETURN_IF_ERROR(atom);
		
		const char* op_name = location_op_to_string(atom->op);
		CCC_ASSERT(op_name);
		
		fprintf(out, "%s", op_name);
		
		if (atom->value.has_value()) {
			if ((atom->op == OP_REG || atom->op == OP_BASEREG) && *atom->value < 32) {
				fprintf(out, "(%s)", mips::GPR_STRINGS[*atom->value]);
			} else {
				fprintf(out, "(0x%x)", *atom->value);
			}
		}
	}
	
	fprintf(out, "}");
	
	return Result<void>();
}

Result<LocationAtom> LocationDescription::parse_atom(u32& offset) const
{
	LocationAtom atom;
	
	const u8* op = get_unaligned<u8>(m_block, offset);
	CCC_CHECK(op, "Invalid location description (cannot read op).");
	offset += sizeof(u8);
	
	const char* op_name = location_op_to_string(*op);
	CCC_CHECK(op_name, "Invalid location description (unknown op 0x%hhx).", *op);
	
	atom.op = static_cast<LocationOp>(*op);
	
	if (*op == OP_REG || *op == OP_BASEREG || *op == OP_ADDR || *op == OP_CONST || *op == OP_80) {
		const u32* value = get_unaligned<u32>(m_block, offset);
		CCC_CHECK(value, "Invalid location descripton (cannot read value).");
		offset += sizeof(u32);
		
		atom.value = *value;
	}
	
	return atom;
}

// *****************************************************************************

std::optional<Type> Type::from_attributes(
	const Value& fund_type, const Value& mod_fund_type, const Value& user_def_type, const Value& mod_u_d_type)
{
	if (fund_type.valid()) {
		return from_fund_type(fund_type);
	} else if (mod_fund_type.valid()) {
		return from_user_def_type(mod_fund_type);
	} else if (user_def_type.valid()) {
		return from_fund_type(user_def_type);
	} else if (mod_u_d_type.valid()) {
		return from_mod_u_d_type(mod_u_d_type);
	}
	
	return std::nullopt;
}

Type Type::from_fund_type(const Value& fund_type)
{
	Type type;
	type.m_attribute = AT_fund_type;
	type.m_value = fund_type;
	return type;
}

Type Type::from_mod_fund_type(const Value& mod_fund_type)
{
	Type type;
	type.m_attribute = AT_mod_fund_type;
	type.m_value = mod_fund_type;
	return type;
}

Type Type::from_user_def_type(const Value& user_def_type)
{
	Type type;
	type.m_attribute = AT_user_def_type;
	type.m_value = user_def_type;
	return type;
}

Type Type::from_mod_u_d_type(const Value& mod_u_d_type)
{
	Type type;
	type.m_attribute = AT_mod_u_d_type;
	type.m_value = mod_u_d_type;
	return type;
}

u32 Type::attribute() const
{
	return m_attribute;
}

Result<FundamentalType> Type::fund_type() const
{
	switch (m_attribute) {
		case AT_fund_type: {
			u16 fund_type = static_cast<u16>(m_value.constant());
			CCC_CHECK(fundamental_type_to_string(fund_type), "Invalid fundamental type 0x%hx.", fund_type);
			return static_cast<FundamentalType>(fund_type);
		}
		case AT_mod_fund_type: {
			std::span<const u8> block = m_value.block();
			std::optional<u16> fund_type = copy_unaligned<u16>(block, block.size() - sizeof(u16));
			CCC_CHECK(fund_type.has_value(), "Modified fundamental type attribute too small.");
			CCC_CHECK(fundamental_type_to_string(*fund_type), "Invalid modified fundamental type 0x%hx.", *fund_type);
			
			return static_cast<FundamentalType>(*fund_type);
		}
	}
	
	return CCC_FAILURE("Type::fund_type called on user-defined type.");
}

Result<u32> Type::user_def_type() const
{
	switch (m_attribute) {
		case AT_user_def_type: {
			return m_value.reference();
		}
		case AT_mod_u_d_type: {
			std::span<const u8> block = m_value.block();
			std::optional<u32> die_offset = copy_unaligned<u32>(block, block.size() - sizeof(u32));
			CCC_CHECK(die_offset.has_value(), "Modified user-defined type attribute too small.");
			
			return *die_offset;
		}
	}
	
	return CCC_FAILURE("Type::user_def_type called on fundamental type.");
}

Result<std::span<const TypeModifier>> Type::modifiers() const
{
	if (m_attribute != AT_mod_fund_type && m_attribute != AT_mod_u_d_type) {
		return std::span<const TypeModifier>();
	}
	
	u32 head_size = m_attribute == AT_mod_fund_type ? 2 : 4;
	std::span<const u8> block = m_value.block();
	std::span<const u8> modifiers = block.subspan(0, block.size() - head_size);
	
	for (u8 modifier : modifiers)
		CCC_CHECK(type_modifier_to_string(modifier), "Invalid type modifier 0x%hhx.", modifier);
	
	return std::span<const TypeModifier>(
		reinterpret_cast<const TypeModifier*>(modifiers.data()),
		reinterpret_cast<const TypeModifier*>(modifiers.data() + modifiers.size()));
}

// *****************************************************************************

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
		case AT_mangled_name: return "mangled_name";
		case AT_overlay_id: return "overlay_id";
		case AT_overlay_name: return "overlay_name";
	}
	
	return nullptr;
}

const char* location_op_to_string(u32 op)
{
	switch (op) {
		case OP_REG: return "reg";
		case OP_BASEREG: return "basereg";
		case OP_ADDR: return "addr";
		case OP_CONST: return "const";
		case OP_DEREF2: return "deref2";
		case OP_DEREF: return "deref";
		case OP_ADD: return "add";
		case OP_80: return "op80";
	}
	
	return nullptr;
}

const char* fundamental_type_to_string(u32 fund_type)
{
	switch (fund_type) {
		case FT_char: return "char";
		case FT_signed_char: return "signed_char";
		case FT_unsigned_char: return "unsigned_char";
		case FT_short: return "short";
		case FT_signed_short: return "signed_short";
		case FT_unsigned_short: return "unsigned_short";
		case FT_integer: return "integer";
		case FT_signed_integer: return "signed_integer";
		case FT_unsigned_integer: return "unsigned_integer";
		case FT_long: return "long";
		case FT_signed_long: return "signed_long";
		case FT_unsigned_long: return "unsigned_long";
		case FT_pointer: return "pointer";
		case FT_float: return "float";
		case FT_dbl_prec_float: return "dbl_prec_float";
		case FT_ext_prec_float: return "ext_prec_float";
		case FT_complex: return "complex";
		case FT_dbl_prec_complex: return "dbl_prec_complex";
		case FT_void: return "void";
		case FT_boolean: return "boolean";
		case FT_ext_prec_complex: return "ext_prec_complex";
		case FT_label: return "label";
		case FT_long_long: return "long_long";
		case FT_signed_long_long: return "signed_long_long";
		case FT_unsigned_long_long: return "unsigned_long_long";
		case FT_int128: return "int128";
	}
	
	return nullptr;
}

const char* type_modifier_to_string(u32 modifier)
{
	switch (modifier) {
		case MOD_pointer_to: return "pointer_to";
		case MOD_reference_to: return "reference_to";
		case MOD_const: return "const";
		case MOD_volatile: return "volatile";
	}
	
	return nullptr;
}

}
