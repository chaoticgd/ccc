#include "ccc.h"

std::string read_string(const std::vector<u8>& bytes, u64 offset) {
	std::string result;
	for(u64 i = offset; i < bytes.size(); i++) {
		if(bytes[i] == 0) {
			break;
		} else {
			result += bytes[i];
		}
	}
	return result;
}
