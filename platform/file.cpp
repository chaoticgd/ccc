#include "file.h"

#include <fstream>
#include <sstream>

namespace platform {

std::optional<std::vector<unsigned char>> read_binary_file(const fs::path& path) {
	FILE* file = fopen(path.c_str(), "rb");
	if(file == nullptr) {
		return std::nullopt;
	}
	int64_t size = file_size(file);
	std::vector<unsigned char> output(size);
	if(fread(output.data(), size, 1, file) != 1) {
		return std::nullopt;
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

int64_t file_size(FILE* file) {
	int64_t pos = ftell(file);
	fseek(file, 0, SEEK_END);
	int64_t size = ftell(file);
	fseek(file, pos, SEEK_SET);
	return size;
}

}
