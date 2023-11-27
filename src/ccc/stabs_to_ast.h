// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "ast.h"
#include "stabs.h"
#include "symbols.h"

namespace ccc {
	
struct StabsToAstState {
	u32 file_handle;
	std::map<StabsTypeNumber, const StabsType*>* stabs_types;
};

std::unique_ptr<ast::Node> stabs_type_to_ast_and_handle_errors(const StabsType& type, const StabsToAstState& state, s32 abs_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute);
Result<std::unique_ptr<ast::Node>> stabs_data_type_symbol_to_ast(const ParsedSymbol& symbol, const StabsToAstState& state);
Result<std::unique_ptr<ast::Node>> stabs_type_to_ast(const StabsType& type, const StabsToAstState& state, s32 abs_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute);
ast::AccessSpecifier stabs_field_visibility_to_access_specifier(StabsFieldVisibility visibility);

}
