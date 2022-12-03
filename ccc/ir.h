#ifndef _CCC_IR_H
#define _CCC_IR_H

#include "insn.h"
#include "module.h"

namespace ccc::ir {

struct Function {
	std::string name;
	u32 address;
};

std::map<u32, Function> scan_for_functions(u32 address, std::span<mips::Insn> insns);

};

#endif
