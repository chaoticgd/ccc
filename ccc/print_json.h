#ifndef _CCC_PRINT_JSON_H
#define _CCC_PRINT_JSON_H

#include "analysis.h"

namespace ccc {

void print_json(FILE* dest, const AnalysisResults& src, bool print_per_file_types);

}

#endif
