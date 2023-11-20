// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "file.h"

#include <fstream>
#include <sstream>

using namespace ccc;

namespace platform {

ccc::Result<std::vector<ccc::u8>> read_binary_file(const fs::path& path) {
	FILE* file = fopen(path.string().c_str(), "rb");
	CCC_CHECK(file != nullptr, "Failed to open file '%s' (%s).", path.string().c_str(), strerror(errno));
	s64 size = file_size(file);
	std::vector<u8> output(size);
	if(size > 0) {
		size_t read_result = fread(output.data(), size, 1, file);
		fclose(file);
		CCC_CHECK(read_result == 1, "Failed to read from file '%s' (%s).", path.string().c_str(), strerror(errno));
	}
	return output;
}

std::optional<std::string> read_text_file(const fs::path& path) {
	std::ifstream file_stream;
	file_stream.open(path);
	if(!file_stream.is_open()) {
		return std::nullopt;
	}
	std::stringstream string_stream;
	string_stream << file_stream.rdbuf();
	return string_stream.str();
}

s64 file_size(FILE* file) {
	s64 pos = ftell(file);
	fseek(file, 0, SEEK_END);
	s64 size = ftell(file);
	fseek(file, pos, SEEK_SET);
	return size;
}

}
