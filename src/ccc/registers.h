// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "util.h"

namespace ccc::mips {

enum class RegisterClass {
	INVALID = 0,
	GPR = 1,
	SPECIAL_GPR = 2,
	SCP = 3,
	FPR = 4,
	SPECIAL_FPU = 5,
	VU0 = 6
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

enum class FPR {
	R0 = 0,
	R1 = 1,
	R2 = 2,
	R3 = 3,
	R4 = 4,
	R5 = 5,
	R6 = 6,
	R7 = 7,
	R8 = 8,
	R9 = 9,
	R10 = 10,
	R11 = 11,
	R12 = 12,
	R13 = 13,
	R14 = 14,
	R15 = 15,
	R16 = 16,
	R17 = 17,
	R18 = 18,
	R19 = 19,
	R20 = 20,
	R21 = 21,
	R22 = 22,
	R23 = 23,
	R24 = 24,
	R25 = 25,
	R26 = 26,
	R27 = 27,
	R28 = 28,
	R29 = 29,
	R30 = 30,
	R31 = 31
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

extern const char** REGISTER_STRING_TABLES[7];
extern const u64 REGISTER_STRING_TABLE_SIZES[7];
extern const char* REGISTER_CLASSES[7];

extern const char* INVALID_REGISTER_STRING;
extern const char* GPR_STRINGS[32];
extern const char* SPECIAL_GPR_STRINGS[6];
extern const char* SCP_STRINGS[32];
extern const char* FPR_STRINGS[32];
extern const char* SPECIAL_FPU_STRINGS[3];
extern const char* VU0_STRINGS[32];

std::pair<RegisterClass, s32> map_dbx_register_index(s32 index);

}
