#ifndef _CCC_DATA_REFINEMENT_H
#define _CCC_DATA_REFINEMENT_H

#include "analysis.h"
#include "module.h"

namespace ccc {

void refine_variables(HighSymbolTable& high, const std::vector<Module*>& modules);

}

#endif
