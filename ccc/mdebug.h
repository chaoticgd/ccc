#include "util.h"
#include "coredata.h"

namespace ccc {

SymbolTable parse_symbol_table(const ProgramImage& image, const ProgramSection& section);
const char* symbol_type(SymbolType type);
const char* symbol_class(SymbolClass symbol_class);

}
