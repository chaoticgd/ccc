#include "registers.h"

namespace ccc::mips {

const char** REGISTER_STRING_TABLES[7] = {
	&INVALID_REGISTER_STRING,
	GPR_STRINGS,
	SPECIAL_GPR_STRINGS,
	SCP_STRINGS,
	FPR_STRINGS,
	SPECIAL_FPU_STRINGS,
	VU0_STRINGS
};

const u64 REGISTER_STRING_TABLE_SIZES[7] = {
	1,
	ARRAY_SIZE(GPR_STRINGS),
	ARRAY_SIZE(SPECIAL_GPR_STRINGS),
	ARRAY_SIZE(SCP_STRINGS),
	ARRAY_SIZE(FPR_STRINGS),
	ARRAY_SIZE(SPECIAL_FPU_STRINGS),
	ARRAY_SIZE(VU0_STRINGS)
};

const char* INVALID_REGISTER_STRING = "BADREGISTER";

const char* GPR_STRINGS[32] = {
	"zero",
	"at",
	"v0",
	"v1",
	"a0",
	"a1",
	"a2",
	"a3",
	"t0",
	"t1",
	"t2",
	"t3",
	"t4",
	"t5",
	"t6",
	"t7",
	"s0",
	"s1",
	"s2",
	"s3",
	"s4",
	"s5",
	"s6",
	"s7",
	"t8",
	"t9",
	"k0",
	"k1",
	"gp",
	"sp",
	"fp",
	"ra"
};

const char* SPECIAL_GPR_STRINGS[6] = {
	"pc",
	"hi",
	"lo",
	"hi1",
	"lo1",
	"sa"
};

const char* SCP_STRINGS[32] = {
	"Index",
	"Random",
	"EntryLo0",
	"EntryLo1",
	"Context",
	"PageMask",
	"Wired",
	"(Reserved)",
	"BadVAddr",
	"Count",
	"EntryHi",
	"Compare",
	"Status",
	"Cause",
	"EPC",
	"PRId",
	"Config",
	"(Reserved)",
	"BadPAddr",
	"Debug",
	"Perf",
	"(Reserved)",
	"(Reserved)",
	"TagLo",
	"TagHi",
	"ErrorEPC",
	"(Reserved)"
};

const char* FPR_STRINGS[32] = {
	"fpr0",
	"fpr1",
	"fpr2",
	"fpr3",
	"fpr4",
	"fpr5",
	"fpr6",
	"fpr7",
	"fpr8",
	"fpr9",
	"fpr10",
	"fpr11",
	"fpr12",
	"fpr13",
	"fpr14",
	"fpr15",
	"fpr16",
	"fpr17",
	"fpr18",
	"fpr19",
	"fpr20",
	"fpr21",
	"fpr22",
	"fpr23",
	"fpr24",
	"fpr25",
	"fpr26",
	"fpr27",
	"fpr28",
	"fpr29",
	"fpr30",
	"fpr31"
};

const char* SPECIAL_FPU_STRINGS[3] = {
	"fcr0",
	"fcr31",
	"acc"
};

const char* VU0_STRINGS[32] = {
	"vf0",
	"vf1",
	"vf2",
	"vf3",
	"vf4",
	"vf5",
	"vf6",
	"vf7",
	"vf8",
	"vf9",
	"vf10",
	"vf11",
	"vf12",
	"vf13",
	"vf14",
	"vf15",
	"vf16",
	"vf17",
	"vf18",
	"vf19",
	"vf20",
	"vf21",
	"vf22",
	"vf23",
	"vf24",
	"vf25",
	"vf26",
	"vf27",
	"vf28",
	"vf29",
	"vf30",
	"vf31"
};

std::pair<RegisterClass, s32> map_gcc_register_index(s32 index) {
	if(index >= 0 && index <= 31) {
		return {RegisterClass::GPR, index};
	} else {
		return{RegisterClass::INVALID, 0};//verify_not_reached("Bad register index %d. Please file a bug report!", index);
	}
}

}
