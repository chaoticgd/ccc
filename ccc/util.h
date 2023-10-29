#ifndef _CCC_UTIL_H
#define _CCC_UTIL_H

#include <map>
#include <set>
#include <span>
#include <cstdio>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <inttypes.h>

namespace ccc {

using u8 = unsigned char;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8 = signed char;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

#define CCC_ANSI_COLOUR_OFF "\033[0m"
#define CCC_ANSI_COLOUR_RED "\033[31m"
#define CCC_ANSI_COLOUR_MAGENTA "\033[35m"
#define CCC_ANSI_COLOUR_GRAY "\033[90m"

struct Error {
	const char* message;
	const char* source_file;
	s32 source_line;
};

Error* format_error(const char* source_file, int source_line, const char* format, ...);
void print_error(FILE* out, const Error* error);
void print_warning(FILE* out, const Error* warning);

#define CCC_FATAL(...) \
	{ \
		ccc::Error* error = ccc::format_error(__FILE__, __LINE__, __VA_ARGS__); \
		ccc::print_error(stderr, error); \
		exit(1); \
	}
	
#define CCC_CHECK_FATAL(condition, ...) \
	if(!(condition)) { \
		ccc::Error* error = ccc::format_error(__FILE__, __LINE__, __VA_ARGS__); \
		ccc::print_error(stderr, error); \
		exit(1); \
	}

#define CCC_ASSERT(condition) \
	CCC_CHECK_FATAL(condition, #condition)

// The main error handling construct in CCC. This class is used to bundle
// together a return value and a pointer to error information, so that errors
// can be propagated up the stack.
template <typename Value>
class Result {
	template <typename OtherValue>
	friend class Result;
protected:
	Value m_value;
	Error* m_error;
	
	Result() {}
	
public:
	Result(Value value) : m_value(std::move(value)), m_error(nullptr) {}
	
	// Used to propagate errors up the call stack.
	template <typename OtherValue>
	Result(const Result<OtherValue>& rhs) {
		CCC_ASSERT(rhs.m_error != nullptr);
		m_error = rhs.m_error;
	}
	
	static Result<Value> failure(Error* error) {
		Result<Value> result;
		result.m_error = error;
		return result;
	}
	
	bool success() const {
		return m_error == nullptr;
	}
	
	const Error& error() const {
		CCC_ASSERT(m_error != nullptr);
		return *m_error;
	}
	
	Value& operator*() {
		CCC_ASSERT(m_error == nullptr);
		return m_value;
	}
	
	const Value& operator*() const {
		CCC_ASSERT(m_error == nullptr);
		return m_value;
	}
	
	Value* operator->() {
		CCC_ASSERT(m_error == nullptr);
		return &m_value;
	}
	
	const Value* operator->() const {
		CCC_ASSERT(m_error == nullptr);
		return &m_value;
	}
};

template <>
class Result<void> : public Result<int> {
public:
	Result() : Result<int>(0) {}
	
	template <typename OtherValue>
	Result(const Result<OtherValue>& rhs) {
		CCC_ASSERT(rhs.m_error != nullptr);
		m_error = rhs.m_error;
	}
};

#define CCC_FAILURE(...) ccc::Result<int>::failure(ccc::format_error(__FILE__, __LINE__, __VA_ARGS__))
#define CCC_RETURN_IF_ERROR(result) if(!(result).success()) return (result);
#define CCC_EXIT_IF_ERROR(result) \
	if(!(result).success()) { \
		ccc::print_error(stderr, &(result).error()); \
		exit(1); \
	}

#define CCC_CHECK(condition, ...) \
	if(!(condition)) { \
		return CCC_FAILURE(__VA_ARGS__); \
	}

#define CCC_EXPECT_CHAR(input, c, context) \
	CCC_CHECK(*(input++) == c, \
		"Expected '%c' in %s, got '%c' (%02hhx)", \
		c, context, *(input - 1), *(input - 1))

template <typename... Args>
void warn_impl(const char* source_file, int source_line, const char* format, Args... args) {
	Error* warning = format_error(source_file, source_line, format, args...);
	print_warning(stderr, warning);
}
#define CCC_WARN(...) \
	ccc::warn_impl(__FILE__, __LINE__, __VA_ARGS__)

#ifdef _MSC_VER
	#define CCC_PACKED_STRUCT(name, ...) \
		__pragma(pack(push, 1)) struct name { __VA_ARGS__ } __pragma(pack(pop));
#else
	#define CCC_PACKED_STRUCT(name, ...) \
		struct __attribute__((__packed__)) name { __VA_ARGS__ };
#endif

template <typename T>
const T* get_packed(const std::vector<u8>& bytes, u64 offset) {
	if(bytes.size() < offset + sizeof(T)) {
		return nullptr;
	}
	return reinterpret_cast<const T*>(&bytes[offset]);
}

Result<const char*> get_string(const std::vector<u8>& bytes, u64 offset);

#define CCC_BEGIN_END(x) (x).begin(), (x).end()
#define CCC_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// These functions are to be used only for source file paths present in the
// symbol table, since we want them to be handled consistently across different
// platforms, which with std::filesystem::path doesn't seem to be possible.
std::string merge_paths(const std::string& base, const std::string& path);
std::string normalise_path(const char* input, bool use_backslashes_as_path_separators);
bool guess_is_windows_path(const char* path);
std::string extract_file_name(const std::string& path);

}

#endif
