#ifndef _CCC_AST_H
#define _CCC_AST_H

#include "util.h"
#include "stabs.h"

namespace ccc {

struct TypeName {
	std::string first_part;
	std::vector<s32> array_indices;
};
enum class AstNodeDescriptor {
	LEAF, ENUM, STRUCT, UNION, TYPEDEF
};
using EnumFields = std::vector<std::pair<s32, std::string>>;
struct AstBaseClass {
	s8 visibility;
	s32 offset;
	std::string type_name;
};
struct AstNode {
	bool is_static = false;
	s32 offset;
	s32 size;
	std::string name;
	AstNodeDescriptor descriptor;
	std::vector<s32> array_indices;
	bool top_level = false;
	struct {
		std::string type_name;
	} leaf;
	struct {
		EnumFields fields;
	} enum_type;
	struct {
		std::vector<AstBaseClass> base_classes;
		std::vector<AstNode> fields;
	} struct_or_union;
	struct {
		std::string type_name;
	} typedef_type;
	const StabsSymbol* symbol = nullptr;
	// Fields below populated by deduplicate_type.
	std::set<std::string> source_files;
	bool conflicting_types = false; // Are there other differing types with the same name?
};

std::map<s32, TypeName> resolve_c_type_names(const std::map<s32, const StabsType*>& types);
struct FieldInfo {
	bool is_static = false;
	s32 offset;
	s32 size;
	const StabsType& type;
	const std::string& name;
};
std::optional<AstNode> stabs_symbol_to_ast(const StabsSymbol& symbol, const std::map<s32, TypeName>& type_names);
std::vector<AstNode> deduplicate_ast(const std::vector<std::pair<std::string, std::vector<AstNode>>>& per_file_ast);

}

#endif
