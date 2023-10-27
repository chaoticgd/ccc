#ifndef _CCC_SYMBOLS_H
#define _CCC_SYMBOLS_H

#include "util.h"
#include "stabs.h"
#include "mdebug.h"

namespace ccc {

enum class StabsSymbolDescriptor : u8 {
	LOCAL_VARIABLE = '_',
	REFERENCE_PARAMETER_A = 'a',
	LOCAL_FUNCTION = 'f',
	GLOBAL_FUNCTION = 'F',
	GLOBAL_VARIABLE = 'G',
	REGISTER_PARAMETER = 'P',
	VALUE_PARAMETER = 'p',
	REGISTER_VARIABLE = 'r',
	STATIC_GLOBAL_VARIABLE = 'S',
	TYPE_NAME = 't',
	ENUM_STRUCT_OR_TYPE_TAG = 'T',
	STATIC_LOCAL_VARIABLE = 'V',
	REFERENCE_PARAMETER_V = 'v'
};

struct StabsType;

enum class ParsedSymbolType {
	NAME_COLON_TYPE,
	SOURCE_FILE,
	SUB_SOURCE_FILE,
	LBRAC,
	RBRAC,
	FUNCTION_END,
	NON_STABS
};

struct ParsedSymbol {
	ParsedSymbolType type;
	const mdebug::Symbol* raw;
	struct { 
		StabsSymbolDescriptor descriptor;
		std::string name;
		std::unique_ptr<StabsType> type;
	} name_colon_type;
	struct {
		s32 number = -1;
	} lrbrac;
};

Result<std::vector<ParsedSymbol>> parse_symbols(const std::vector<mdebug::Symbol>& input, mdebug::SourceLanguage detected_language);
Result<ParsedSymbol> parse_stabs_type_symbol(const char* input);

}

#endif
