// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "dwarf_section.h"

namespace ccc::dwarf {

class SymbolPrinter {
public:
	SymbolPrinter(SectionReader& reader);
	
	Result<void> print_dies(FILE* out, DIE die, s32 depth) const;
	Result<void> print_attributes(FILE* out, const DIE& die) const;
	Result<void> print_reference(FILE* out, u32 reference) const;
	Result<void> print_block(FILE* out, u32 offset, Attribute attribute, const Value& value) const;
	Result<void> print_constant(FILE* out, Attribute attribute, const Value& value) const;
	Result<void> print_type(FILE* out, const Type& type) const;
	Result<void> print_subscr_data(FILE* out, const ArraySubscriptData& subscript_data) const;
	Result<void> print_enumeration_element_list(FILE* out, const EnumerationElementList& element_list) const;
	
protected:
	SectionReader& m_reader;
};

}
