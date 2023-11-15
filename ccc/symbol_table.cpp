#include "symbol_table.h"

namespace ccc {

u32 identify_symbol_tables(const ElfFile& elf) {
	u32 result = 0;
	for(const ElfSection& section : elf.sections) {
		if(section.name == ".symtab" && section.size > 0) {
			result |= SYMTAB;
		}
		if(section.name == ".mdebug" && section.size > 0) {
			result |= MDEBUG;
		}
		if(section.name == ".stab" && section.size > 0) {
			result |= STAB;
		}
		if(section.name == ".debug" && section.size > 0) {
			result |= DWARF;
		}
		if(section.name == ".sndata" && section.size > 0) {
			result |= SNDATA;
		}
	}
	return result;
}

// *****************************************************************************

SymbolTableGuardian::SymbolTableGuardian() {}

std::optional<SymbolTableHandle> SymbolTableGuardian::get_current_handle() const {
	std::lock_guard<std::mutex> guard(m_big_symbol_table_lock);
	return m_current_handle;
}

bool SymbolTableGuardian::read(SymbolTableHandle handle, std::function<void(const SymbolTable&)> callback) const {
	std::lock_guard<std::mutex> guard(m_big_symbol_table_lock);
	if(handle != m_current_handle) {
		return false;
	}
	callback(m_symbol_table);
	return true;
}

void SymbolTableGuardian::overwrite(SymbolTable symbol_table) {
	std::lock_guard<std::mutex> guard(m_big_symbol_table_lock);
	m_symbol_table = std::move(symbol_table);
	m_current_handle = s_next_handle++;
}

std::atomic<s32> SymbolTableGuardian::s_next_handle;

// *****************************************************************************

void print_symbol_table_formats_to_string(FILE* out, u32 formats) {
	bool printed = false;
	for(u32 bit = 1; bit < MAX_SYMBOL_TABLE; bit <<= 1) {
		u32 format = formats & bit;
		if(format != 0) {
			fprintf(out, "%s%s", printed ? " " : "", symbol_table_format_to_string((SymbolTableFormat) format));
			printed = true;
		}
	}
	if(!printed) {
		fprintf(out, "none");
	}
}

const char* symbol_table_format_to_string(SymbolTableFormat format) {
	switch(format) {
		case SYMTAB: return "symtab";
		case MAP: return "map";
		case MDEBUG: return "mdebug";
		case STAB: return "stab";
		case DWARF: return "dwarf";
		case SNDATA: return "sndata";
		case SNDLL: return "sndll";
	}
	return "";
}

}
