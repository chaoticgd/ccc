// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "file.h"

#include <fstream>
#include <sstream>

using namespace ccc;

namespace platform {

Result<std::vector<ccc::u8>> read_binary_file(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	CCC_CHECK(file, "Failed to open file '%s' (%s).", path.string().c_str(), strerror(errno));
	CCC_CHECK(fs::is_regular_file(path), "Failed to open '%s' (not a regular file).", path.string().c_str());
	file.seekg(0, std::ios::end);
	s64 size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::vector<u8> output(size);
	file.read((char*) output.data(), size);
	CCC_CHECK(file, "Failed to read from file '%s' (%s).", path.string().c_str(), strerror(errno));
	return output;
}

std::optional<std::string> read_text_file(const fs::path& path)
{
	std::ifstream file_stream;
	file_stream.open(path);
	if (!file_stream.is_open()) {
		return std::nullopt;
	}
	std::stringstream string_stream;
	string_stream << file_stream.rdbuf();
	return string_stream.str();
}

}
