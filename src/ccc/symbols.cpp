#include "symbols.h"

namespace ccc {

#define SYMBOLS_DEBUG(...) //__VA_ARGS__
#define SYMBOLS_DEBUG_PRINTF(...) SYMBOLS_DEBUG(printf(__VA_ARGS__);)

static bool validate_symbol_descriptor(StabsSymbolDescriptor descriptor);

Result<std::vector<ParsedSymbol>> parse_symbols(const std::vector<mdebug::Symbol>& input, mdebug::SourceLanguage detected_language) {
	std::vector<ParsedSymbol> output;
	std::string prefix;
	for(const mdebug::Symbol& symbol : input) {
		if(symbol.is_stabs) {
			switch(symbol.code) {
				case mdebug::N_GSYM: // Global variable
				case mdebug::N_FUN: // Function
				case mdebug::N_STSYM: // Data section static global variable
				case mdebug::N_LCSYM: // BSS section static global variable
				case mdebug::N_RSYM: // Register variable
				case mdebug::N_LSYM: // Automatic variable or type definition
				case mdebug::N_PSYM: { // Parameter variable
					// Some STABS symbols are split between multiple strings.
					if(symbol.string[0] != '\0') {
						if(symbol.string[strlen(symbol.string) - 1] == '\\') {
							prefix += std::string(symbol.string, symbol.string + strlen(symbol.string) - 1);
						} else {
							std::string symbol_string = prefix + symbol.string;
							prefix.clear();
							Result<ParsedSymbol> stabs_symbol = parse_stabs_type_symbol(symbol_string.c_str());
							CCC_RETURN_IF_ERROR(stabs_symbol);
							(*stabs_symbol).raw = &symbol;
							output.emplace_back(std::move(*stabs_symbol));
						}
					} else {
						CCC_CHECK(prefix.empty(), "Invalid STABS continuation.");
						if(symbol.code == mdebug::N_FUN) {
							ParsedSymbol& func_end = output.emplace_back();
							func_end.type = ParsedSymbolType::FUNCTION_END;
							func_end.raw = &symbol;
						}
					}
					break;
				}
				case mdebug::N_SOL: { // Sub-source file
					ParsedSymbol& sub = output.emplace_back();
					sub.type = ParsedSymbolType::SUB_SOURCE_FILE;
					sub.raw = &symbol;
					break;
				}
				case mdebug::N_LBRAC: { // Begin scope
					ParsedSymbol& begin_scope = output.emplace_back();
					begin_scope.type = ParsedSymbolType::LBRAC;
					begin_scope.raw = &symbol;
					if(strlen(symbol.string) >= 4) {
						begin_scope.lrbrac.number = atoi(&symbol.string[4]);
					}
					break;
				}
				case mdebug::N_RBRAC: { // End scope
					ParsedSymbol& end_scope = output.emplace_back();;
					end_scope.type = ParsedSymbolType::RBRAC;
					end_scope.raw = &symbol;
					if(strlen(symbol.string) >= 4) {
						end_scope.lrbrac.number = atoi(&symbol.string[4]);
					}
					break;
				}
				case mdebug::N_SO: { // Source filename
					ParsedSymbol& so_symbol = output.emplace_back();
					so_symbol.type = ParsedSymbolType::SOURCE_FILE;
					so_symbol.raw = &symbol;
					break;
				}
				case mdebug::STAB:
				case mdebug::N_OPT:
				case mdebug::N_BINCL: {
					break;
				}
				case mdebug::N_FNAME:
				case mdebug::N_MAIN:
				case mdebug::N_PC:
				case mdebug::N_NSYMS:
				case mdebug::N_NOMAP:
				case mdebug::N_OBJ:
				case mdebug::N_M2C:
				case mdebug::N_SLINE:
				case mdebug::N_DSLINE:
				case mdebug::N_BSLINE:
				case mdebug::N_EFD:
				case mdebug::N_EHDECL:
				case mdebug::N_CATCH:
				case mdebug::N_SSYM:
				case mdebug::N_EINCL:
				case mdebug::N_ENTRY:
				case mdebug::N_EXCL:
				case mdebug::N_SCOPE:
				case mdebug::N_BCOMM:
				case mdebug::N_ECOMM:
				case mdebug::N_ECOML:
				case mdebug::N_NBTEXT:
				case mdebug::N_NBDATA:
				case mdebug::N_NBBSS:
				case mdebug::N_NBSTS:
				case mdebug::N_NBLCS:
				case mdebug::N_LENG: {
					CCC_WARN("Unhandled N_%s symbol: %s", mdebug::stabs_code(symbol.code), symbol.string);
					break;
				}
			}
		} else {
			ParsedSymbol& non_stabs_symbol = output.emplace_back();
			non_stabs_symbol.type = ParsedSymbolType::NON_STABS;
			non_stabs_symbol.raw = &symbol;
		}
	}
	return output;
}

Result<ParsedSymbol> parse_stabs_type_symbol(const char* input) {
	SYMBOLS_DEBUG_PRINTF("PARSING %s\n", input);
	
	ParsedSymbol symbol;
	symbol.type = ParsedSymbolType::NAME_COLON_TYPE;
	
	std::optional<std::string> name = eat_dodgy_stabs_identifier(input);
	CCC_CHECK(name.has_value(), "Cannot parse stabs symbol name.");
	symbol.name_colon_type.name = *name;
	
	CCC_EXPECT_CHAR(input, ':', "identifier");
	CCC_CHECK(*input != '\0', "Unexpected end of input.");
	if((*input >= '0' && *input <= '9') || *input == '(') {
		symbol.name_colon_type.descriptor = StabsSymbolDescriptor::LOCAL_VARIABLE;
	} else {
		std::optional<char> symbol_descriptor = eat_char(input);
		CCC_CHECK(symbol_descriptor.has_value(), "Cannot parse symbol descriptor.");
		symbol.name_colon_type.descriptor = (StabsSymbolDescriptor) *symbol_descriptor;
	}
	CCC_CHECK(validate_symbol_descriptor(symbol.name_colon_type.descriptor),
		"Invalid symbol descriptor '%c'.",
		(char) symbol.name_colon_type.descriptor);
	CCC_CHECK(*input != '\0', "Unexpected end of input.");
	if(*input == 't') {
		input++;
	}
	
	auto type = parse_stabs_type(input);
	CCC_RETURN_IF_ERROR(type);
	symbol.name_colon_type.type = std::move(*type);
	
	// Make sure that variable names aren't used as type names e.g. the STABS
	// symbol "somevar:P123=*456" may be referenced by the type number 123, but
	// the type name is not "somevar".
	bool is_type = symbol.name_colon_type.descriptor == StabsSymbolDescriptor::TYPE_NAME
		|| symbol.name_colon_type.descriptor == StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG; 
	if(is_type) {
		symbol.name_colon_type.type->name = symbol.name_colon_type.name;
	}
	
	symbol.name_colon_type.type->is_typedef = symbol.name_colon_type.descriptor == StabsSymbolDescriptor::TYPE_NAME;
	symbol.name_colon_type.type->is_root = true;
	
	return symbol;
}

static bool validate_symbol_descriptor(StabsSymbolDescriptor descriptor) {
	bool valid;
	switch(descriptor) {
		case StabsSymbolDescriptor::LOCAL_VARIABLE:
		case StabsSymbolDescriptor::REFERENCE_PARAMETER_A:
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
		case StabsSymbolDescriptor::REFERENCE_PARAMETER_V:
			valid = true;
			break;
		default:
			valid = false;
			break;
	}
	return valid;
}

}
