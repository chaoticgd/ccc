#ifndef _CCC_ELF_H
#define _CCC_ELF_H

#include "util.h"
#include "module.h"

namespace ccc {

Result<void> parse_elf_file(Module& mod);

}

#endif
