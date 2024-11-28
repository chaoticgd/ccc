// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "util.h"

namespace ccc::dwarf {

enum Form {
	FORM_ADDR   = 0x1,
	FORM_REF    = 0x2,
	FORM_BLOCK2 = 0x3,
	FORM_BLOCK4 = 0x4,
	FORM_DATA2  = 0x5,
	FORM_DATA4  = 0x6,
	FORM_DATA8  = 0x7,
	FORM_STRING = 0x8,
};

enum Attribute {
	AT_sibling            = 0x001,
	AT_location           = 0x002,
	AT_name               = 0x003,
	AT_fund_type          = 0x005,
	AT_mod_fund_type      = 0x006,
	AT_user_def_type      = 0x007,
	AT_mod_u_d_type       = 0x008,
	AT_ordering           = 0x009,
	AT_subscr_data        = 0x00a,
	AT_byte_size          = 0x00b,
	AT_bit_offset         = 0x00c,
	AT_bit_size           = 0x00d,
	AT_element_list       = 0x00f,
	AT_stmt_list          = 0x010,
	AT_low_pc             = 0x011,
	AT_high_pc            = 0x012,
	AT_language           = 0x013,
	AT_member             = 0x014,
	AT_discr              = 0x015,
	AT_discr_value        = 0x016,
	AT_string_length      = 0x019,
	AT_common_reference   = 0x01a,
	AT_comp_dir           = 0x01b,
	AT_const_value        = 0x01c,
	AT_containing_type    = 0x01d,
	AT_default_value      = 0x01e,
	AT_friends            = 0x01f,
	AT_inline             = 0x020,
	AT_is_optional        = 0x021,
	AT_lower_bound        = 0x022,
	AT_program            = 0x023,
	AT_private            = 0x024,
	AT_producer           = 0x025,
	AT_protected          = 0x026,
	AT_prototyped         = 0x027,
	AT_public             = 0x028,
	AT_pure_virtual       = 0x029,
	AT_return_addr        = 0x02a,
	AT_specification      = 0x02b,
	AT_start_scope        = 0x02c,
	AT_stride_size        = 0x02e,
	AT_upper_bound        = 0x02f,
	AT_virtual            = 0x030,
	AT_mangled_name       = 0x200,
	AT_overlay_id         = 0x229,
	AT_overlay_name       = 0x22a
};

// The value of an attribute.
class Value {
public:
	Value();
	Value(const Value& rhs);
	~Value();
	Value& operator=(const Value& rhs);
	
	Form form() const;
	bool valid() const;
	
	static Value from_address(u32 address);
	static Value from_reference(u32 reference);
	static Value from_constant_2(u16 constant);
	static Value from_constant_4(u32 constant);
	static Value from_constant_8(u64 constant);
	static Value from_block_2(std::span<const u8> block);
	static Value from_block_4(std::span<const u8> block);
	static Value from_string(std::string_view string); // Must be null terminated.
	
	u32 address() const;
	u32 reference() const;
	u64 constant() const;
	std::span<const u8> block() const;
	std::string_view string() const;
	
	Address address_or_null() const;
	std::optional<u32> reference_or_null() const;
	std::optional<u64> constant_or_null() const;
	std::span<const u8> block_or_null() const;
	std::string_view string_or_null() const;
	
protected:
	u8 m_form = 0;
	union {
		u32 address;
		u32 reference;
		u64 constant;
		struct {
			const u8* begin;
			const u8* end;
		} block;
		struct {
			const char* begin;
			const char* end;
		} string;
	} m_value;
};

struct AttributeTuple {
	u32 offset;
	Attribute attribute;
	Value value;
};

// Parse a single attribute and advance the offset.
Result<AttributeTuple> parse_attribute(std::span<const u8> bytes, u32& offset, u32 importer_flags);

enum LocationOp : u8 {
	OP_REG     = 0x01,
	OP_BASEREG = 0x02,
	OP_ADDR    = 0x03,
	OP_CONST   = 0x04,
	OP_DEREF2  = 0x05,
	OP_DEREF   = 0x06,
	OP_ADD     = 0x07,
	OP_80      = 0x80
};

struct LocationAtom {
	LocationOp op;
	std::optional<u32> value;
};

class LocationDescription {
public:
	static LocationDescription from_block(std::span<const u8> block);
	
	Result<void> print(FILE* out) const;
	
protected:
	Result<LocationAtom> parse_atom(u32& offset) const;
	
	std::span<const u8> m_block;
};

enum FundamentalType : u16 {
	FT_char               = 0x0001,
	FT_signed_char        = 0x0002,
	FT_unsigned_char      = 0x0003,
	FT_short              = 0x0004,
	FT_signed_short       = 0x0005,
	FT_unsigned_short     = 0x0006,
	FT_integer            = 0x0007,
	FT_signed_integer     = 0x0008,
	FT_unsigned_integer   = 0x0009,
	FT_long               = 0x000a,
	FT_signed_long        = 0x000b,
	FT_unsigned_long      = 0x000c,
	FT_pointer            = 0x000d,
	FT_float              = 0x000e,
	FT_dbl_prec_float     = 0x000f,
	FT_ext_prec_float     = 0x0010,
	FT_complex            = 0x0011,
	FT_dbl_prec_complex   = 0x0012,
	FT_void               = 0x0014,
	FT_boolean            = 0x0015,
	FT_ext_prec_complex   = 0x0016,
	FT_label              = 0x0017,
	FT_long_long          = 0x8008,
	FT_signed_long_long   = 0x8108,
	FT_unsigned_long_long = 0x8208,
	FT_int128             = 0xa510
};

enum TypeModifier : u8 {
	MOD_pointer_to   = 0x01,
	MOD_reference_to = 0x02,
	MOD_const        = 0x03,
	MOD_volatile     = 0x04
};

// Parses all the different DWARF type attributes and provides a single API for
// consuming them.
class Type {
public:
	static std::optional<Type> from_attributes(
		const Value& fund_type, const Value& mod_fund_type, const Value& user_def_type, const Value& mod_u_d_type);
	static std::optional<Type> from_attribute_tuple(const AttributeTuple& tuple);
	
	static Type from_fund_type(const Value& fund_type);
	static Type from_mod_fund_type(const Value& mod_fund_type);
	static Type from_user_def_type(const Value& user_def_type);
	static Type from_mod_u_d_type(const Value& mod_u_d_type);
	
	u32 attribute() const;
	Result<FundamentalType> fund_type() const;
	Result<u32> user_def_type() const;
	Result<std::span<const TypeModifier>> modifiers() const;
	
protected:
	u32 m_attribute = 0;
	Value m_value;
};

enum Language : u32 {
	LANG_C89         = 0x00000001,
	LANG_C           = 0x00000002,
	LANG_ADA83       = 0x00000003,
	LANG_C_PLUS_PLUS = 0x00000004,
	LANG_COBOL74     = 0x00000005,
	LANG_COBOL85     = 0x00000006,
	LANG_FORTRAN77   = 0x00000007,
	LANG_FORTRAN90   = 0x00000008,
	LANG_PASCAL83    = 0x00000009,
	LANG_MODULA2     = 0x0000000a,
	LANG_ASSEMBLY    = 0x00008000
};

enum ArrayOrdering : u8 {
	ORD_col_major = 0,
	ORD_row_major = 1
};

enum ArraySubscriptFormatSpecifier : u8 {
	FMT_FT_C_C = 0x0,
	FMT_FT_C_X = 0x1,
	FMT_FT_X_C = 0x2,
	FMT_FT_X_X = 0x3,
	FMT_UT_C_C = 0x4,
	FMT_UT_C_X = 0x5,
	FMT_UT_X_C = 0x6,
	FMT_UT_X_X = 0x7,
	FMT_ET     = 0x8
};

enum class ArrayBoundType {
	NIL,
	CONSTANT,
	LOCATION_DESCRIPTION
};

class ArrayBound {
public:
	static ArrayBound from_constant(u32 constant);
	static ArrayBound from_location_description(LocationDescription location_description);
	
	ArrayBoundType type() const;
	u32 constant() const;
	const LocationDescription& location_description() const;
	
	Result<void> print(FILE* out) const;
	
protected:
	ArrayBoundType m_type = ArrayBoundType::NIL;
	u32 m_constant = 0;
	LocationDescription m_location_description;
};

struct ArraySubscriptItem {
	ArraySubscriptFormatSpecifier specifier;
	Type subscript_index_type;
	ArrayBound lower_bound;
	ArrayBound upper_bound;
	Type element_type;
};

class ArraySubscriptData {
public:
	static ArraySubscriptData from_block(std::span<const u8> block);
	
	u32 size() const;
	
	Result<ArraySubscriptItem> parse_item(u32& offset, u32 importer_flags) const;
	
protected:
	Result<u16> parse_fund_type(u32& offset) const;
	Result<u32> parse_user_def_type(u32& offset) const;
	Result<u32> parse_constant(u32& offset) const;
	Result<LocationDescription> parse_location_description(u32& offset) const;
	
	std::span<const u8> m_block;
};

struct EnumerationElement {
	u32 value;
	std::string name;
};

class EnumerationElementList {
public:
	static EnumerationElementList from_block(std::span<const u8> block);
	
	u32 size() const;
	
	Result<EnumerationElement> parse_element(u32& offset) const;
	
protected:
	std::span<const u8> m_block;
};

const char* form_to_string(u32 value);
const char* attribute_to_string(u32 value);
const char* location_op_to_string(u32 value);
const char* fundamental_type_to_string(u32 value);
const char* type_modifier_to_string(u32 value);
const char* language_to_string(u32 value);
const char* array_ordering_to_string(u32 value);
const char* array_subscript_format_specifier_to_string(u32 value);

}
