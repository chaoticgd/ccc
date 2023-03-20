#ifndef _CCC_DATA_REFINEMENT_H
#define _CCC_DATA_REFINEMENT_H

#include "analysis.h"
#include "module.h"

namespace ccc {

void refine_global_variables(HighSymbolTable& high, const std::vector<Module*>& modules);
void refine_static_local_variables(HighSymbolTable& high, const std::vector<Module*>& modules);

}

#endif
