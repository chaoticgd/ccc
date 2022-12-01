#ifndef _CCC_REGISTERS_H
#define _CCC_REGISTERS_H

#include "util.h"

namespace ccc::mips {

enum class RegisterClass {
	INVALID,
	GPR,
	SPECIAL_GPR,
	SCP,
	FPU,
	SPECIAL_FPU,
	VU0
};

enum class GPR {
	INVALID = -1,
	ZERO = 0,
	AT = 1,
	V0 = 2,
	V1 = 3,
	A0 = 4,
	A1 = 5,
	A2 = 6,
	A3 = 7,
	T0 = 8,
	T1 = 9,
	T2 = 10,
	T3 = 11,
	T4 = 12,
	T5 = 13,
	T6 = 14,
	T7 = 15,
	S0 = 16,
	S1 = 17,
	S2 = 18,
	S3 = 19,
	S4 = 20,
	S5 = 21,
	S6 = 22,
	S7 = 23,
	T8 = 24,
	T9 = 25,
	K0 = 26,
	K1 = 27,
	GP = 28,
	SP = 29,
	FP = 30,
	RA = 31
};

enum SpecialGPR {
	PC,
	HI,
	LO,
	HI1,
	LO1,
	SA
};

enum ScpRegister {
	SCP_INDEX = 0,
	SCP_RANDOM = 1,
	SCP_ENTRYLO0 = 2,
	SCP_ENTRYLO1 = 3,
	SCP_CONTEXT = 4,
	SCP_PAGEMASK =5,
	SCP_WIRED = 6,
	SCP_RESERVED7 = 7,
	SCP_BADVADDR = 8,
	SCP_COUNT = 9,
	SCP_ENTRYHI = 10,
	SCP_COMPARE = 11,
	SCP_STATUS = 12,
	SCP_CAUSE = 13,
	SCP_EPC = 14,
	SCP_PRID = 15,
	SCP_CONFIG = 16,
	SCP_RESERVED17 = 17,
	SCP_BADPADDR = 23,
	SCP_DEBUG = 24,
	SCP_PERF = 25,
	SCP_RESERVED26 = 26,
	SCP_RESERVED27 = 27,
	SCP_TAGLO = 28,
	SCP_TAGHI = 29,
	SCP_ERROREPC = 30,
	SCP_RESERVED31 = 31
};

enum SpecialFpuRegister {
	FCR0,
	FCR31,
	ACC
};

}

#endif
