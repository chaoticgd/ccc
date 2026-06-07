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

	file.seekg(0, std::ios::end);
	s64 size = file.tellg();
	CCC_CHECK(size >= 0, "Failed to determine size of file '%s'.", path.string().c_str());

	std::vector<u8> output(size);
	file.seekg(0, std::ios::beg);
	file.read((char*) output.data(), size);
	CCC_CHECK(file.good(), "Failed to read from file '%s' (%s).", path.string().c_str(), strerror(errno));

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
