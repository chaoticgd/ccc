// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "dwarf_attributes.h"
#include "util.h"

#include <map>
#include <initializer_list>

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
	TAG_overlay                = 0x4080,
	TAG_format_label           = 0x8000,
	TAG_namelist               = 0x8001,
	TAG_function_template      = 0x8002,
	TAG_class_template         = 0x8003
};

enum AttributeFormatFlag {
	// Process the attribute normally; don't generate an error if the attribute
	// is missing.
	AFF_NONE = 0,
	
	// Generate an error if an attribute is missing. Note that even though the
	// specification for DWARF 1 says that all attributes are optional, for our
	// purposes this is still quite useful.
	AFF_REQUIRED = 1 << 0
};

struct AttributeFormat {
	Attribute attribute;
	u32 index;
	u32 valid_forms;
	u32 flags;
};

using AttributeListFormat = std::map<Attribute, AttributeFormat>;

// Represents a Debugging Information Entry. Intended to be used to
// incrementally parse a .debug section.
class DIE {
public:
	// Parse a single DIE. Will return std::nullopt for padding entries smaller
	// than 8 bytes.
	static Result<std::optional<DIE>> parse(std::span<const u8> debug, u32 offset, u32 importer_flags);
	
	// Generate a map of attributes to read, to be used for parsing attributes.
	static AttributeListFormat attribute_list_format(std::vector<AttributeFormat> input);
	
	// Generate a specification for an attribute to read.
	static AttributeFormat attribute_format(
		Attribute attribute, std::vector<u32> valid_forms, u32 flags = AFF_NONE);
	
	Result<std::optional<DIE>> first_child() const;
	Result<std::optional<DIE>> sibling() const;
	
	u32 offset() const;
	Tag tag() const;
	
	// Parse the attributes, and output the ones specified by the format parameter.
	Result<void> scan_attributes(const AttributeListFormat& format, std::initializer_list<Value*> output) const;
	
	// Parse the attributes, and output them all in order.
	Result<std::vector<AttributeTuple>> all_attributes() const;
	
protected:
	std::span<const u8> m_debug;
	u32 m_offset;
	u32 m_length;
	Tag m_tag;
	u32 m_importer_flags;
};

class SectionReader {
public:
	SectionReader(std::span<const u8> debug, std::span<const u8> line, u32 importer_flags);
	
	Result<DIE> first_die() const;
	Result<std::optional<DIE>> die_at(u32 offset) const;
	
	u32 importer_flags() const;
	
protected:
	std::span<const u8> m_debug;
	std::span<const u8> m_line;
	u32 m_importer_flags;
};

const char* tag_to_string(u32 tag);

}
