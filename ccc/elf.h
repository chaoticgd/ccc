#pragma once

#include "util.h"
#include "module.h"

namespace ccc {

Result<void> parse_elf_file(Module& mod);

}
