#ifndef _CCC_COMMAND_LINE_H
#define _CCC_COMMAND_LINE_H

#include "util.h"

namespace ccc::cli {

struct Options {
	s32 mode = 0;
	u32 flags = 0;
	fs::path input_file;
	fs::path output_file;
};

enum ModeFlags {
	MF_NO_FLAGS = 0,
	MF_REQUIRE_INPUT_PATH = (1 << 0)
};

struct ModeInfo {
	s32 mode = 0;
	u32 mode_flags = 0;
	const char* name_1 = nullptr;
	const char* name_2 = nullptr;
	const char* name_3 = nullptr;
};

struct FlagInfo {
	u32 flag = 0;
	const char* name_1 = nullptr;
	const char* name_2 = nullptr;
	const char* name_3 = nullptr;
};

struct OptionsInfo {
	std::vector<ModeInfo> modes;
	std::vector<FlagInfo> flags;
};

Options parse_arguments(int argc, char** argv, const OptionsInfo& options);
FILE* get_output_file(const Options& options);

}

#endif
