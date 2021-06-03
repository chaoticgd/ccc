#pragma once

#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <filesystem>

// *****************************************************************************
// util.cpp
// *****************************************************************************

namespace fs = std::filesystem;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// Like assert, but for user errors.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
template <typename... Args>
void verify(bool condition, const char* error_message, Args... args) {
	if(!condition) {
		fprintf(stderr, error_message, args...);
		exit(1);
	}
}
#pragma GCC diagnostic pop

#ifdef _MSC_VER
	#define packed_struct(name, ...) \
		__pragma(pack(push, 1)) struct name { __VA_ARGS__ } __pragma(pack(pop));
#else
	#define packed_struct(name, ...) \
		struct __attribute__((__packed__)) name { __VA_ARGS__ };
#endif

template <typename T>
const T& get_packed(const std::vector<u8>& bytes, u64 offset, const char* subject) {
	verify(bytes.size() >= offset + sizeof(T), "error: Failed to read %s.\n", subject);
	return *(const T*) &bytes[offset];
}

std::string read_string(const std::vector<u8>& bytes, u64 offset);

struct Range {
	s32 begin;
	s32 end;
};

// *****************************************************************************
// Core data structures
// *****************************************************************************

struct ProgramImage {
	std::vector<u8> bytes;
};

// This is like a simplified ElfSectionType.
enum class ProgramSectionType {
	MIPS_DEBUG,
	OTHER
};

struct ProgramSection {
	u64 image;
	u64 file_offset;
	u64 size;
	ProgramSectionType type;
};

struct SymFileDescriptor {
	std::string name;
	Range procedures;
};

struct SymProcedureDescriptor {
	std::string name;
};

struct SymbolTable {
	u64 file_descriptor_table_offset;
	std::vector<SymFileDescriptor> files;
	std::vector<SymProcedureDescriptor> procedures;
};

struct Program {
	std::vector<ProgramImage> images;
	std::vector<ProgramSection> sections;
};

// *****************************************************************************
// elf.cpp
// *****************************************************************************

ProgramImage read_program_image(fs::path path);
void parse_elf_file(Program& program, u64 image_index);

// *****************************************************************************
// mdebug.cpp
// *****************************************************************************

SymbolTable parse_symbol_table(const ProgramImage& image, const ProgramSection& section);
