#pragma once

#include <vector>
#include <stdio.h>
#include <optional>
#include <filesystem>

namespace fs = std::filesystem;

namespace platform {

std::optional<std::vector<unsigned char>> read_binary_file(const fs::path& path);
std::optional<std::string> read_text_file(const fs::path& path);
int64_t file_size(FILE* file);

// On Windows long is only 4 bytes, so the regular libc standard IO functions
// are crippled, hence we need to use these special versions instead.
#ifdef _MSC_VER
	#define fseek _fseeki64
	#define ftell _ftelli64
#endif

}
