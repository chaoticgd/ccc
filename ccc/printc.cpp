#include "ccc.h"

static TypeName resolve_c_type_name(const std::map<s32, const StabsType*>& types, const StabsType* type_ptr);
static void print_type_name(FILE* output, const StabsType& type, const std::map<s32, TypeName>& type_names, s32 depth);
static void print_array_indices(FILE* output);
static void indent(FILE* output, s32 depth);

s32 type_number_of(StabsType* type) {
	verify(type && !type->anonymous, "error: Tried to access type number of anonymous or null type.\n");
	return type->type_number;
}

std::map<s32, TypeName> resolve_c_type_names(const std::map<s32, const StabsType*>& types) {
	std::map<s32, TypeName> type_names;
	for(auto& [type_number, type] : types) {
		assert(type);
		type_names[type_number] = resolve_c_type_name(types, type);
	}
	return type_names;
}

const StabsType* find_type(StabsType* type, const std::map<s32, const StabsType*>& types, s32 outer_type_number) {
	assert(type && !type->anonymous);
	if(type->type_number == outer_type_number) {
		return nullptr;
	}
	auto iterator = types.find(type->type_number);
	verify(iterator != types.end(), "error: Tried to lookup undeclared type.\n");
	assert(iterator->second);
	return iterator->second;
}

// FIXME: Detect indirect recusion e.g. type mappings 1 -> 2, 2 -> 1.
static TypeName resolve_c_type_name(const std::map<s32, const StabsType*>& types, const StabsType* type_ptr) {
	if(!type_ptr) {
		return {"<error type>", {}};
	}
	const StabsType& type = *type_ptr;
	
	if(type.name.has_value()) {
		TypeName name;
		name.first_part = *type.name;
		return name;
	}
	
	if(!type.has_body) {
		TypeName name;
		name.first_part = "<error type>";
		return name;
	}
	
	switch(type.descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE: {
			auto inner_type = find_type(type.type_reference.type.get(), types, type.type_number);
			return resolve_c_type_name(types, inner_type);
		}
		case StabsTypeDescriptor::ARRAY: {
			auto inner_type = find_type(type.array_type.element_type.get(), types, type.type_number);
			TypeName name = resolve_c_type_name(types, inner_type);
			StabsType* index = type.array_type.index_type.get();
			assert(index);
			verify(index->descriptor == StabsTypeDescriptor::RANGE, "error: Invalid index type for array.\n");
			name.array_indices.push_back(index->range_type.high);
			return name;
		}
		case StabsTypeDescriptor::FUNCTION: {
			return {"/* function */ void", {}};
		}
		case StabsTypeDescriptor::RANGE: {
			return {"/* range */ void", {}};
		}
		case StabsTypeDescriptor::STRUCT: {
			return {"/* struct */ void", {}};
		}
		case StabsTypeDescriptor::UNION: {
			return {"/* union */ void", {}};
		}
		case StabsTypeDescriptor::CROSS_REFERENCE: {
			TypeName type_name;
			type_name.first_part = type.cross_reference.identifier;
			return type_name;
		}
		case StabsTypeDescriptor::METHOD: {
			TypeName type_name;
			type_name.first_part = std::string("<err method>");
			return type_name;
		}
		case StabsTypeDescriptor::REFERENCE:
		case StabsTypeDescriptor::POINTER: {
			auto inner_type = find_type(type.reference_or_pointer.value_type.get(), types, type.type_number);
			TypeName name = resolve_c_type_name(types, inner_type);
			name.first_part += (s8) type.descriptor;
			return name;
		}
		case StabsTypeDescriptor::MEMBER: {
			TypeName type_name;
			type_name.first_part = std::string("<err member>");
			return type_name;
		}
		default:
			verify_not_reached("error: Unexpected type descriptor.\n");
	}
};

void print_symbol_as_c(FILE* output, const StabsSymbol& symbol, const std::map<s32, TypeName>& type_names) {
	switch(symbol.descriptor) {
		case StabsSymbolDescriptor::ENUM_STRUCT_OR_TYPE_TAG:
			fprintf(output, "typedef ");
			print_type_name(output, symbol.type, type_names, 0);
			fprintf(output, " %s;\n", symbol.name.c_str());
			break;
		default:
			fprintf(output, "// ?");
	}
}

static void print_type_name(FILE* output, const StabsType& type, const std::map<s32, TypeName>& type_names, s32 depth) {
	if(!type.has_body) {
		auto iter = type_names.find(type.type_number);
		if(iter == type_names.end()) {fprintf(stderr, "error: Undeclared type referenced: %d.\n", type.type_number);return;}
		indent(output, depth);
		fprintf(output, "%s", iter->second.first_part.c_str());
		return;
	}
	
	bool is_struct = false;
	switch(type.descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE: {
			assert(0);
			//auto actual_type = declared_types.find(type.type_number);
			//if(actual_type != declared_types.end()) {
			//	indent(output, depth);
			//	fprintf(output, "%s", actual_type->second.c_str());
			//} else {
			//	if(type.aux_type.get()) {
			//		print_type(output, *type.aux_type.get(), name, declared_types, depth);
			//		return;
			//	} else {
			//		indent(output, depth);
			//		fprintf(output, "<err type>");
			//	}
			//}
			//break;
		}
		case StabsTypeDescriptor::ARRAY: {
			assert(type.array_type.element_type.get());
			print_type_name(output, *type.array_type.element_type.get(), type_names, depth);
			return;
		}
		case StabsTypeDescriptor::ENUM: {
			indent(output, depth);
			fprintf(output, "enum {\n");
			for(auto& [value, name] : type.enum_type.fields) {
				bool is_last = value == type.enum_type.fields.back().first;
				indent(output, depth + 1);
				fprintf(output, "%s = %d%s\n", name.c_str(), value, is_last ? "" : ",");
			}
			indent(output, depth);
			fprintf(output, "}\n");
			break;
		}
		case StabsTypeDescriptor::FUNCTION:
			indent(output, depth);
			fprintf(output, "/* function ptr */ void");
			break;
		case StabsTypeDescriptor::STRUCT:
			is_struct = true;
		case StabsTypeDescriptor::UNION: {
			indent(output, depth);
			fprintf(output, "%s {\n", is_struct ? "struct" : "union");
			for(const StabsField& field : type.struct_or_union.fields) {
				print_type_name(output, field.type, type_names, depth + 1);
				fprintf(output, " %s", field.name.c_str());
				print_array_indices(output);
				fprintf(output, ";\n");
			}
			indent(output, depth);
			fprintf(output, "}");
			break;
		}
		case StabsTypeDescriptor::CROSS_REFERENCE: {
			fprintf(output, "%s", type.cross_reference.identifier.c_str());
			break;
		}
		case StabsTypeDescriptor::REFERENCE:
		case StabsTypeDescriptor::POINTER: {
			if(!type.reference_or_pointer.value_type.get()) {
				verify_not_reached("error: Invalid reference or pointer type.\n");
			}
			print_type_name(output, *type.reference_or_pointer.value_type.get(), type_names, depth);
			fprintf(output, "%c", (s8) type.descriptor);
			break;
		}
		default: {
			verify_not_reached("error: Unhandled type descriptor '%c' (%hhx).\n",
				(s8) type.descriptor, (s8) type.descriptor);
		}
	}
}

static void print_array_indices(FILE* output) {
	// TODO
}

static void indent(FILE* output, s32 depth) {
	for(s32 i = 0; i < depth; i++) {
		fprintf(output, "\t");
	}
}
