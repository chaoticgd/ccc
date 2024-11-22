// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#pragma once

#include "ast.h"
#include "dwarf_section.h"

namespace ccc::dwarf {

class TypeImporter {
public:
	TypeImporter(SymbolDatabase& database, const SectionReader& dwarf, u32 importer_flags);
	
	Result<std::unique_ptr<ast::Node>> type_attribute_to_ast(const DIE& die);
	Result<std::unique_ptr<ast::Node>> type_to_ast(const Type& type);
	Result<std::unique_ptr<ast::Node>> fundamental_type_to_ast(FundamentalType fund_type);
	Result<std::unique_ptr<ast::Node>> die_to_ast(const DIE& die);
	
protected:
	Result<std::unique_ptr<ast::Node>> array_type_to_ast(const DIE& die);
	
	SymbolDatabase& m_database;
	const SectionReader& m_dwarf;
	u32 m_importer_flags;
};

bool die_is_type(const DIE& die);

}
