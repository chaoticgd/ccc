#ifndef _CCC_REGISTERS_H
#define _CCC_REGISTERS_H

#include "util.h"

namespace ccc::mips {

enum class RegisterClass {
	INVALID = -1,
	GPR = 0,
	SPECIAL_GPR = 1,
	SCP = 2,
	FPU = 3,
	SPECIAL_FPU = 4,
	VU0 = 5
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

enum class SpecialGPR {
	PC = 0,
	HI = 1,
	LO = 2,
	HI1 = 3,
	LO1 = 4,
	SA = 5
};

enum class ScpRegister {
	INDEX = 0,
	RANDOM = 1,
	ENTRYLO0 = 2,
	ENTRYLO1 = 3,
	CONTEXT = 4,
	PAGEMASK = 5,
	WIRED = 6,
	RESERVED7 = 7,
	BADVADDR = 8,
	COUNT = 9,
	ENTRYHI = 10,
	COMPARE = 11,
	STATUS = 12,
	CAUSE = 13,
	EPC = 14,
	PRID = 15,
	CONFIG = 16,
	RESERVED17 = 17,
	BADPADDR = 23,
	DEBUG = 24,
	PERF = 25,
	RESERVED26 = 26,
	RESERVED27 = 27,
	TAGLO = 28,
	TAGHI = 29,
	ERROREPC = 30,
	RESERVED31 = 31
};

enum class FpuRegister {
	FPR0 = 0,
	FPR1 = 1,
	FPR2 = 2,
	FPR3 = 3,
	FPR4 = 4,
	FPR5 = 5,
	FPR6 = 6,
	FPR7 = 7,
	FPR8 = 8,
	FPR9 = 9,
	FPR10 = 10,
	FPR11 = 11,
	FPR12 = 12,
	FPR13 = 13,
	FPR14 = 14,
	FPR15 = 15,
	FPR16 = 16,
	FPR17 = 17,
	FPR18 = 18,
	FPR19 = 19,
	FPR20 = 20,
	FPR21 = 21,
	FPR22 = 22,
	FPR23 = 23,
	FPR24 = 24,
	FPR25 = 25,
	FPR26 = 26,
	FPR27 = 27,
	FPR28 = 28,
	FPR29 = 29,
	FPR30 = 30,
	FPR31 = 31
};

enum SpecialFpuRegister {
	FCR0 = 0,
	FCR31 = 1,
	ACC = 2
};

enum class Vu0Register {
	VF0 = 0,
	VF1 = 1,
	VF2 = 2,
	VF3 = 3,
	VF4 = 4,
	VF5 = 5,
	VF6 = 6,
	VF7 = 7,
	VF8 = 8,
	VF9 = 9,
	VF10 = 10,
	VF11 = 11,
	VF12 = 12,
	VF13 = 13,
	VF14 = 14,
	VF15 = 15,
	VF16 = 16,
	VF17 = 17,
	VF18 = 18,
	VF19 = 19,
	VF20 = 20,
	VF21 = 21,
	VF22 = 22,
	VF23 = 23,
	VF24 = 24,
	VF25 = 25,
	VF26 = 26,
	VF27 = 27,
	VF28 = 28,
	VF29 = 29,
	VF30 = 30,
	VF31 = 31
};

extern const char** REGISTER_STRING_TABLES[6];
extern const u64 REGISTER_STRING_TABLE_SIZES[6];

extern const char* GPR_STRINGS[32];
extern const char* SPECIAL_GPR_STRINGS[6];
extern const char* SCP_STRINGS[32];
extern const char* FPU_STRINGS[32];
extern const char* SPECIAL_FPU_STRINGS[3];
extern const char* VU0_STRINGS[32];

}

#endif
