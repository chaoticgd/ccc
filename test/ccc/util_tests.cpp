// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "ccc/util.h"

using namespace ccc;

#define DEREF_OR_ZERO(x) ((x) ? (*(x)) : 0)

TEST(CCCUtil, GetAligned)
{
	alignas(8) u8 data[7] = {1, 0, 0, 1, 0, 0, 1};
	
	EXPECT_EQ(DEREF_OR_ZERO(get_aligned<u32>(data, 0)), 0x01000001);
	EXPECT_EQ(get_aligned<u32>(data, 1), nullptr);
	EXPECT_EQ(get_aligned<u32>(data, 4), nullptr);
	EXPECT_EQ(get_aligned<u32>(data, 7), nullptr);
	EXPECT_EQ(get_aligned<u32>(data, 8), nullptr);
	EXPECT_EQ(get_aligned<u32>(data, 0xfffffffffffffffc), nullptr);
}

TEST(CCCUtil, GetUnaligned)
{
	alignas(8) u8 data[7] = {1, 2, 3, 4, 5, 6, 7};
	
	EXPECT_EQ(DEREF_OR_ZERO(get_unaligned<u8>(data, 0)), 1);
	EXPECT_EQ(DEREF_OR_ZERO(get_unaligned<u8>(data, 1)), 2);
	EXPECT_EQ(get_unaligned<u8>(data, 8), nullptr);
	EXPECT_EQ(get_unaligned<u8>(data, 0xffffffffffffffff), nullptr);
}

TEST(CCCUtil, CopyUnaligned)
{
	alignas(8) u8 data[7] = {1, 0, 0, 1, 0, 0, 1};
	
	EXPECT_EQ(DEREF_OR_ZERO(copy_unaligned<u32>(data, 0)), 0x01000001);
	EXPECT_EQ(DEREF_OR_ZERO(copy_unaligned<u32>(data, 3)), 0x01000001);
	EXPECT_EQ(copy_unaligned<u32>(data, 4).has_value(), false);
	EXPECT_EQ(copy_unaligned<u32>(data, 8).has_value(), false);
	EXPECT_EQ(copy_unaligned<u32>(data, 0xffffffffffffffff).has_value(), false);
}

TEST(CCCUtil, GetString)
{
	alignas(8) u8 data[7] = {'h', 'e', 'l', 'l', 'o', '\0', '!'};
	
	EXPECT_EQ(get_string(data, 0), std::string("hello"));
	EXPECT_EQ(get_string(data, 5), std::string(""));
	EXPECT_EQ(get_string(data, 6), std::nullopt);
	EXPECT_EQ(get_string(data, 7), std::nullopt);
	EXPECT_EQ(get_string(data, 0xffffffffffffffff), std::nullopt);
}
