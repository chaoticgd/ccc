#include "ccc.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdint>

std::vector<uint8_t> read_file_bin(const std::filesystem::path& path) {
	std::basic_ifstream<uint8_t> file{ path, std::ios::binary };
	return { std::istreambuf_iterator<uint8_t>{file}, {} };
}

std::string read_string(const std::vector<u8>& bytes, u64 offset) {
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
