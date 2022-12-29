#ifndef _CCC_SYMBOLS_H
#define _CCC_SYMBOLS_H

#include "util.h"
#include "stabs.h"
#include "mdebug.h"

namespace ccc {

enum class StabsSymbolDescriptor : u8 {
	LOCAL_VARIABLE = '_',
	REFERENCE_PARAMETER = 'a',
	LOCAL_FUNCTION = 'f',
	GLOBAL_FUNCTION = 'F',
	GLOBAL_VARIABLE = 'G',
	REGISTER_PARAMETER = 'P',
	VALUE_PARAMETER = 'p',
	REGISTER_VARIABLE = 'r',
	STATIC_GLOBAL_VARIABLE = 'S',
	TYPE_NAME = 't',
	ENUM_STRUCT_OR_TYPE_TAG = 'T',
	STATIC_LOCAL_VARIABLE = 'V'
};

struct StabsType;

struct ParsedSymbol {
	const Symbol* raw;
	bool is_stabs = false;
	// The fields below are only populated for STABS symbols.
	StabsSymbolDescriptor descriptor;
	std::string name;
	std::unique_ptr<StabsType> type;
};

std::vector<ParsedSymbol> parse_symbols(const std::vector<Symbol>& input, SourceLanguage detected_language);
ParsedSymbol parse_stabs_symbol(const char* input);

}

#endif
