#include "ccc.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdint>

buffer read_file_bin(fs::path const& filepath) {
	std::ifstream ifs(filepath, std::ios::binary | std::ios::ate);

	if (!ifs)
		throw std::runtime_error(filepath.string() + ": " + std::strerror(errno));

	const auto end = ifs.tellg();
	ifs.seekg(0, std::ios::beg);

	const auto size = std::size_t(end - ifs.tellg());

	if (size == 0)  // avoid undefined behavior
		return {};

	const buffer buf(size);

	if (!ifs.read((char*)buf.data(), buf.size()))
		throw std::runtime_error(filepath.string() + ": " + std::strerror(errno));

	return buf;
}

std::string read_string(const buffer& bytes, u64 offset) {
	if(offset > bytes.size()) {
		return "(unexpected eof)";
	}
	std::string result;
	for(u64 i = offset; i < bytes.size(); i++) {
		if(bytes[i] == 0) {
			break;
		} else {
			result += bytes[i];
		}
	}
	return result;
}
