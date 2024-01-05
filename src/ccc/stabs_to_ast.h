// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "ast.h"
#include "stabs.h"

namespace ccc {
	
struct StabsToAstState {
	u32 file_handle;
	std::map<StabsTypeNumber, const StabsType*>* stabs_types;
	u32 importer_flags;
	DemanglerFunctions demangler;
};

Result<std::unique_ptr<ast::Node>> stabs_type_to_ast(
	const StabsType& type,
	const StabsType* enclosing_struct,
	const StabsToAstState& state,
	s32 depth,
	bool substitute_type_name,
	bool force_substitute);
ast::AccessSpecifier stabs_field_visibility_to_access_specifier(StabsStructOrUnionType::Visibility visibility);

}
