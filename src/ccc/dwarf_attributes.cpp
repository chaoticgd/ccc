// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_attributes.h"

#include "importer_flags.h"
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

Result<AttributeTuple> parse_attribute(std::span<const u8> bytes, u32& offset, u32 importer_flags)
{
	AttributeTuple result;
	
	result.offset = offset;
	
	const std::optional<u16> name = copy_unaligned<u16>(bytes, offset);
	CCC_CHECK(name.has_value(), "Cannot read attribute name at 0x%x.", offset);
	offset += sizeof(u16);
	
	u8 form = *name & 0xf;
	CCC_CHECK(form_to_string(form) != nullptr,
		"Unknown attribute form 0x%hhx at 0x%x.", form, offset);
	
	u16 attribute = *name >> 4;
	bool known_attribute = attribute_to_string(attribute);
	if (!known_attribute && (importer_flags & STRICT_PARSING)) {
		CCC_WARN("Unknown attribute name 0x%03hx at 0x%x.", *name, offset);
	}
	
	result.attribute = static_cast<Attribute>(attribute);
	
	switch (form) {
		case FORM_ADDR: {
			std::optional<u32> address = copy_unaligned<u32>(bytes, offset);
			CCC_CHECK(address.has_value(), "Cannot read address attribute at 0x%x.", offset);
			result.value = Value::from_address(*address);
			offset += sizeof(u32);
			break;
		}
		case FORM_REF: {
			std::optional<u32> reference = copy_unaligned<u32>(bytes, offset);
			CCC_CHECK(reference.has_value(), "Cannot read reference attribute at 0x%x.", offset);
			result.value = Value::from_reference(*reference);
			offset += sizeof(u32);
			break;
		}
		case FORM_BLOCK2: {
			std::optional<u16> size = copy_unaligned<u16>(bytes, offset);
			CCC_CHECK(size.has_value(), "Cannot read block attribute size at 0x%x.", offset);
			offset += sizeof(u16);
			
			CCC_CHECK((u64) offset + *size <= bytes.size(),
				"Cannot read block attribute data at 0x%x.", offset);
			result.value = Value::from_block_2(bytes.subspan(offset, *size));
			offset += *size;
			
			break;
		}
		case FORM_BLOCK4: {
			std::optional<u32> size = copy_unaligned<u32>(bytes, offset);
			CCC_CHECK(size.has_value(), "Cannot read block attribute size at 0x%x.", offset);
			offset += sizeof(u32);
			
			CCC_CHECK((u64) offset + *size <= bytes.size(),
				"Cannot read block attribute data at 0x%x.", offset);
			result.value = Value::from_block_4(bytes.subspan(offset, *size));
			offset += *size;
			
			break;
		}
		case FORM_DATA2: {
			std::optional<u16> constant = copy_unaligned<u16>(bytes, offset);
			CCC_CHECK(constant.has_value(), "Cannot read constant attribute at 0x%x.", offset);
			result.value = Value::from_constant_2(*constant);
			offset += sizeof(u16);
			break;
		}
		case FORM_DATA4: {
			std::optional<u32> constant = copy_unaligned<u32>(bytes, offset);
			CCC_CHECK(constant.has_value(), "Cannot read constant attribute at 0x%x.", offset);
			result.value = Value::from_constant_4(*constant);
			offset += sizeof(u32);
			break;
		}
		case FORM_DATA8: {
			std::optional<u64> constant = copy_unaligned<u64>(bytes, offset);
			CCC_CHECK(constant.has_value(), "Cannot read constant attribute at 0x%x.", offset);
			result.value = Value::from_constant_8(*constant);
			offset += sizeof(u64);
			break;
		}
		case FORM_STRING: {
			std::optional<std::string_view> string = get_string(bytes, offset);
			CCC_CHECK(string.has_value(), "Cannot read string attribute at 0x%x.", offset);
			result.value = Value::from_string(*string);
			offset += static_cast<u32>(string->size()) + 1;
			break;
		}
	}
	
	return result;
}

// *****************************************************************************

LocationDescription LocationDescription::from_block(std::span<const u8> block)
{
	LocationDescription location_description;
	location_description.m_block = block;
	return location_description;
}

Result<void> LocationDescription::print(FILE* out) const
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

std::optional<Type> Type::from_attribute_tuple(const AttributeTuple& tuple)
{
	switch (tuple.attribute) {
		case AT_fund_type:
			return Type::from_fund_type(tuple.value);
		case AT_mod_fund_type:
			return Type::from_mod_fund_type(tuple.value);
		case AT_user_def_type:
			return Type::from_user_def_type(tuple.value);
		case AT_mod_u_d_type:
			return Type::from_mod_u_d_type(tuple.value);
		default:
			break;
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
	
	return CCC_FAILURE("Type::fund_type called on user-defined or null type.");
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
	
	return CCC_FAILURE("Type::user_def_type called on fundamental or null type.");
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

ArrayBound ArrayBound::from_constant(u32 constant)
{
	ArrayBound bound;
	bound.m_type = ArrayBoundType::CONSTANT;
	bound.m_constant = constant;
	return bound;
}

ArrayBound ArrayBound::from_location_description(LocationDescription location_description)
{
	ArrayBound bound;
	bound.m_type = ArrayBoundType::LOCATION_DESCRIPTION;
	bound.m_location_description = location_description;
	return bound;
}

ArrayBoundType ArrayBound::type() const
{
	return m_type;
}

u32 ArrayBound::constant() const
{
	CCC_ASSERT(m_type == ArrayBoundType::CONSTANT);
	return m_constant;
}

const LocationDescription& ArrayBound::location_description() const
{
	CCC_ASSERT(m_type == ArrayBoundType::LOCATION_DESCRIPTION);
	return m_location_description;
}

Result<void> ArrayBound::print(FILE* out) const
{
	switch (m_type) {
		case ArrayBoundType::CONSTANT: {
			fprintf(out, "0x%x", m_constant);
			break;
		}
		case ArrayBoundType::LOCATION_DESCRIPTION: {
			Result<void> print_result = m_location_description.print(out);
			CCC_RETURN_IF_ERROR(print_result);
			break;
		}
		default: {
			return CCC_FAILURE("ArrayBound::print called on null array bound.");
		}
	}
	
	return Result<void>();
}

// *****************************************************************************

ArraySubscriptData ArraySubscriptData::from_block(std::span<const u8> block)
{
	ArraySubscriptData subscript_data;
	subscript_data.m_block = block;
	return subscript_data;
}

u32 ArraySubscriptData::size() const
{
	return static_cast<u32>(m_block.size());
}

Result<ArraySubscriptItem> ArraySubscriptData::parse_subscript(u32& offset, u32 importer_flags) const
{
	ArraySubscriptItem subscript;
	
	std::optional<u8> specifier = copy_unaligned<u8>(m_block, offset);
	CCC_CHECK(specifier.has_value(), "Failed to read array subscript format specifier.");
	CCC_CHECK(array_subscript_format_specifier_to_string(*specifier),
		"Invalid array subscript format specifier 0x%hhx.\n", *specifier);
	subscript.specifier = static_cast<ArraySubscriptFormatSpecifier>(*specifier);
	offset += sizeof(u8);
	
	// Parse the subscript index type, which is either a fundamental type or a
	// user-defined type.
	switch (subscript.specifier) {
		case FMT_FT_C_C:
		case FMT_FT_C_X:
		case FMT_FT_X_C:
		case FMT_FT_X_X: {
			Result<u16> fund_type = parse_fund_type(offset);
			CCC_RETURN_IF_ERROR(fund_type);
			
			subscript.subscript_index_type = Type::from_fund_type(
				Value::from_constant_2(*fund_type));
			break;
		}
		case FMT_UT_C_C:
		case FMT_UT_C_X:
		case FMT_UT_X_C:
		case FMT_UT_X_X: {
			Result<u32> user_def_type = parse_user_def_type(offset);
			CCC_RETURN_IF_ERROR(user_def_type);
			
			subscript.subscript_index_type = Type::from_user_def_type(
				Value::from_reference(*user_def_type));
			break;
		}
		default: {
			break;
		}
	}
	
	// Parse the lower bound, which is either a constant (C) or a location
	// description (X).
	switch (subscript.specifier) {
		case FMT_FT_C_C:
		case FMT_FT_C_X:
		case FMT_UT_C_C:
		case FMT_UT_C_X: {
			Result<u32> constant = parse_constant(offset);
			CCC_RETURN_IF_ERROR(constant);
			
			subscript.lower_bound = ArrayBound::from_constant(*constant);
			break;
		}
		case FMT_FT_X_C:
		case FMT_FT_X_X:
		case FMT_UT_X_C:
		case FMT_UT_X_X: {
			Result<LocationDescription> location_description = parse_location_description(offset);
			CCC_RETURN_IF_ERROR(location_description);
			
			subscript.lower_bound = ArrayBound::from_location_description(*location_description);
			break;
		}
		default: {
			break;
		}
	}
	
	// Parse the upper bound, which is either a constant (C) or a location
	// description (X).
	switch (subscript.specifier) {
		case FMT_FT_C_C:
		case FMT_FT_X_C:
		case FMT_UT_C_C:
		case FMT_UT_X_C: {
			Result<u32> constant = parse_constant(offset);
			CCC_RETURN_IF_ERROR(constant);
			
			subscript.upper_bound = ArrayBound::from_constant(*constant);
			break;
			break;
		}
		case FMT_FT_C_X:
		case FMT_FT_X_X:
		case FMT_UT_C_X:
		case FMT_UT_X_X: {
			Result<LocationDescription> location_description = parse_location_description(offset);
			CCC_RETURN_IF_ERROR(location_description);
			
			subscript.upper_bound = ArrayBound::from_location_description(*location_description);
			break;
		}
		default: {
			break;
		}
	}
	
	// Parse the element type.
	if (subscript.specifier == FMT_ET) {
		Result<AttributeTuple> attribute = parse_attribute(m_block, offset, importer_flags);
		CCC_RETURN_IF_ERROR(attribute);
		
		std::optional<Type> element_type = Type::from_attribute_tuple(*attribute);
		CCC_CHECK(element_type.has_value(), "Element type is not a type attribute.");
		subscript.element_type = std::move(*element_type);
	}
	
	return subscript;
}

Result<u16> ArraySubscriptData::parse_fund_type(u32& offset) const
{
	std::optional<u16> fund_type = copy_unaligned<u16>(m_block, offset);
	CCC_CHECK(fund_type.has_value(), "Failed to read fundamental type in array subscript.");
	CCC_CHECK(fundamental_type_to_string(*fund_type),
		"Invalid fundamental type 0x%hx in array subscript.", *fund_type);
	offset += sizeof(u16);
	return *fund_type;
}

Result<u32> ArraySubscriptData::parse_user_def_type(u32& offset) const
{
	std::optional<u32> user_def_type = copy_unaligned<u32>(m_block, offset);
	CCC_CHECK(user_def_type.has_value(), "Failed to read user-defined type in array subscript.");
	offset += sizeof(u32);
	return *user_def_type;
}

Result<u32> ArraySubscriptData::parse_constant(u32& offset) const
{
	std::optional<u32> constant = copy_unaligned<u32>(m_block, offset);
	CCC_CHECK(constant.has_value(), "Failed to read constant in array subscript.");
	offset += sizeof(u32);
	return *constant;
}

Result<LocationDescription> ArraySubscriptData::parse_location_description(u32& offset) const
{
	std::optional<u16> size = copy_unaligned<u16>(m_block, offset);
	CCC_CHECK(size.has_value(), "Failed to read location description size in array subscript.");
	offset += sizeof(u16);
	
	std::optional<std::span<const u8>> location_description = get_subspan(m_block, offset, *size);
	CCC_CHECK(location_description.has_value(), "Failed to read location description in array subscript.");
	offset += *size;
	
	return LocationDescription::from_block(*location_description);
}

// *****************************************************************************

const char* form_to_string(u32 value)
{
	switch (value) {
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

const char* attribute_to_string(u32 value)
{
	switch (value) {
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

const char* location_op_to_string(u32 value)
{
	switch (value) {
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

const char* fundamental_type_to_string(u32 value)
{
	switch (value) {
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

const char* type_modifier_to_string(u32 value)
{
	switch (value) {
		case MOD_pointer_to: return "pointer_to";
		case MOD_reference_to: return "reference_to";
		case MOD_const: return "const";
		case MOD_volatile: return "volatile";
	}
	
	return nullptr;
}

const char* language_to_string(u32 value)
{
	switch (value) {
		case LANG_C89: return "C89";
		case LANG_C: return "C";
		case LANG_ADA83: return "ADA83";
		case LANG_C_PLUS_PLUS: return "C_PLUS_PLUS";
		case LANG_COBOL74: return "COBOL74";
		case LANG_COBOL85: return "COBOL85";
		case LANG_FORTRAN77: return "FORTRAN77";
		case LANG_FORTRAN90: return "FORTRAN90";
		case LANG_PASCAL83: return "PASCAL83";
		case LANG_MODULA2: return "MODULA2";
		case LANG_ASSEMBLY: return "ASSEMBLY";
	}
	
	return nullptr;
}

const char* array_ordering_to_string(u32 value)
{
	switch (value) {
		case ORD_col_major: return "col_major";
		case ORD_row_major: return "row_major";
	}
	
	return nullptr;
}

const char* array_subscript_format_specifier_to_string(u32 value)
{
	switch (value) {
		case FMT_FT_C_C: return "FT_C_C";
		case FMT_FT_C_X: return "FT_C_X";
		case FMT_FT_X_C: return "FT_X_C";
		case FMT_FT_X_X: return "FT_X_X";
		case FMT_UT_C_C: return "UT_C_C";
		case FMT_UT_C_X: return "UT_C_X";
		case FMT_UT_X_C: return "UT_X_C";
		case FMT_UT_X_X: return "UT_X_X";
		case FMT_ET: return "ET";
	}
	
	return nullptr;
}

}
