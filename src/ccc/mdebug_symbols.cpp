// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "mdebug_symbols.h"

namespace ccc::mdebug {

Result<std::vector<ParsedSymbol>> parse_symbols(const std::vector<mdebug::Symbol>& input, mdebug::SourceLanguage detected_language) {
	std::vector<ParsedSymbol> output;
	std::string prefix;
	for(const mdebug::Symbol& symbol : input) {
		if(symbol.is_stabs()) {
			switch(symbol.code()) {
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
							
							ParsedSymbol& parsed = output.emplace_back();
							parsed.type = ParsedSymbolType::NAME_COLON_TYPE;
							parsed.raw = &symbol;
							
							Result<StabsSymbol> stabs_symbol_result = parse_stabs_symbol(symbol_string.c_str());
							CCC_RETURN_IF_ERROR(stabs_symbol_result);
							parsed.name_colon_type = std::move(*stabs_symbol_result);
						}
					} else {
						CCC_CHECK(prefix.empty(), "Invalid STABS continuation.");
						if(symbol.code() == mdebug::N_FUN) {
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
				case mdebug::N_LBRAC: { // Begin block
					ParsedSymbol& begin_block = output.emplace_back();
					begin_block.type = ParsedSymbolType::LBRAC;
					begin_block.raw = &symbol;
					break;
				}
				case mdebug::N_RBRAC: { // End block
					ParsedSymbol& end_block = output.emplace_back();
					end_block.type = ParsedSymbolType::RBRAC;
					end_block.raw = &symbol;
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
					CCC_WARN("Unhandled N_%s symbol: %s", mdebug::stabs_code_to_string(symbol.code()), symbol.string);
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

}
