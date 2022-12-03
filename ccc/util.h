#ifndef _CCC_UTIL_H
#define _CCC_UTIL_H

#include <map>
#include <set>
#include <vector>
#include <cstdio>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <assert.h>
#include <iostream>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <inttypes.h>

namespace ccc {

namespace fs = std::filesystem;

using u8 = unsigned char;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8 = signed char;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using buffer = std::vector<u8>;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
template <typename... Args>
void verify_impl(const char* file, int line, bool condition, const char* error_message, Args... args) {
	if(!condition) {
		fprintf(stderr, "[%s:%d] \033[31merror:\033[0m ", file, line);
		fprintf(stderr, error_message, args...);
	fprintf(stderr, "\n");
		exit(1);
	}
}
#define verify(condition, ...) \
	ccc::verify_impl(__FILE__, __LINE__, condition, __VA_ARGS__)
template <typename... Args>
[[noreturn]] void verify_not_reached_impl(const char* file, int line, const char* error_message, Args... args) {
	fprintf(stderr, "[%s:%d] \033[31merror:\033[0m ", file, line);
	fprintf(stderr, error_message, args...);
	fprintf(stderr, "\n");
	exit(1);
}
#define verify_not_reached(...) \
	ccc::verify_not_reached_impl(__FILE__, __LINE__, __VA_ARGS__)
#pragma GCC diagnostic pop

#ifdef _MSC_VER
	#define packed_struct(name, ...) \
		__pragma(pack(push, 1)) struct name { __VA_ARGS__ } __pragma(pack(pop));
#else
	#define packed_struct(name, ...) \
		struct __attribute__((__packed__)) name { __VA_ARGS__ };
#endif

template <typename T>
const T& get_packed(const std::vector<u8>& bytes, u64 offset, const char* subject) {
	verify(bytes.size() >= offset + sizeof(T), "Failed to read %s.", subject);
	return *(const T*) &bytes[offset];
}

buffer read_file_bin(fs::path const& filepath);
std::string read_string(const buffer& bytes, u64 offset);

struct Range {
	s32 low;
	s32 high;
};

#define BEGIN_END(x) (x).begin(), (x).end()
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

}

#endif
