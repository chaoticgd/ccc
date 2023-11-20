#pragma once

#include "insn.h"

namespace ccc::mips {

extern const InsnInfo* INSN_TABLES[MAX_INSN_CLASS];

extern const InsnInfo MIPS_OPCODE_TABLE[MAX_OPCODE];
extern const InsnInfo MIPS_SPECIAL_TABLE[MAX_SPECIAL];
extern const InsnInfo MIPS_REGIMM_TABLE[MAX_REGIMM];
extern const InsnInfo MMI_TABLE[MAX_MMI];
extern const InsnInfo MMI0_TABLE[MAX_MMI0];
extern const InsnInfo MMI1_TABLE[MAX_MMI1];
extern const InsnInfo MMI2_TABLE[MAX_MMI2];
extern const InsnInfo MMI3_TABLE[MAX_MMI3];
extern const InsnInfo COP0_TABLE[MAX_COP0];
extern const InsnInfo COP0_BC0_TABLE[MAX_BC0];
extern const InsnInfo COP0_C0_TABLE[MAX_C0];
extern const InsnInfo COP1_TABLE[MAX_COP1];
extern const InsnInfo COP1_BC1_TABLE[MAX_BC1];
extern const InsnInfo COP1_S_TABLE[MAX_S];
extern const InsnInfo COP1_W_TABLE[MAX_W];

}
