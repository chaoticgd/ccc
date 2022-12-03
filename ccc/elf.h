#ifndef _CCC_ELF_H
#define _CCC_ELF_H

#include "util.h"
#include "module.h"

namespace ccc::loaders {

Module read_elf_file(fs::path path);

}

#endif
