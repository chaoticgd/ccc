// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "util.h"

namespace ccc {

struct u128;
struct s128;

struct u128 {
	u64 low;
	u64 high;
	
	u128();
	u128(u64 value);
	u128(u64 h, u64 l);
	u128(const u128& rhs);
	u128(const s128& rhs);
	
	u128& operator=(const u128& rhs);
	
	u128 operator+(u128 rhs);
	u128 operator-(u128 rhs);
	
	u128 operator~();
	u128 operator&(u128 rhs);
	u128 operator|(u128 rhs);
	u128 operator^(u128 rhs);
	
	// Logical shifts.
	u128 operator<<(u64 bits);
	u128 operator>>(u64 bits);
	
	friend bool operator==(const u128& lhs, const u128& rhs) = default;
	friend bool operator!=(const u128& lhs, const u128& rhs) = default;
	
	std::string to_string();
	static std::optional<u128> from_string(const std::string&& hex);
};

struct s128 {
	u64 low;
	u64 high;
	
	s128();
	s128(s64 value);
	s128(u64 h, u64 l);
	s128(const u128& rhs);
	s128(const s128& rhs);
	
	s128& operator=(const s128& rhs);
	
	s128 operator+(s128 rhs);
	s128 operator-(s128 rhs);
	
	s128 operator~();
	s128 operator&(s128 rhs);
	s128 operator|(s128 rhs);
	s128 operator^(s128 rhs);
	
	// Arithmetic shifts.
	s128 operator<<(u64 bits);
	s128 operator>>(u64 bits);
	
	friend bool operator==(const s128& lhs, const s128& rhs) = default;
	friend bool operator!=(const s128& lhs, const s128& rhs) = default;
	
	std::string to_string();
	static std::optional<s128> from_string(const std::string&& hex);
};

}
