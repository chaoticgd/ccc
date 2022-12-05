#include "ir.h"

namespace ccc::ir {

std::map<u32, Function> scan_for_functions(u32 address, std::span<mips::Insn> insns) {
	std::map<u32, Function> functions;
	for(mips::Insn& insn : insns) {
		if(insn.opcode() == OPCODE_JAL) {
			u32 address = insn.target_bytes();
			Function& func = functions[address];
			func.name = "func_" + stringf("%08x", address);
			func.address = address;
		}
	}
	return functions;
}

}
