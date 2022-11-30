#ifndef _CCC_INSN_H
#define _CCC_INSN_H

#include "util.h"
#include "opcodes.h"

namespace ccc {

enum InsnClass {
	INSN_CLASS_MIPS = 0,
	INSN_CLASS_MIPS_SPECIAL = 1,
	INSN_CLASS_MIPS_REGIMM = 2,
	INSN_CLASS_MMI = 3,
	INSN_CLASS_MMI0 = 4,
	INSN_CLASS_MMI1 = 5,
	INSN_CLASS_MMI2 = 6,
	INSN_CLASS_MMI3 = 7,
	INSN_CLASS_COP0 = 8,
	INSN_CLASS_COP0_BC0 = 9,
	INSN_CLASS_COP0_C0 = 10,
	INSN_CLASS_COP1 = 11,
	INSN_CLASS_COP1_BC1 = 12,
	INSN_CLASS_COP1_S = 13,
	INSN_CLASS_COP1_W = 14,
	INSN_CLASS_COP2 = 15,
	MAX_INSN_CLASS = 16
};

enum InsnType {
	INSN_TYPE_IMM,
	INSN_TYPE_JMP,
	INSN_TYPE_REG,
	INSN_TYPE_COP2_0,
	INSN_TYPE_COP2_1,
	INSN_TYPE_COP2_2,
	INSN_TYPE_COP2_3,
	INSN_TYPE_COP2_4,
	INSN_TYPE_COP2_5,
	INSN_TYPE_COP2_6,
	INSN_TYPE_COP2_7,
	INSN_TYPE_COP2_8,
	INSN_TYPE_COP2_9,
	INSN_TYPE_COP2_10,
	INSN_TYPE_COP2_11,
	INSN_TYPE_COP2_13,
	INSN_TYPE_BAD
};

struct InsnInfo {
	const char* mnemonic;
};

struct Insn {
	Insn(u32 val);
	
	InsnClass iclass() const;
	const InsnInfo& info() const;
	u32 target_bytes() const;
	
	u32 opcode() const;
	u32 rs() const;
	u32 rt() const;
	u32 rd() const;
	u32 sa() const;
	u32 function() const;
	u32 immediate() const;
	u32 target_insns() const;
	
	static Insn create_i_type(u32 op, u32 rs, u32 rt, u32 immediate);
	static Insn create_j_type(u32 op, u32 target);
	static Insn create_r_type(u32 op, u32 rs, u32 rt, u32 rd, u32 sa, u32 function);
	
	u32 value;
};

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

#endif
