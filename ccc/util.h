#ifndef _CCC_UTIL_H
#define _CCC_UTIL_H

#include <map>
#include <set>
#include <span>
#include <vector>
#include <cstdio>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <variant>
#include <assert.h>
#include <iostream>
#include <stdarg.h>
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
template <typename... Args>
void verify_impl(const char* file, int line, bool condition, const char* format, Args... args) {
	if(!condition) {
		fprintf(stderr, "[%s:%d] \033[31merror:\033[0m ", file, line);
		fprintf(stderr, format, args...);
	fprintf(stderr, "\n");
		exit(1);
	}
}
#define verify(condition, ...) \
	ccc::verify_impl(__FILE__, __LINE__, condition, __VA_ARGS__)
template <typename... Args>
[[noreturn]] void verify_not_reached_impl(const char* file, int line, const char* format, Args... args) {
	fprintf(stderr, "[%s:%d] \033[31merror:\033[0m ", file, line);
	fprintf(stderr, format, args...);
	fprintf(stderr, "\n");
	exit(1);
}
#define verify_not_reached(...) \
	ccc::verify_not_reached_impl(__FILE__, __LINE__, __VA_ARGS__)
template <typename... Args>
void warn_impl(const char* file, int line, const char* format, Args... args) {
	fprintf(stderr, "[%s:%d] \033[35mwarning:\033[0m ", file, line);
	fprintf(stderr, format, args...);
	fprintf(stderr, "\n");
}
#define warn(...) \
	ccc::warn_impl(__FILE__, __LINE__, __VA_ARGS__)
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

std::vector<u8> read_file_bin(fs::path const& filepath);
std::string read_text_file(const fs::path& path);
std::string get_string(const std::vector<u8>& bytes, u64 offset);
const char* get_c_string(const std::vector<u8>& bytes, u64 offset);

struct Range {
	s32 low;
	s32 high;
};

#define BEGIN_END(x) (x).begin(), (x).end()
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

std::string string_format(const char* format, va_list args);
std::string stringf(const char* format, ...);

// These functions are to be used only for source file paths present in the
// symbol table, since we want them to be handled consistently across different
// platforms, which with std::filesystem::path doesn't seem to be possible.
std::pair<std::string, bool> merge_paths(const std::string& base, const std::string& path);
std::string normalise_path(const char* input, bool use_backslashes_as_path_separators);
bool guess_is_windows_path(const char* path);

struct StringPointer {
	const char* ptr = nullptr;
	
	StringPointer& operator=(const char* rhs) { ptr = rhs; return *this; }
	bool operator==(const std::string& rhs) const { return strcmp(ptr, rhs.c_str()); }
	bool empty() const { return !ptr || strlen(ptr) == 0; }
	const char* c_str() const { return ptr; }
	bool starts_with(const char* pattern) const { return std::string(ptr).starts_with(pattern); }
	std::string substr(size_t x) { return std::string(ptr).substr(x); }
	std::string substr(size_t x, size_t y) { return std::string(ptr).substr(x, y); }
	std::string::size_type find(const char* pattern) { return std::string(ptr).find(pattern); }
};

}

#endif
