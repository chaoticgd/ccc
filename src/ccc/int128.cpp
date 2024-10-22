// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "int128.h"

namespace ccc {

static const char* HEX_DIGITS = "0123456789abcdef";

u128::u128()
	: low(0)
	, high(0) {}

u128::u128(u64 value)
	: low(value)
	, high(0) {}

u128::u128(u64 h, u64 l)
	: low(l)
	, high(h) {}

u128::u128(const u128& rhs)
	: low(rhs.low)
	, high(rhs.high) {}

u128::u128(const s128& rhs)
	: low(static_cast<u64>(rhs.low))
	, high(static_cast<u64>(rhs.high)) {}

u128& u128::operator=(const u128& rhs)
{
	low = rhs.low;
	high = rhs.high;
	return *this;
}

u128 u128::operator+(u128 rhs)
{
	u128 result;
	result.low = low + rhs.low;
	result.high = high + rhs.high + (result.low < low);
	return result;
}

u128 u128::operator-(u128 rhs)
{
	u128 result;
	result.low = low - rhs.low;
	result.high = high - rhs.high - (result.low > low);
	return result;
}

u128 u128::operator~()
{
	u128 result;
	result.low = ~low;
	result.high = ~high;
	return result;
}

u128 u128::operator&(u128 rhs)
{
	u128 result;
	result.low = low & rhs.low;
	result.high = high & rhs.high;
	return result;
}

u128 u128::operator|(u128 rhs)
{
	u128 result;
	result.low = low | rhs.low;
	result.high = high | rhs.high;
	return result;
}

u128 u128::operator^(u128 rhs)
{
	u128 result;
	result.low = low ^ rhs.low;
	result.high = high ^ rhs.high;
	return result;
}

u128 u128::operator<<(u64 bits)
{
	u128 result;
	if (bits == 0) {
		result.low = low;
		result.high = high;
	} else if (bits < 64) {
		result.low = low << bits;
		result.high = (high << bits) | (low >> (64 - bits));
	} else if (bits < 128) {
		result.low = 0;
		result.high = low << (bits - 64);
	} else {
		result.low = 0;
		result.high = 0;
	}
	return result;
}

u128 u128::operator>>(u64 bits)
{
	u128 result;
	if (bits == 0) {
		result.low = low;
		result.high = high;
	} else if (bits < 64) {
		result.low = (low >> bits) | (high << (64 - bits));
		result.high = high >> bits;
	} else if (bits < 128) {
		result.low = high >> (bits - 64);
		result.high = 0;
	} else {
		result.low = 0;
		result.high = 0;
	}
	return result;
}

std::string u128::to_string()
{
	std::string result(32, '\0');
	for (u32 i = 0; i < 16; i++) {
		result[i] = HEX_DIGITS[(high >> ((15 - i) * 4)) & 0xf];
	}
	for (u32 i = 0; i < 16; i++) {
		result[16 + i] = HEX_DIGITS[(low >> ((15 - i) * 4)) & 0xf];
	}
	return result;
}

std::optional<u128> u128::from_string(const std::string&& hex)
{
	u128 result;
	for (u32 i = 0; i < 16; i++) {
		char c = hex[i];
		if (c >= '0' && c <= '9') {
			result.high |= static_cast<u64>(c - '0') << ((15 - i) * 4);
		} else if (c >= 'A' && c <= 'F') {
			result.high |= static_cast<u64>(10 + c - 'A') << ((15 - i) * 4);
		} else if (c >= 'a' && c <= 'f') {
			result.high |= static_cast<u64>(10 + c - 'a') << ((15 - i) * 4);
		} else {
			return std::nullopt;
		}
	}
	for (u32 i = 0; i < 16; i++) {
		char c = hex[16 + i];
		if (c >= '0' && c <= '9') {
			result.low |= static_cast<u64>(c - '0') << ((15 - i) * 4);
		} else if (c >= 'A' && c <= 'F') {
			result.low |= static_cast<u64>(10 + c - 'A') << ((15 - i) * 4);
		} else if (c >= 'a' && c <= 'f') {
			result.low |= static_cast<u64>(10 + c - 'a') << ((15 - i) * 4);
		} else {
			return std::nullopt;
		}
	}
	return result;
}

s128::s128()
	: low(0)
	, high(0) {}

s128::s128(s64 value)
	: low(static_cast<u64>(value))
	, high((value < 0) ? 0xffffffffffffffff : 0) {}

s128::s128(u64 h, u64 l)
	: low(l)
	, high(h) {}

s128::s128(const u128& rhs)
	: low(static_cast<s64>(rhs.low))
	, high(static_cast<s64>(rhs.high)) {}

s128::s128(const s128& rhs)
	: low(rhs.low)
	, high(rhs.high) {}

s128& s128::operator=(const s128& rhs)
{
	low = rhs.low;
	high = rhs.high;
	return *this;
}

s128 s128::operator+(s128 rhs)
{
	s128 result;
	result.low = low + rhs.low;
	result.high = high + rhs.high + (result.low < low);
	return result;
}

s128 s128::operator-(s128 rhs)
{
	s128 result;
	result.low = low - rhs.low;
	result.high = high - rhs.high - (result.low > low);
	return result;
}

s128 s128::operator~()
{
	s128 result;
	result.low = ~low;
	result.high = ~high;
	return result;
}

s128 s128::operator&(s128 rhs)
{
	s128 result;
	result.low = low & rhs.low;
	result.high = high & rhs.high;
	return result;
}

s128 s128::operator|(s128 rhs)
{
	s128 result;
	result.low = low | rhs.low;
	result.high = high | rhs.high;
	return result;
}

s128 s128::operator^(s128 rhs)
{
	s128 result;
	result.low = low ^ rhs.low;
	result.high = high ^ rhs.high;
	return result;
}

s128 s128::operator<<(u64 bits)
{
	s128 result;
	if (bits == 0) {
		result.low = low;
		result.high = high;
	} else if (bits < 64) {
		result.low = low << bits;
		result.high = (high << bits) | (low >> (64 - bits));
	} else if (bits < 128) {
		result.low = 0;
		result.high = low << (bits - 64);
	} else {
		result.low = 0;
		result.high = 0;
	}
	return result;
}

s128 s128::operator>>(u64 bits)
{
	s128 result;
	if (bits == 0) {
		result.low = low;
		result.high = high;
	} else if (bits < 64) {
		result.low = (low >> bits)
			| (high << (64 - bits));
		result.high = (high >> bits)
			| ((high > 0x7fffffffffffffff) ? (((static_cast<u64>(1) << bits) - 1) << (64 - bits)) : 0);
	} else if (bits == 64) {
		result.low = high;
		result.high = (high > 0x7fffffffffffffff) ? 0xffffffffffffffff : 0;
	} else if (bits < 128) {
		result.low = (high >> (bits - 64))
			| ((high > 0x7fffffffffffffff) ? (((static_cast<u64>(1) << (bits - 64)) - 1) << (128 - bits)) : 0);
		result.high = (high > 0x7fffffffffffffff) ? 0xffffffffffffffff : 0;
	} else {
		result.low = (high > 0x7fffffffffffffff) ? 0xffffffffffffffff : 0;
		result.high = (high > 0x7fffffffffffffff) ? 0xffffffffffffffff : 0;
	}
	return result;
}

std::string s128::to_string()
{
	std::string result(32, '\0');
	for (u32 i = 0; i < 16; i++) {
		result[i] = HEX_DIGITS[(high >> ((15 - i) * 4)) & 0xf];
	}
	for (u32 i = 0; i < 16; i++) {
		result[16 + i] = HEX_DIGITS[(low >> ((15 - i) * 4)) & 0xf];
	}
	return result;
}

std::optional<s128> s128::from_string(const std::string&& hex)
{
	s128 result;
	for (u32 i = 0; i < 16; i++) {
		char c = hex[i];
		if (c >= '0' && c <= '9') {
			result.high |= static_cast<u64>(c - '0') << ((15 - i) * 4);
		} else if (c >= 'A' && c <= 'F') {
			result.high |= static_cast<u64>(10 + c - 'A') << ((15 - i) * 4);
		} else if (c >= 'a' && c <= 'f') {
			result.high |= static_cast<u64>(10 + c - 'a') << ((15 - i) * 4);
		} else {
			return std::nullopt;
		}
	}
	for (u32 i = 0; i < 16; i++) {
		char c = hex[16 + i];
		if (c >= '0' && c <= '9') {
			result.low |= static_cast<u64>(c - '0') << ((15 - i) * 4);
		} else if (c >= 'A' && c <= 'F') {
			result.low |= static_cast<u64>(10 + c - 'A') << ((15 - i) * 4);
		} else if (c >= 'a' && c <= 'f') {
			result.low |= static_cast<u64>(10 + c - 'a') << ((15 - i) * 4);
		} else {
			return std::nullopt;
		}
	}
	return result;
}

}
