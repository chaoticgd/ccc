// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "util.h"

#include <map>

namespace ccc::dwarf {

enum Tag : u16 {
	TAG_padding                = 0x0000,
	TAG_array_type             = 0x0001,
	TAG_class_type             = 0x0002,
	TAG_entry_point            = 0x0003,
	TAG_enumeration_type       = 0x0004,
	TAG_formal_parameter       = 0x0005,
	TAG_global_subroutine      = 0x0006,
	TAG_global_variable        = 0x0007,
	TAG_label                  = 0x000a,
	TAG_lexical_block          = 0x000b,
	TAG_local_variable         = 0x000c,
	TAG_member                 = 0x000d,
	TAG_pointer_type           = 0x000f,
	TAG_reference_type         = 0x0010,
	TAG_compile_unit           = 0x0011,
	TAG_string_type            = 0x0012,
	TAG_structure_type         = 0x0013,
	TAG_subroutine             = 0x0014,
	TAG_subroutine_type        = 0x0015,
	TAG_typedef                = 0x0016,
	TAG_union_type             = 0x0017,
	TAG_unspecified_parameters = 0x0018,
	TAG_variant                = 0x0019,
	TAG_common_block           = 0x001a,
	TAG_common_inclusion       = 0x001b,
	TAG_inheritance            = 0x001c,
	TAG_inlined_subroutine     = 0x001d,
	TAG_module                 = 0x001e,
	TAG_ptr_to_member_type     = 0x001f,
	TAG_set_type               = 0x0020,
	TAG_subrange_type          = 0x0021,
	TAG_with_stmt              = 0x0022,
	
	/* GNU extensions */
	
	TAG_format_label = 0x8000,  /* for FORTRAN 77 and Fortran 90 */
	TAG_namelist = 0x8001,  /* For Fortran 90 */
	TAG_function_template = 0x8002,  /* for C++ */
	TAG_class_template = 0x8003   /* for C++ */
};

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
	AT_lo_user            = 0x200,
	AT_hi_user            = 0x3ff
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
	static Value from_string(const char* string);
	
	u32 address() const;
	u32 reference() const;
	u64 constant() const;
	std::span<const u8> block() const;
	const char* string() const;
	
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
		const char* string;
	} m_value;
};

struct AttributeTuple {
	u32 offset;
	Attribute attribute;
	Value value;
};

struct RequiredAttribute {
	Attribute attribute;
	u32 valid_forms;
	u32 index;
};

using RequiredAttributes = std::map<Attribute, RequiredAttribute>;

// Represents a Debugging Information Entry. Intended to be used to
// incrementally parse a .debug section.
class DIE {
public:
	// Parse a single DIE. Will return std::nullopt for padding entries smaller
	// than 8 bytes.
	static Result<std::optional<DIE>> parse(std::span<const u8> debug, u32 offset, u32 importer_flags);
	
	// Generate a map of attributes to read, to be used for parsing attributes.
	static RequiredAttributes require_attributes(std::span<const RequiredAttribute> input);
	
	Result<std::optional<DIE>> first_child() const;
	Result<std::optional<DIE>> sibling() const;
	
	u32 offset() const;
	Tag tag() const;
	
	// Parse the attributes, and output the ones specified by the required parameter.
	Result<void> attributes(std::span<Value*> output, const RequiredAttributes& required) const;
	
	// Parse the attributes, and output them all in order.
	Result<std::vector<AttributeTuple>> all_attributes() const;
	
protected:
	// Parse a single attribute and advance the offset.
	Result<AttributeTuple> parse_attribute(u32& offset) const;
	
	std::span<const u8> m_debug;
	u32 m_offset;
	u32 m_length;
	Tag m_tag;
	u32 m_importer_flags;
};

class SectionReader {
public:
	SectionReader(std::span<const u8> debug, std::span<const u8> line);
	
	Result<DIE> first_die(u32 importer_flags) const;
	
	Result<void> print_dies(FILE* out, DIE die, s32 depth) const;
	Result<void> print_attributes(FILE* out, const DIE& die) const;
	
protected:
	std::span<const u8> m_debug;
	std::span<const u8> m_line;
};

const char* tag_to_string(u32 tag);
const char* form_to_string(u32 form);
const char* attribute_to_string(u32 attribute);

}
