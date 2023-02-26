#include "command_line.h"

namespace ccc::cli {

Options parse_arguments(int argc, char** argv, const OptionsInfo& input) {
	Options options;
	verify(argc >= 2, "Too few arguments.");
	const char* command = argv[1];
	bool require_input_path = false;
	for(const ModeInfo& info : input.modes) {
		if(strcmp(command, info.name_1) == 0 || strcmp(command, info.name_2) == 0 || strcmp(command, info.name_3) == 0) {
			options.mode = info.mode;
			if(info.mode_flags & MF_REQUIRE_INPUT_PATH) {
				require_input_path = true;
			}
			break;
		}
	}
	bool input_path_provided = false;
	for(s32 i = 2; i < argc; i++) {
		const char* arg = argv[i];
		bool matches_flag = false;
		for(const FlagInfo& info : input.flags) {
			if(strcmp(arg, info.name_1) == 0 || strcmp(arg, info.name_2) == 0 || strcmp(arg, info.name_3) == 0) {
				options.flags |= info.flag;
				matches_flag = true;
			}
		}
		if(!matches_flag) {
			if(strcmp(arg, "--output") == 0) {
				if(i + 1 < argc) {
					options.output_file = argv[++i];
				} else {
					verify_not_reached("No output path specified.");
				}
			} else if(strncmp(arg, "--", 2) == 0) {
				verify_not_reached("Unknown option '%s'.", arg);
			} else if(input_path_provided) {
				verify_not_reached("Multiple input paths specified.");
			} else {
				options.input_file = argv[i];
				input_path_provided = true;
			}
		}
	}
	verify(!require_input_path || !options.input_file.empty(), "No input path specified.");
	return options;
}

FILE* get_output_file(const Options& options) {
	if(!options.output_file.empty()) {
		FILE* out = open_file_w(options.output_file.c_str());
		verify(out, "Failed to open output file '%s'.", options.output_file.string().c_str());
		return out;
	} else {
		return stdout;
	}
}

}
