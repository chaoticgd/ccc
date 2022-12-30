#include "ccc.h"

#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdint>
#include <string>
#include <cstring>

namespace ccc {

std::vector<u8> read_file_bin(fs::path const& filepath) {
	std::ifstream ifs(filepath, std::ios::binary | std::ios::ate);

	if (!ifs)
		throw std::runtime_error(filepath.string() + ": " + std::strerror(errno));

	const auto end = ifs.tellg();
	ifs.seekg(0, std::ios::beg);

	const auto size = std::size_t(end - ifs.tellg());

	if (size == 0)  // avoid undefined behavior
		return {};

	const std::vector<u8> buf(size);

	if (!ifs.read((char*)buf.data(), buf.size()))
		throw std::runtime_error(filepath.string() + ": " + std::strerror(errno));

	return buf;
}

std::string read_text_file(const fs::path& path) {
	std::ifstream file_stream;
	file_stream.open(path.string());
	verify(file_stream.is_open(), "Failed to open file.");
	std::stringstream string_stream;
	string_stream << file_stream.rdbuf();
	return string_stream.str();
}

std::string get_string(const std::vector<u8>& bytes, u64 offset) {
	verify(offset < bytes.size(), "Tried to read a string past the end of the buffer.");
	std::string result;
	for(u64 i = offset; i < bytes.size() && bytes[i] != '\0'; i++) {
		result += bytes[i];
	}
	return result;
}

const char* get_c_string(const std::vector<u8>& bytes, u64 offset) {
	verify(offset < bytes.size(), "Tried to read a string past the end of the buffer.");
	for(const unsigned char* c = bytes.data() + offset; c < bytes.data() + bytes.size(); c++) {
		if(*c == '\0') {
			return (const char*) &bytes[offset];
		}
	}
	verify_not_reached("Unexpected end of buffer while reading string.");
}

std::string string_format(const char* format, va_list args) {
	static char buffer[16 * 1024];
	vsnprintf(buffer, 16 * 1024, format, args);
	return std::string(buffer);
}

std::string stringf(const char* format, ...) {
	va_list args;
	va_start(args, format);
	std::string string = string_format(format, args);
	va_end(args);
	return string;
}

}
