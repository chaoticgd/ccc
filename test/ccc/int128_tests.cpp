// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "ccc/int128.h"

using namespace ccc;

TEST(CCCInt128, UnsignedAdd)
{
	u128 a(0xf0f0f0f0f0f0f0f0, 0xf0f0f0f0f0f0f0f0);
	u128 b(0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f);
	u128 c(0xffffffffffffffff, 0xffffffffffffffff);
	u128 d(0x0000000000000000, 0xffffffffffffffff);
	u128 e(0x0000000000000000, 0x0000000000000001);
	u128 f(0x0000000000000001, 0x0000000000000000);
	EXPECT_EQ(a + b, c);
	EXPECT_EQ(d + e, f);
}

TEST(CCCInt128, UnsignedSubtract)
{
	u128 a(0xf0f0f0f0f0f0f0f0, 0xf0f0f0f0f0f0f0f0);
	u128 b(0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f);
	u128 c(0xe1e1e1e1e1e1e1e1, 0xe1e1e1e1e1e1e1e1);
	u128 d(0x0000000000000000, 0x0000000000000001);
	u128 e(0x0000000000000000, 0x0000000000000002);
	u128 f(0xffffffffffffffff, 0xffffffffffffffff);
	EXPECT_EQ(a - b, c);
	EXPECT_EQ(d - e, f);
}

TEST(CCCInt128, UnsignedNot)
{
	u128 a(0xf0f0f0f0f0f0f0f0, 0xf0f0f0f0f0f0f0f0);
	u128 b(0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f);
	EXPECT_EQ(a, ~b);
}

TEST(CCCInt128, UnsignedAnd)
{
	u128 a(0x00000000ffffffff, 0xffffffff00000000);
	u128 b(0xf0f0f0f0f0f0f0f0, 0x0f0f0f0f0f0f0f0f);
	u128 c(0x00000000f0f0f0f0, 0x0f0f0f0f00000000);
	EXPECT_EQ(a & b, c);
}

TEST(CCCInt128, UnsignedOr)
{
	u128 a(0x00000000ffffffff, 0xffffffff00000000);
	u128 b(0xf0f0f0f0f0f0f0f0, 0x0f0f0f0f0f0f0f0f);
	u128 c(0xf0f0f0f0ffffffff, 0xffffffff0f0f0f0f);
	EXPECT_EQ(a | b, c);
}

TEST(CCCInt128, UnsignedXor)
{
	u128 a(0x00000000ffffffff, 0xffffffff00000000);
	u128 b(0xf0f0f0f0f0f0f0f0, 0x0f0f0f0f0f0f0f0f);
	u128 c(0xf0f0f0f00f0f0f0f, 0xf0f0f0f00f0f0f0f);
	EXPECT_EQ(a ^ b, c);
}

TEST(CCCInt128, UnsignedLeftShift)
{
	u128 a(0xff00ff0000000000, 0xbbffffff000000ee);
	u128 b(0xff0000000000bbff, 0xffff000000ee0000);
	u128 c(0xbbffffff000000ee, 0x0000000000000000);
	u128 d(0xffffff000000ee00, 0x0000000000000000);
	u128 e(0x0000000000000000, 0x0000000000000000);
	EXPECT_EQ(a << 0, a);
	EXPECT_EQ(a << 16, b);
	EXPECT_EQ(a << 64, c);
	EXPECT_EQ(a << 72, d);
	EXPECT_EQ(a << 128, e);
}

TEST(CCCInt128, UnsignedRightShift)
{
	u128 a(0xff00ff0000000000, 0xbbffffff000000ee);
	u128 b(0x0000ff00ff000000, 0x0000bbffffff0000);
	u128 c(0x0000000000000000, 0xff00ff0000000000);
	u128 d(0x0000000000000000, 0x00ff00ff00000000);
	u128 e(0x0000000000000000, 0x0000000000000000);
	EXPECT_EQ(a >> 0, a);
	EXPECT_EQ(a >> 16, b);
	EXPECT_EQ(a >> 64, c);
	EXPECT_EQ(a >> 72, d);
	EXPECT_EQ(a >> 128, e);
}

TEST(CCCInt128, UnsignedToString)
{
	u128 a(0xff00ff0000000000, 0xbbffffff000000ee);
	EXPECT_EQ(a.to_string(), "ff00ff0000000000bbffffff000000ee");
}

TEST(CCCInt128, UnsignedFromString)
{
	std::optional<u128> a(u128(0x123456789abcdef0, 0xffeeddccbbaa9988));
	std::optional<u128> b(std::nullopt);
	EXPECT_EQ(a, u128::from_string("123456789abcdef0ffEEddCCbbAA9988"));
	EXPECT_EQ(b, u128::from_string("hello"));
}

TEST(CCCInt128, SignedAdd)
{
	s128 a(0xf0f0f0f0f0f0f0f0, 0xf0f0f0f0f0f0f0f0);
	s128 b(0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f);
	s128 c(0xffffffffffffffff, 0xffffffffffffffff);
	s128 d(0x0000000000000000, 0xffffffffffffffff);
	s128 e(0x0000000000000000, 0x0000000000000001);
	s128 f(0x0000000000000001, 0x0000000000000000);
	EXPECT_EQ(a + b, c);
	EXPECT_EQ(d + e, f);
}

TEST(CCCInt128, SignedSubtract)
{
	s128 a(0xf0f0f0f0f0f0f0f0, 0xf0f0f0f0f0f0f0f0);
	s128 b(0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f);
	s128 c(0xe1e1e1e1e1e1e1e1, 0xe1e1e1e1e1e1e1e1);
	s128 d(0x0000000000000000, 0x0000000000000001);
	s128 e(0x0000000000000000, 0x0000000000000002);
	s128 f(0xffffffffffffffff, 0xffffffffffffffff);
	EXPECT_EQ(a - b, c);
	EXPECT_EQ(d - e, f);
}

TEST(CCCInt128, SignedNot)
{
	s128 a(0xf0f0f0f0f0f0f0f0, 0xf0f0f0f0f0f0f0f0);
	s128 b(0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f);
	EXPECT_EQ(a, ~b);
}

TEST(CCCInt128, SignedAnd)
{
	s128 a(0x00000000ffffffff, 0xffffffff00000000);
	s128 b(0xf0f0f0f0f0f0f0f0, 0x0f0f0f0f0f0f0f0f);
	s128 c(0x00000000f0f0f0f0, 0x0f0f0f0f00000000);
	EXPECT_EQ(a & b, c);
}

TEST(CCCInt128, SignedOr)
{
	s128 a(0x00000000ffffffff, 0xffffffff00000000);
	s128 b(0xf0f0f0f0f0f0f0f0, 0x0f0f0f0f0f0f0f0f);
	s128 c(0xf0f0f0f0ffffffff, 0xffffffff0f0f0f0f);
	EXPECT_EQ(a | b, c);
}

TEST(CCCInt128, SignedXor)
{
	s128 a(0x00000000ffffffff, 0xffffffff00000000);
	s128 b(0xf0f0f0f0f0f0f0f0, 0x0f0f0f0f0f0f0f0f);
	s128 c(0xf0f0f0f00f0f0f0f, 0xf0f0f0f00f0f0f0f);
	EXPECT_EQ(a ^ b, c);
}

TEST(CCCInt128, SignedLeftShift)
{
	s128 a(0xff00ff0000000000, 0xbbffffff000000ee);
	s128 b(0xff0000000000bbff, 0xffff000000ee0000);
	s128 c(0xbbffffff000000ee, 0x0000000000000000);
	s128 d(0xffffff000000ee00, 0x0000000000000000);
	s128 e(0x0000000000000000, 0x0000000000000000);
	EXPECT_EQ(a << 0, a);
	EXPECT_EQ(a << 16, b);
	EXPECT_EQ(a << 64, c);
	EXPECT_EQ(a << 72, d);
	EXPECT_EQ(a << 128, e);
}

TEST(CCCInt128, SignedRightShiftPositive)
{
	s128 a(0x7f00ff0000000000, 0xbbffffff000000ee);
	s128 b(0x00007f00ff000000, 0x0000bbffffff0000);
	s128 c(0x0000000000000000, 0x7f00ff0000000000);
	s128 d(0x0000000000000000, 0x007f00ff00000000);
	s128 e(0x0000000000000000, 0x0000000000000000);
	EXPECT_EQ(a >> 0, a);
	EXPECT_EQ(a >> 16, b);
	EXPECT_EQ(a >> 64, c);
	EXPECT_EQ(a >> 72, d);
	EXPECT_EQ(a >> 128, e);
}

TEST(CCCInt128, SignedRightShiftNegative)
{
	s128 a(0xff00ff0000000000, 0xbbffffff000000ee);
	s128 b(0xffffff00ff000000, 0x0000bbffffff0000);
	s128 c(0xffffffffffffffff, 0xff00ff0000000000);
	s128 d(0xffffffffffffffff, 0xffff00ff00000000);
	s128 e(0xffffffffffffffff, 0xffffffffffffffff);
	EXPECT_EQ(a >> 0, a);
	EXPECT_EQ(a >> 16, b);
	EXPECT_EQ(a >> 64, c);
	EXPECT_EQ(a >> 72, d);
	EXPECT_EQ(a >> 128, e);
}

TEST(CCCInt128, SignedToString)
{
	s128 a(0xff00ff0000000000, 0xbbffffff000000ee);
	EXPECT_EQ(a.to_string(), "ff00ff0000000000bbffffff000000ee");
}

TEST(CCCInt128, SignedFromString)
{
	std::optional<s128> a(s128(0x123456789abcdef0, 0xffeeddccbbaa9988));
	std::optional<s128> b(std::nullopt);
	EXPECT_EQ(a, s128::from_string("123456789abcdef0ffEEddCCbbAA9988"));
	EXPECT_EQ(b, s128::from_string("hello"));
}
