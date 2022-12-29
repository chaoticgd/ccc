#include "symbols.h"

namespace ccc {

#define SYMBOLS_DEBUG(...) //__VA_ARGS__
#define SYMBOLS_DEBUG_PRINTF(...) SYMBOLS_DEBUG(printf(__VA_ARGS__);)

static void validate_symbol_descriptor(StabsSymbolDescriptor descriptor);

std::vector<ParsedSymbol> parse_symbols(const std::vector<Symbol>& input, SourceLanguage detected_language) {
	std::vector<ParsedSymbol> output;
	std::string prefix;
	bool last_symbol_was_end = false;
	for(const Symbol& symbol : input) {
		bool is_stabs_symbol =
			   (symbol.storage_type == SymbolType::NIL // types, globals
				&& symbol.storage_class != SymbolClass::COMPILER_VERSION_INFO)
			|| (last_symbol_was_end // functions
				&& symbol.storage_type == SymbolType::LABEL
				&& (detected_language == SourceLanguage::C
					|| detected_language == SourceLanguage::CPP))
			|| (symbol.storage_type == SymbolType::STATIC // static globals
				&& (s32) symbol.storage_class >= 1
				&& (s32) symbol.storage_class <= 3
				&& symbol.string.find(":") != std::string::npos) // omit various non-stabs symbols
				&& (symbol.string.size() < 3 // false positive: windows paths
					|| !(symbol.string[1] == ':'
						&& (symbol.string[2] == '/'
							|| symbol.string[2] == '\\')));
		if(is_stabs_symbol) {
			// Some STABS symbols are split between multiple strings.
			if(!symbol.string.empty()) {
				if(symbol.string[symbol.string.size() - 1] == '\\') {
					prefix += symbol.string.substr(0, symbol.string.size() - 1);
				} else {
					std::string symbol_string = prefix + symbol.string;
					prefix.clear();
					bool stab_valid = true;
					stab_valid &= symbol_string[0] != '$';
					stab_valid &= symbol_string != "gcc2_compiled.";
					stab_valid &= symbol_string != "__gnu_compiled_c";
					stab_valid &= symbol_string != "__gnu_compiled_cpp";
					if(stab_valid) {
						ParsedSymbol stabs_symbol = parse_stabs_symbol(symbol_string.c_str());
						stabs_symbol.raw = &symbol;
						output.emplace_back(std::move(stabs_symbol));
					}
				}
			} else {
				verify(prefix.empty(), "Invalid STABS continuation.");
			}
		} else {
			ParsedSymbol non_stabs_symbol;
			non_stabs_symbol.raw = &symbol;
			output.emplace_back(std::move(non_stabs_symbol));
		}
		last_symbol_was_end = (symbol.storage_type == SymbolType::END);
	}
	return output;
}

ParsedSymbol parse_stabs_symbol(const char* input) {
	SYMBOLS_DEBUG_PRINTF("PARSING %s\n", input);
	
	ParsedSymbol symbol;
	symbol.is_stabs = true;
	symbol.name = eat_dodgy_stabs_identifier(input);
	expect_s8(input, ':', "identifier");
	verify(*input != '\0', ERR_END_OF_SYMBOL);
	if(*input >= '0' && *input <= '9') {
		symbol.descriptor = StabsSymbolDescriptor::LOCAL_VARIABLE;
	} else {
		symbol.descriptor = (StabsSymbolDescriptor) eat_s8(input);
	}
	validate_symbol_descriptor(symbol.descriptor);
	verify(*input != '\0', ERR_END_OF_SYMBOL);
	if(*input == 't') {
		input++;
	}
	symbol.type = parse_stabs_type(input);
	// This is a bit of hack to make it so variable names aren't used as type
	// names e.g.the STABS symbol "somevar:P123=*456" may be referenced by the
	// type number 123, but the type name is not "somevar".
	bool is_type = symbol.descriptor == StabsSymbolDescriptor::TYPE_NAME
		|| symbol.descriptor == StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG; 
	if(is_type) {
		symbol.type->name = symbol.name;
	}
	symbol.type->is_typedef = symbol.descriptor == StabsSymbolDescriptor::TYPE_NAME;
	symbol.type->is_root = true;
	return symbol;
}

static void validate_symbol_descriptor(StabsSymbolDescriptor descriptor) {
	switch(descriptor) {
		case StabsSymbolDescriptor::LOCAL_VARIABLE:
		case StabsSymbolDescriptor::REFERENCE_PARAMETER:
		case StabsSymbolDescriptor::LOCAL_FUNCTION:
		case StabsSymbolDescriptor::GLOBAL_FUNCTION:
		case StabsSymbolDescriptor::GLOBAL_VARIABLE:
		case StabsSymbolDescriptor::REGISTER_PARAMETER:
		case StabsSymbolDescriptor::VALUE_PARAMETER:
		case StabsSymbolDescriptor::REGISTER_VARIABLE:
		case StabsSymbolDescriptor::STATIC_GLOBAL_VARIABLE:
		case StabsSymbolDescriptor::TYPE_NAME:
		case StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG:
		case StabsSymbolDescriptor::STATIC_LOCAL_VARIABLE:
			break;
		default:
			verify_not_reached("Unknown symbol descriptor: %c.", (s8) descriptor);
	}
}

}
