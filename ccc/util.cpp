#include "ccc.h"

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iterator>
#include <stdexcept>
#include <filesystem>

namespace ccc {

std::vector<u8> read_binary_file(const fs::path& path) {
	FILE* file = fopen(path.string().c_str(), "rb");
	verify(file, "Failed to open file '%s'.", path.string().c_str());
	s64 size = file_size(file);
	std::vector<u8> output(size);
	verify(fread(output.data(), size, 1, file) == 1, "Failed to read file '%s'.", path.string().c_str());
	return output;
}

s64 file_size(FILE* file) {
	s64 pos = ftell(file);
	fseek(file, 0, SEEK_END);
	s64 size = ftell(file);
	fseek(file, pos, SEEK_SET);
	return size;
}

std::string get_string(const std::vector<u8>& bytes, u64 offset) {
	verify(offset < bytes.size(), "Tried to read a string past the end of the buffer.");
	std::string result;
	for(u64 i = offset; i < bytes.size() && bytes[i] != '\0'; i++) {
		result += bytes[i];
	}
	return result;
}

const char* get_c_string(const std::vector<u8>& bytes, u64 offset) {
	verify(offset < bytes.size(), "Tried to read a string past the end of the buffer.");
	for(const unsigned char* c = bytes.data() + offset; c < bytes.data() + bytes.size(); c++) {
		if(*c == '\0') {
			return (const char*) &bytes[offset];
		}
	}
	verify_not_reached("Unexpected end of buffer while reading string.");
}

std::string string_format(const char* format, va_list args) {
	static char buffer[16 * 1024];
	vsnprintf(buffer, 16 * 1024, format, args);
	return std::string(buffer);
}

std::string stringf(const char* format, ...) {
	va_list args;
	va_start(args, format);
	std::string string = string_format(format, args);
	va_end(args);
	return string;
}

std::pair<std::string, bool> merge_paths(const std::string& base, const std::string& path) {
	// Try to figure out if we're dealing with a Windows path of a UNIX path.
	bool is_windows_path = false;
	if(base.empty()) {
		is_windows_path = guess_is_windows_path(path.c_str());
	} else {
		is_windows_path = guess_is_windows_path(base.c_str());
	}
	
	// Actually merge the paths. If path is the entire path, we don't need to
	// append base onto the front, so check for that now.
	bool is_absolute_unix = (path.size() >= 1) && path[0] == '/' || path[0] == '\\';
	bool is_absolute_windows = (path.size() >= 3) && path[1] == ':' && (path[2] == '/' || path[2] == '\\');
	if(base.empty() || is_absolute_unix || is_absolute_windows) {
		return {normalise_path(path.c_str(), is_windows_path), is_windows_path};
	}
	return {normalise_path((base + "/" + path).c_str(), is_windows_path), is_windows_path};
}

std::string normalise_path(const char* input, bool use_backslashes_as_path_separators) {
	bool is_absolute = false;
	std::optional<char> drive_letter;
	std::vector<std::string> parts;
	
	// Parse the beginning of the path.
	if(*input == '/' || *input == '\\') { // UNIX path, drive relative Windows path or UNC Windows path.
		is_absolute = true;
	} else if(isalpha(*input) && input[1] == ':' && (input[2] == '/' || input[2] == '\\')) { // Absolute Windows path.
		is_absolute = true;
		drive_letter = toupper(*input);
		input += 2;
	} else {
		parts.emplace_back();
	}
	
	// Parse the rest of the path.
	while(*input != 0) {
		if(*input == '/' || *input == '\\') {
			while(*input == '/' || *input == '\\') input++;
			parts.emplace_back();
		} else {
			parts.back() += *(input++);
		}
	}
	
	// Remove "." and ".." parts.
	for(s32 i = 0; i < (s32) parts.size(); i++) {
		if(parts[i] == ".") {
			parts.erase(parts.begin() + i);
			i--;
		} else if(parts[i] == ".." && i > 0 && parts[i - 1] != "..") {
			parts.erase(parts.begin() + i);
			parts.erase(parts.begin() + i - 1);
			i -= 2;
		}
	}
	
	// Output the path in a normal form.
	std::string output;
	if(is_absolute) {
		if(drive_letter.has_value()) {
			output += *drive_letter;
			output += ":";
		}
		output += use_backslashes_as_path_separators ? '\\' : '/';
	}
	for(size_t i = 0; i < parts.size(); i++) {
		output += parts[i];
		if(i != parts.size() - 1) {
			output += use_backslashes_as_path_separators ? '\\' : '/';
		}
	}
	
	return output;
}

bool guess_is_windows_path(const char* path) {
	for(const char* ptr = path; *ptr != 0; ptr++) {
		if(*ptr == '\\') {
			return true;
		} else if(*ptr == '/') {
			return false;
		}
	}
	return false;
}

}
