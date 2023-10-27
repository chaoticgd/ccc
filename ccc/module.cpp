#include "module.h"

namespace ccc {

ModuleSection* Module::lookup_section(const char* name) {
	for(ModuleSection& section : sections) {
		if(section.name == name) {
			return &section;
		}
	}
	return nullptr;
}

std::optional<u32> Module::file_offset_to_virtual_address(u32 file_offset) {
	for(ModuleSegment& segment : segments) {
		if(file_offset >= segment.file_offset && file_offset < segment.file_offset + segment.size) {
			return segment.virtual_address + file_offset - segment.file_offset;
		}
	}
	return std::nullopt;
}

Result<void> read_virtual(u8* dest, u32 address, u32 size, const std::vector<Module*>& modules) {
	while(size > 0) {
		bool mapped = false;
		
		for(const Module* module : modules) {
			for(const ModuleSegment& segment : module->segments) {
				if(address >= segment.virtual_address && address < segment.virtual_address + segment.size) {
					u32 offset = address - segment.virtual_address;
					u32 copy_size = std::min(segment.size - offset, size);
					CCC_CHECK(segment.file_offset + offset + copy_size <= module->image.size(), "Segment is bad or image is too small.");
					memcpy(dest, &module->image[segment.file_offset + offset], copy_size);
					dest += copy_size;
					address += copy_size;
					size -= copy_size;
					mapped = true;
				}
			}
		}
		
		CCC_CHECK(mapped, "Tried to read from memory that wouldn't have come from any of the loaded modules");
	}
	return Result<void>();
}

}
