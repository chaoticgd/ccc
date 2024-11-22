// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "dwarf_forge.h"

namespace ccc::dwarf {

void Forge::begin_die(std::string id, Tag tag)
{
	u32 offset = push<u32>(0xbaadbaad);
	push<u16>(tag);
	
	push<u16>((AT_sibling << 4) | FORM_REF);
	push<u32>(0xbaadbaad);
	
	CCC_ABORT_IF_FALSE(!m_prev_siblings.empty(), "Unmatched begin_children/end_children calls.");
	
	// Link the sibling attribute of the previous DIE to this one.
	if (m_prev_siblings.back().has_value()) {
		u32 prev_die = *m_prev_siblings.back();
		CCC_ASSERT(prev_die + 12 <= m_debug.size())
		memcpy(&m_debug[prev_die + 8], &offset, sizeof(u32));
	}
	
	m_prev_siblings.back() = offset;
	
	m_dies.emplace(std::move(id), static_cast<u32>(m_debug.size()));
}

void Forge::end_die()
{
	// Fill in the size field.
	CCC_ASSERT(m_prev_siblings.back().has_value());
	u32 begin_offset = *m_prev_siblings.back();
	CCC_ASSERT(begin_offset + 4 <= m_debug.size());
	u32 size = static_cast<u32>(m_debug.size()) - begin_offset;
	memcpy(&m_debug[begin_offset], &size, sizeof(u32));
}

void Forge::address(Attribute attribute, u32 address)
{
	push<u16>((attribute << 4) | FORM_ADDR);
	push<u32>(address);
}

void Forge::reference(Attribute attribute, std::string id)
{
	push<u16>((attribute << 4) | FORM_REF);
	u32 offset = push<u32>(0xbaadbaad);
	m_references.emplace(offset, id);
}

void Forge::constant_2(Attribute attribute, u16 constant)
{
	push<u16>((attribute << 4) | FORM_DATA2);
	push<u16>(constant);
}

void Forge::constant_4(Attribute attribute, u32 constant)
{
	push<u16>((attribute << 4) | FORM_DATA4);
	push<u32>(constant);
}

void Forge::constant_8(Attribute attribute, u64 constant)
{
	push<u16>((attribute << 4) | FORM_DATA8);
	push<u64>(constant);
}

void Forge::block_2(Attribute attribute, std::initializer_list<u8> block, std::initializer_list<BlockId> ids)
{
	CCC_ASSERT(block.size() <= UINT16_MAX);
	
	push<u16>((attribute << 4) | FORM_BLOCK2);
	push<u16>(static_cast<u16>(block.size()));
	u32 offset = static_cast<u32>(m_debug.size());
	for (u8 byte : block) {
		push<u8>(byte);
	}
	
	for (BlockId id : ids) {
		m_references.emplace(offset + id.offset, id.id);
	}
}

void Forge::block_4(Attribute attribute, std::initializer_list<u8> block, std::initializer_list<BlockId> ids)
{
	CCC_ASSERT(block.size() <= UINT32_MAX);
	
	push<u16>((attribute << 4) | FORM_BLOCK4);
	push<u32>(static_cast<u32>(block.size()));
	u32 offset = static_cast<u32>(m_debug.size());
	for (u8 byte : block) {
		push<u8>(byte);
	}
	
	for (BlockId id : ids) {
		m_references.emplace(offset + id.offset, id.id);
	}
}

void Forge::string(Attribute attribute, std::string_view string)
{
	push<u16>((attribute << 4) | FORM_STRING);
	for (char c : string) {
		push<char>(c);
	}
	push<char>('\0');
}

void Forge::begin_children()
{
	m_prev_siblings.emplace_back(std::nullopt);
}

void Forge::end_children()
{
	// Add a null entry and link it up if necessary.
	if (m_prev_siblings.back().has_value()) {
		u32 offset = push<u32>(6);
		push<u16>(0);
		
		u32 prev_die = *m_prev_siblings.back();
		CCC_ASSERT(prev_die + 12 <= m_debug.size())
		memcpy(&m_debug[prev_die + 8], &offset, sizeof(u32));
	}
	
	m_prev_siblings.pop_back();
}

std::vector<u8> Forge::finish()
{
	// Link up all the references.
	for (auto& [reference_offset, id] : m_references) {
		auto die = m_dies.find(id);
		CCC_ASSERT(die != m_dies.end());
		CCC_ASSERT(reference_offset + 4 <= m_debug.size());
		memcpy(&m_debug[reference_offset], &die->second, sizeof(u32));
	}
	
	// Return the finished section data.
	return std::move(m_debug);
}

}
