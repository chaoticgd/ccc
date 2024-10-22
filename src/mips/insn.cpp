// This file is part of the Chaos Compiler Collection.
//
// SPDX-License-Identifier: MIT

#include "insn.h"

#include "tables.h"

#define OPCODE_MASK    0b11111100000000000000000000000000
#define RS_MASK        0b00000011111000000000000000000000
#define RT_MASK        0b00000000000111110000000000000000
#define RD_MASK        0b00000000000000001111100000000000
#define SA_MASK        0b00000000000000000000011111000000
#define FUNCTION_MASK  0b00000000000000000000000000111111
#define IMMEDIATE_MASK 0b00000000000000001111111111111111
#define TARGET_MASK    0b00000011111111111111111111111111

namespace ccc::mips {

Insn::Insn() {}

Insn::Insn(u32 val) : value(val) {}

InsnClass Insn::iclass() const
{
	if (opcode() == OPCODE_SPECIAL) {
		return INSN_CLASS_MIPS_SPECIAL;
	} else if (opcode() == OPCODE_COP0) {
		if (rs() == COP0_BC0) {
			return INSN_CLASS_COP0_BC0;
		} else if (rs() == COP0_C0) {
			return INSN_CLASS_COP0_C0;
		} else {
			return INSN_CLASS_COP0;
		}
	} else if (opcode() == OPCODE_COP1) {
		if (rs() == COP1_BC1) {
			return INSN_CLASS_COP1_BC1;
		} else if (rs() == COP1_S) {
			return INSN_CLASS_COP1_S;
		} else if (rs() == COP1_W) {
			return INSN_CLASS_COP1_W;
		} else {
			return INSN_CLASS_COP1;
		}
	} else if (opcode() == OPCODE_COP2) {
		return INSN_CLASS_COP2;
	} else if (opcode() == OPCODE_MMI) {
		if (func() == MMI_MMI0) {
			return INSN_CLASS_MMI0;
		} else if (func() == MMI_MMI1) {
			return INSN_CLASS_MMI1;
		} else if (func() == MMI_MMI2) {
			return INSN_CLASS_MMI2;
		} else if (func() == MMI_MMI3) {
			return INSN_CLASS_MMI3;
		} else {
			return INSN_CLASS_MMI;
		}
	} else {
		return INSN_CLASS_MIPS;
	}
}

const InsnInfo& Insn::info() const
{
	switch (iclass()) {
		case INSN_CLASS_MIPS: return MIPS_OPCODE_TABLE[opcode()];
		case INSN_CLASS_MIPS_SPECIAL: return MIPS_SPECIAL_TABLE[func()];
		case INSN_CLASS_MIPS_REGIMM: return MIPS_REGIMM_TABLE[rt()];
		case INSN_CLASS_MMI: return MMI_TABLE[func()];
		case INSN_CLASS_MMI0: return MMI0_TABLE[sa()];
		case INSN_CLASS_MMI1: return MMI1_TABLE[sa()];
		case INSN_CLASS_MMI2: return MMI2_TABLE[sa()];
		case INSN_CLASS_MMI3: return MMI3_TABLE[sa()];
		case INSN_CLASS_COP0: return COP0_TABLE[rs()];
		case INSN_CLASS_COP0_BC0: return COP0_BC0_TABLE[rt()];
		case INSN_CLASS_COP0_C0: return COP0_C0_TABLE[func()];
		case INSN_CLASS_COP1: return COP1_TABLE[rs()];
		case INSN_CLASS_COP1_BC1: return COP1_BC1_TABLE[rt()];
		case INSN_CLASS_COP1_S: return COP1_S_TABLE[func()];
		case INSN_CLASS_COP1_W: return COP1_W_TABLE[func()];
		case INSN_CLASS_COP2: return MIPS_OPCODE_TABLE[OPCODE_COP2];
		default: CCC_EXIT("Invalid instruction %08x.", value);
	}
}

u32 Insn::opcode() const
{
	return (value & OPCODE_MASK) >> 26;
}

u32 Insn::rs() const
{
	return (value & RS_MASK) >> 21;
}

u32 Insn::rt() const
{
	return (value & RT_MASK) >> 16;
}

u32 Insn::rd() const
{
	return (value & RD_MASK) >> 11;
}

u32 Insn::sa() const
{
	return (value & SA_MASK) >> 6;
}

u32 Insn::func() const
{
	return (value & FUNCTION_MASK) >> 0;
}

u32 Insn::immed() const
{
	return (value & IMMEDIATE_MASK) >> 0;
}

u32 Insn::target_insns() const
{
	return (value & TARGET_MASK) >> 0;
}

u32 Insn::target_bytes() const
{
	return target_insns() * 4;
}

u32 Insn::field(InsnField field) const
{
	switch (field) {
		case InsnField::NONE: return 0;
		case InsnField::RS: return rs();
		case InsnField::RT: return rt();
		case InsnField::IMMED: return immed();
		case InsnField::TARGET: return target_bytes();
		case InsnField::RD: return rd();
		case InsnField::SA: return sa();
		case InsnField::FUNC: return func();
	}
	return 0;
}

}
