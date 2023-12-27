// This file is part of the Chaos Compiler Collection.
//
// SPDX-License-Identifier: MIT

#pragma once

#include "../ccc/util.h"
#include "../ccc/registers.h"
#include "opcodes.h"

namespace ccc::mips {

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

enum InsnFormat {
	INSN_FORMAT_IMM,
	INSN_FORMAT_JMP,
	INSN_FORMAT_REG,
	INSN_FORMAT_COP2_0,
	INSN_FORMAT_COP2_1,
	INSN_FORMAT_COP2_2,
	INSN_FORMAT_COP2_3,
	INSN_FORMAT_COP2_4,
	INSN_FORMAT_COP2_5,
	INSN_FORMAT_COP2_6,
	INSN_FORMAT_COP2_7,
	INSN_FORMAT_COP2_8,
	INSN_FORMAT_COP2_9,
	INSN_FORMAT_COP2_10,
	INSN_FORMAT_COP2_11,
	INSN_FORMAT_COP2_13,
	INSN_FORMAT_BAD
};

enum class InsnField {
	NONE,
	RS,
	RT,
	IMMED,
	TARGET,
	RD,
	SA,
	FUNC
};

enum class FlowDirection {
	NONE = 0,
	IN,
	OUT,
	INOUT
};

enum class FlowType {
	IMMED,
	REG,
	FIXED_REG
};

struct FlowInfo {
	FlowInfo()
		: direction(FlowDirection::NONE) {}
	FlowInfo(FlowDirection d, FlowType t, InsnField f, RegisterClass c, s32 i)
		: direction(d), type(t), field(f), reg_class(c), reg_index(i) {}
	FlowInfo(FlowDirection d, FlowType t, InsnField f)
		: FlowInfo(d, t, f, RegisterClass::INVALID, -1) {}
	// REG
	FlowInfo(FlowDirection d, RegisterClass c, InsnField f)
		: FlowInfo(d, FlowType::REG, f, c, -1) {}
	// FIXED_REG
	FlowInfo(FlowDirection d, GPR gpr)
		: FlowInfo(d, FlowType::FIXED_REG, InsnField::NONE, RegisterClass::GPR, (s32) gpr) {}
	FlowInfo(FlowDirection d, SpecialGPR sgpr)
		: FlowInfo(d, FlowType::FIXED_REG, InsnField::NONE, RegisterClass::SPECIAL_GPR, (s32) sgpr) {}
	
	FlowDirection direction;
	FlowType type;
	InsnField field;
	RegisterClass reg_class;
	s32 reg_index;
	
	bool is_past_end() const { return direction == FlowDirection::NONE; }
};

enum class InsnType {
	INVALD,
	BRANCH, // branches, jumps
	CALL, // calls, syscalls
	ARTMTC, // moves, integer arithmetic, floating point maths
	LOADFM, // memory loads
	STOREM, // memory stores
	SYSTEM  // cache, pref
};

struct InsnInfo {
	const char* mnemonic;
	InsnType type;
	FlowInfo data_flows[10];
};

struct Insn {
	Insn();
	Insn(u32 val);
	
	InsnClass iclass() const;
	const InsnInfo& info() const;
	
	u32 opcode() const;
	u32 rs() const;
	u32 rt() const;
	u32 rd() const;
	u32 sa() const;
	u32 func() const;
	u32 immed() const;
	u32 target_insns() const;
	u32 target_bytes() const;
	
	u32 field(InsnField field) const;
	
	static Insn create_i_type(u32 op, u32 rs, u32 rt, u32 immediate);
	static Insn create_j_type(u32 op, u32 target);
	static Insn create_r_type(u32 op, u32 rs, u32 rt, u32 rd, u32 sa, u32 function);
	
	u32 value;
};

}
