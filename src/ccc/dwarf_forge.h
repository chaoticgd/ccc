// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "dwarf_section.h"

namespace ccc::dwarf {

// DWARF 1 section builder for testing purposes.
class Forge {
public:
	// Start crafting a DIE.
	void begin_die(std::string id, Tag tag);
	
	// Finish crafting a DIE.
	void end_die();
	
	// Used for specifying references inside a block attribute that need to be
	// linked up.
	struct BlockId {
		u32 offset;
		std::string id;
	};
	
	// Craft attributes. These should be called between a pair of begin_die and
	// end_die calls.
	void address(Attribute attribute, u32 address);
	void reference(Attribute attribute, std::string id);
	void constant_2(Attribute attribute, u16 constant);
	void constant_4(Attribute attribute, u32 constant);
	void constant_8(Attribute attribute, u64 constant);
	void block_2(Attribute attribute, std::initializer_list<u8> block, std::initializer_list<BlockId> ids = {});
	void block_4(Attribute attribute, std::initializer_list<u8> block, std::initializer_list<BlockId> ids = {});
	void string(Attribute attribute, std::string_view string);
	
	// Make the next DIEs children of the last DIE crafted.
	void begin_children();
	
	// Go up one level.
	void end_children();
	
	// Output the result.
	std::vector<u8> finish();
	
private:
	template <typename T>
	u32 push(T value)
	{
		size_t offset = m_debug.size();
		m_debug.resize(offset + sizeof(T));
		memcpy(&m_debug[offset], &value, sizeof(T));
		return static_cast<u32>(offset);
	}
	
	std::vector<u8> m_debug;
	
	// These are used to patch references to DIEs.
	std::map<std::string, u32> m_dies;
	std::map<u32, std::string> m_references;
	std::vector<std::optional<u32>> m_prev_siblings = {std::nullopt};
};

}
