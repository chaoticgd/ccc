#ifndef _CCC_STABS_TO_AST_H
#define _CCC_STABS_TO_AST_H

#include "ast.h"
#include "stabs.h"

namespace ccc {
	
struct StabsToAstState {
	s32 file_index;
	std::map<StabsTypeNumber, const StabsType*>* stabs_types;
};

std::unique_ptr<ast::Node> stabs_type_to_ast_and_handle_errors(const StabsType& type, const StabsToAstState& state, s32 abs_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute);
Result<std::unique_ptr<ast::Node>> stabs_data_type_symbol_to_ast(const ParsedSymbol& symbol, const StabsToAstState& state);
Result<std::unique_ptr<ast::Node>> stabs_type_to_ast(const StabsType& type, const StabsToAstState& state, s32 abs_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute);
Result<std::unique_ptr<ast::Node>> stabs_field_to_ast(const StabsField& field, const StabsToAstState& state, s32 abs_parent_offset_bytes, s32 depth);
ast::AccessSpecifier stabs_field_visibility_to_access_specifier(StabsFieldVisibility visibility);

}

#endif
