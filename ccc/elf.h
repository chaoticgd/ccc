#ifndef _CCC_ELF_H
#define _CCC_ELF_H

#include "util.h"
#include "coredata.h"

namespace ccc {

ProgramImage read_program_image(fs::path path);
void parse_elf_file(Program& program, u64 image_index);

}

#endif
