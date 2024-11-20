// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "dwarf_attributes.h"
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
	TAG_overlay                = 0x4080,
	TAG_format_label           = 0x8000,
	TAG_namelist               = 0x8001,
	TAG_function_template      = 0x8002,
	TAG_class_template         = 0x8003
};

struct AttributeTuple {
	u32 offset;
	Attribute attribute;
	Value value;
};

struct AttributeSpec {
	Attribute attribute;
	u32 index;
	bool required;
	u32 valid_forms;
};

using AttributesSpec = std::map<Attribute, AttributeSpec>;

// Represents a Debugging Information Entry. Intended to be used to
// incrementally parse a .debug section.
class DIE {
public:
	// Parse a single DIE. Will return std::nullopt for padding entries smaller
	// than 8 bytes.
	static Result<std::optional<DIE>> parse(std::span<const u8> debug, u32 offset, u32 importer_flags);
	
	// Generate a map of attributes to read, to be used for parsing attributes.
	static inline AttributesSpec specify_attributes(std::vector<AttributeSpec> input)
	{
		AttributesSpec output;
		
		for (u32 i = 0; i < static_cast<u32>(input.size()); i++) {
			AttributeSpec& attribute = output.emplace(input[i].attribute, input[i]).first->second;
			attribute.index = i;
		}
		
		return output;
	}
	
	// Generate a specification for a required attribute.
	static inline AttributeSpec required_attribute(Attribute attribute, std::vector<u32> valid_forms)
	{
		AttributeSpec result;
		result.attribute = attribute;
		result.required = true;
		result.valid_forms = 0;
		for (u32 form : valid_forms) {
			result.valid_forms |= 1 << form;
		}
		return result;
	}
	
	// Generate a specification for an optional attribute.
	static inline AttributeSpec optional_attribute(Attribute attribute, std::vector<u32> valid_forms)
	{
		AttributeSpec result;
		result.attribute = attribute;
		result.required = false;
		result.valid_forms = 0;
		for (u32 form : valid_forms) {
			result.valid_forms |= 1 << form;
		}
		return result;
	}
	
	Result<std::optional<DIE>> first_child() const;
	Result<std::optional<DIE>> sibling() const;
	
	u32 offset() const;
	Tag tag() const;
	
	// Parse the attributes, and output the ones specified by the required parameter.
	Result<void> attributes(const AttributesSpec& spec, std::vector<Value*> output) const;
	
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
	Result<void> print_reference(FILE* out, u32 reference) const;
	Result<void> print_block(FILE* out, u32 offset, Attribute attribute, const Value& value) const;
	Result<void> print_constant(FILE* out, Attribute attribute, const Value& value) const;
	Result<void> print_type(FILE* out, const Type& type) const;
	
protected:
	std::span<const u8> m_debug;
	std::span<const u8> m_line;
};

const char* tag_to_string(u32 tag);

}
