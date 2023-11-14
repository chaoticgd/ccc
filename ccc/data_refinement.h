#pragma once

#include "analysis.h"
#include "elf.h"

namespace ccc {

void refine_variables(HighSymbolTable& high, const std::vector<ElfFile*>& elves);

}
