// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>
#include <stdio.h>
#include <optional>
#include <filesystem>

#include "../ccc/util.h"

namespace fs = std::filesystem;

namespace platform {

ccc::Result<std::vector<ccc::u8>> read_binary_file(const fs::path& path);
std::optional<std::string> read_text_file(const fs::path& path);
ccc::s64 file_size(FILE* file);

// On Windows long is only 4 bytes, so the regular libc standard IO functions
// are crippled, hence we need to use these special versions instead.
#ifdef _MSC_VER
	#define fseek _fseeki64
	#define ftell _ftelli64
#endif

}
