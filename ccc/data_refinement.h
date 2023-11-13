#pragma once

#include "analysis.h"
#include "module.h"

namespace ccc {

void refine_variables(HighSymbolTable& high, const std::vector<Module*>& modules);

}
