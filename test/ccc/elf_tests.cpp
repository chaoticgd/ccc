// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "ccc/elf.h"

using namespace ccc;

TEST(CCCElf, GNULinkOnceSections)
{
	std::optional<ElfLinkOnceSection> bss = ElfFile::parse_link_once_section_name(".gnu.linkonce.b.MyBSSGlobal");
	std::optional<ElfLinkOnceSection> data = ElfFile::parse_link_once_section_name(".gnu.linkonce.d.MyDataGlobal");
	std::optional<ElfLinkOnceSection> sdata = ElfFile::parse_link_once_section_name(".gnu.linkonce.s.MySmallGlobal");
	std::optional<ElfLinkOnceSection> sbss = ElfFile::parse_link_once_section_name(".gnu.linkonce.sb.MySmallBSSGlobal");
	std::optional<ElfLinkOnceSection> text = ElfFile::parse_link_once_section_name(".gnu.linkonce.t.MyFunction");
	
	ASSERT_TRUE(bss.has_value());
	ASSERT_TRUE(data.has_value());
	ASSERT_TRUE(sdata.has_value());
	ASSERT_TRUE(sbss.has_value());
	ASSERT_TRUE(text.has_value());
	
	ASSERT_EQ(bss->is_text, false);
	ASSERT_EQ(data->is_text, false);
	ASSERT_EQ(sdata->is_text, false);
	ASSERT_EQ(sbss->is_text, false);
	ASSERT_EQ(text->is_text, true);
	
	ASSERT_EQ(bss->location, GlobalStorageLocation::BSS);
	ASSERT_EQ(data->location, GlobalStorageLocation::DATA);
	ASSERT_EQ(sdata->location, GlobalStorageLocation::SDATA);
	ASSERT_EQ(sbss->location, GlobalStorageLocation::SBSS);
	ASSERT_EQ(text->location, GlobalStorageLocation::NIL);
	
	ASSERT_EQ(bss->symbol_name, "MyBSSGlobal");
	ASSERT_EQ(data->symbol_name, "MyDataGlobal");
	ASSERT_EQ(sdata->symbol_name, "MySmallGlobal");
	ASSERT_EQ(sbss->symbol_name, "MySmallBSSGlobal");
	ASSERT_EQ(text->symbol_name, "MyFunction");
}

TEST(CCCElf, BadGNULinkOnceSections)
{
	std::optional<ElfLinkOnceSection> truncated_1 = ElfFile::parse_link_once_section_name(".gnu.linkonce.");
	std::optional<ElfLinkOnceSection> truncated_2 = ElfFile::parse_link_once_section_name(".gnu.linkonce.t");
	std::optional<ElfLinkOnceSection> truncated_3 = ElfFile::parse_link_once_section_name(".gnu.linkonce.t.");
	std::optional<ElfLinkOnceSection> invalid_type_1 = ElfFile::parse_link_once_section_name(".gnu.linkonce.a.Hello");
	std::optional<ElfLinkOnceSection> invalid_type_2 = ElfFile::parse_link_once_section_name(".gnu.linkonce.sa.Hello");
	
	ASSERT_FALSE(truncated_1.has_value());
	ASSERT_FALSE(truncated_2.has_value());
	ASSERT_FALSE(truncated_3.has_value());
	ASSERT_FALSE(invalid_type_1.has_value());
	ASSERT_FALSE(invalid_type_2.has_value());
}
