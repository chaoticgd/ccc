// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>
#include <optional>
#include <filesystem>

#include "../ccc/util.h"

namespace fs = std::filesystem;

namespace platform {

ccc::Result<std::vector<ccc::u8>> read_binary_file(const fs::path& path);
std::optional<std::string> read_text_file(const fs::path& path);

}
