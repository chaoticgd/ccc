#include "ast.h"

namespace ccc::ast {

#define AST_DEBUG(...) //__VA_ARGS__
#define AST_DEBUG_PRINTF(...) AST_DEBUG(printf(__VA_ARGS__);)

static bool detect_bitfield(const StabsField& field, const StabsToAstState& state);
static bool compare_nodes_and_merge(CompareResult& dest, const Node& node_lhs, const Node& node_rhs, const TypeLookupInfo& lookup);
static void try_to_match_wobbly_typedefs(CompareResult& result, const Node& node_lhs, const Node& node_rhs, const TypeLookupInfo& lookup);

std::unique_ptr<Node> stabs_symbol_to_ast(const ParsedSymbol& symbol, const StabsToAstState& state) {
	AST_DEBUG_PRINTF("ANALYSING %s\n", symbol.raw->string);
	auto node = stabs_type_to_ast_no_throw(*symbol.name_colon_type.type.get(), state, 0, 0, false, false);
	node->name = (symbol.name_colon_type.name == " ") ? "" : symbol.name_colon_type.name;
	node->symbol = &symbol;
	if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::TYPE_NAME) {
		node->storage_class = SC_TYPEDEF;
	}
	return node;
}

std::unique_ptr<Node> stabs_type_to_ast_no_throw(const StabsType& type, const StabsToAstState& state, s32 absolute_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute) {
	try {
		return stabs_type_to_ast(type, state, absolute_parent_offset_bytes, depth, substitute_type_name, false);
	} catch(std::runtime_error& e) {
		auto error = std::make_unique<TypeName>();
		error->source = TypeNameSource::ERROR;
		error->type_name = e.what();
		return error;
	}
}

std::unique_ptr<Node> stabs_type_to_ast(const StabsType& type, const StabsToAstState& state, s32 absolute_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute) {
	AST_DEBUG_PRINTF("%-*stype desc=%hhx '%c' num=%d name=%s\n",
		depth * 4, "",
		(u8) type.descriptor,
		isprint((u8) type.descriptor) ? (u8) type.descriptor : '!',
		type.type_number,
		type.name.has_value() ? type.name->c_str() : "");
	
	if(depth > 200) {
		throw std::runtime_error("CCC_BADRECURSION");
	}
	
	// This makes sure that types are replaced with their type name in cases
	// where that would be more appropriate.
	if(type.name.has_value()) {
		bool try_substitute = depth > 0 && (type.is_root
			|| type.descriptor == StabsTypeDescriptor::RANGE
			|| type.descriptor == StabsTypeDescriptor::BUILTIN);
		bool is_name_empty = type.name == "" || type.name == " ";
		// Unfortunately, a common case seems to be that __builtin_va_list is
		// indistinguishable from void*, so we prevent it from being output to
		// avoid confusion.
		bool is_va_list = type.name == "__builtin_va_list";
		if((substitute_type_name || try_substitute) && !is_name_empty && !is_va_list) {
			auto type_name = std::make_unique<TypeName>();
			type_name->source = TypeNameSource::REFERENCE;
			type_name->type_name = *type.name;
			type_name->referenced_file_index = state.file_index;
			type_name->referenced_stabs_type_number = type.type_number;
			return type_name;
		}
	}
	
	// This prevents infinite recursion when an automatically generated member
	// function references an unnamed type.
	if(force_substitute) {
		const char* type_string = nullptr;
		if(type.descriptor == StabsTypeDescriptor::ENUM) type_string = "__unnamed_enum";
		if(type.descriptor == StabsTypeDescriptor::STRUCT) type_string = "__unnamed_struct";
		if(type.descriptor == StabsTypeDescriptor::UNION) type_string = "__unnamed_union";
		if(type_string) {
			auto type_name = std::make_unique<TypeName>();
			type_name->source = TypeNameSource::REFERENCE;
			type_name->type_name = type_string;
			type_name->referenced_file_index = state.file_index;
			type_name->referenced_stabs_type_number = type.type_number;
			return type_name;
		}
	}
	
	if(!type.has_body) {
		// The definition of the type has been defined previously, so we have to
		// look it up by its type number.
		auto stabs_type = state.stabs_types->find(type.type_number);
		if(type.anonymous || stabs_type == state.stabs_types->end()) {
			auto type_name = std::make_unique<TypeName>();
			type_name->source = TypeNameSource::ERROR;
			type_name->type_name = stringf("CCC_BADTYPELOOKUP(%d)", type.type_number);
			return type_name;
		}
		return stabs_type_to_ast(*stabs_type->second, state, absolute_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
	}
	
	std::unique_ptr<Node> result;
	
	switch(type.descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE: {
			const auto& stabs_type_ref = type.as<StabsTypeReferenceType>();
			if(type.anonymous | stabs_type_ref.type->anonymous || stabs_type_ref.type->type_number != type.type_number) {
				result = stabs_type_to_ast(*stabs_type_ref.type, state, absolute_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
			} else {
				// I still don't know why in STABS void is a reference to
				// itself, maybe because I'm not a philosopher.
				auto type_name = std::make_unique<TypeName>();
				type_name->source = TypeNameSource::REFERENCE;
				type_name->type_name = "void";
				result = std::move(type_name);
			}
			break;
		}
		case StabsTypeDescriptor::ARRAY: {
			auto array = std::make_unique<Array>();
			const auto& stabs_array = type.as<StabsArrayType>();
			array->element_type = stabs_type_to_ast(*stabs_array.element_type, state, absolute_parent_offset_bytes, depth + 1, true, force_substitute);
			const auto& index = stabs_array.index_type->as<StabsRangeType>();
			// The low and high values are not wrong in this case.
			verify(index.low_maybe_wrong == 0, "Invalid index type for array.");
			array->element_count = index.high_maybe_wrong + 1;
			result = std::move(array);
			break;
		}
		case StabsTypeDescriptor::ENUM: {
			auto inline_enum = std::make_unique<InlineEnum>();
			const auto& stabs_enum = type.as<StabsEnumType>();
			inline_enum->constants = stabs_enum.fields;
			result = std::move(inline_enum);
			break;
		}
		case StabsTypeDescriptor::FUNCTION: {
			auto function = std::make_unique<FunctionType>();
			function->return_type = stabs_type_to_ast(*type.as<StabsFunctionType>().return_type, state, absolute_parent_offset_bytes, depth + 1, true, force_substitute);
			result = std::move(function);
			break;
		}
		case StabsTypeDescriptor::VOLATILE_QUALIFIER: {
			const auto& volatile_qualifier = type.as<StabsVolatileQualifierType>();
			result = stabs_type_to_ast(*volatile_qualifier.type.get(), state, absolute_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
			result->is_volatile = true;
			break;
		}
		case StabsTypeDescriptor::CONST_QUALIFIER: {
			const auto& const_qualifier = type.as<StabsConstQualifierType>();
			result = stabs_type_to_ast(*const_qualifier.type.get(), state, absolute_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
			result->is_const = true;
			break;
		}
		case StabsTypeDescriptor::RANGE: {
			auto builtin = std::make_unique<BuiltIn>();
			builtin->bclass = type.as<StabsRangeType>().range_class;
			result = std::move(builtin);
			break;
		}
		case StabsTypeDescriptor::STRUCT:
		case StabsTypeDescriptor::UNION: {
			const StabsStructOrUnionType* stabs_struct_or_union;
			if(type.descriptor == StabsTypeDescriptor::STRUCT) {
				stabs_struct_or_union = &type.as<StabsStructType>();
			} else {
				stabs_struct_or_union = &type.as<StabsUnionType>();
			}
			auto struct_or_union = std::make_unique<InlineStructOrUnion>();
			struct_or_union->is_struct = type.descriptor == StabsTypeDescriptor::STRUCT;
			struct_or_union->size_bits = (s32) stabs_struct_or_union->size * 8;
			for(const StabsBaseClass& stabs_base_class : stabs_struct_or_union->base_classes) {
				std::unique_ptr<Node> base_class = stabs_type_to_ast(*stabs_base_class.type, state, absolute_parent_offset_bytes, depth + 1, true, force_substitute);
				base_class->is_base_class = true;
				base_class->absolute_offset_bytes = stabs_base_class.offset;
				base_class->access_specifier = stabs_field_visibility_to_access_specifier(stabs_base_class.visibility);
				struct_or_union->base_classes.emplace_back(std::move(base_class));
			}
			AST_DEBUG_PRINTF("%-*s beginfields\n", depth * 4, "");
			for(const StabsField& field : stabs_struct_or_union->fields) {
				auto node = stabs_field_to_ast(field, state, absolute_parent_offset_bytes, depth);
				struct_or_union->fields.emplace_back(std::move(node));
			}
			AST_DEBUG_PRINTF("%-*s endfields\n", depth * 4, "");
			AST_DEBUG_PRINTF("%-*s beginmemberfuncs\n", depth * 4, "");
			std::string struct_or_union_name_no_template_parameters;
			if(type.name.has_value()) {
				struct_or_union_name_no_template_parameters =
					type.name->substr(0, type.name->find("<"));
			}
			for(const StabsMemberFunctionSet& function_set : stabs_struct_or_union->member_functions) {
				for(const StabsMemberFunction& stabs_func : function_set.overloads) {
					auto node = stabs_type_to_ast(*stabs_func.type, state, absolute_parent_offset_bytes, depth + 1, true, true);
					if(function_set.name == "__as") {
						node->name = "operator=";
					} else {
						node->name = function_set.name;
					}
					if(node->descriptor == FUNCTION_TYPE) {
						FunctionType& function = node->as<FunctionType>();
						function.modifier = stabs_func.modifier;
						function.is_constructor = false;
						if(type.name.has_value()) {
							function.is_constructor |= function_set.name == type.name;
							function.is_constructor |= function_set.name == struct_or_union_name_no_template_parameters;
						}
						function.vtable_index = stabs_func.vtable_index;
					}
					node->access_specifier = stabs_field_visibility_to_access_specifier(stabs_func.visibility);
					struct_or_union->member_functions.emplace_back(std::move(node));
				}
			}
			AST_DEBUG_PRINTF("%-*s endmemberfuncs\n", depth * 4, "");
			result = std::move(struct_or_union);
			break;
		}
		case StabsTypeDescriptor::CROSS_REFERENCE: {
			auto type_name = std::make_unique<TypeName>();
			type_name->source = TypeNameSource::CROSS_REFERENCE;
			type_name->type_name = type.as<StabsCrossReferenceType>().identifier;
			result = std::move(type_name);
			break;
		}
		case ccc::StabsTypeDescriptor::FLOATING_POINT_BUILTIN: {
			const auto& fp_builtin = type.as<StabsFloatingPointBuiltInType>();
			auto builtin = std::make_unique<BuiltIn>();
			switch(fp_builtin.bytes) {
				case 1: builtin->bclass = BuiltInClass::UNSIGNED_8; break;
				case 2: builtin->bclass = BuiltInClass::UNSIGNED_16; break;
				case 4: builtin->bclass = BuiltInClass::UNSIGNED_32; break;
				case 8: builtin->bclass = BuiltInClass::UNSIGNED_64; break;
				case 16: builtin->bclass = BuiltInClass::UNSIGNED_128; break;
				default: builtin->bclass = BuiltInClass::UNSIGNED_8; break;
			}
			result = std::move(builtin);
			break;
		}
		case StabsTypeDescriptor::METHOD: {
			const auto& stabs_method = type.as<StabsMethodType>();
			auto function = std::make_unique<FunctionType>();
			function->return_type = stabs_type_to_ast(*stabs_method.return_type.get(), state, absolute_parent_offset_bytes, depth + 1, true, true);
			function->parameters.emplace();
			for(const std::unique_ptr<StabsType>& parameter_type : stabs_method.parameter_types) {
				auto node = stabs_type_to_ast(*parameter_type, state, absolute_parent_offset_bytes, depth + 1, true, true);
				function->parameters->emplace_back(std::move(node));
			}
			result = std::move(function);
			break;
		}
		case StabsTypeDescriptor::POINTER: {
			auto pointer = std::make_unique<Pointer>();
			pointer->value_type = stabs_type_to_ast(*type.as<StabsPointerType>().value_type, state, absolute_parent_offset_bytes, depth + 1, true, force_substitute);
			result = std::move(pointer);
			break;
		}
		case StabsTypeDescriptor::REFERENCE: {
			auto reference = std::make_unique<Reference>();
			reference->value_type = stabs_type_to_ast(*type.as<StabsReferenceType>().value_type.get(), state, absolute_parent_offset_bytes, depth + 1, true, force_substitute);
			result = std::move(reference);
			break;
		}
		case StabsTypeDescriptor::TYPE_ATTRIBUTE: {
			const auto& stabs_type_attribute = type.as<StabsSizeTypeAttributeType>();
			result = stabs_type_to_ast(*stabs_type_attribute.type, state, absolute_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
			result->size_bits = stabs_type_attribute.size_bits;
			break;
		}
		case StabsTypeDescriptor::POINTER_TO_NON_STATIC_MEMBER: {
			const auto& stabs_member_pointer = type.as<StabsPointerToNonStaticDataMember>();
			auto member_pointer = std::make_unique<PointerToDataMember>();
			member_pointer->class_type = stabs_type_to_ast(*stabs_member_pointer.class_type.get(), state, absolute_parent_offset_bytes, depth + 1, true, true);
			member_pointer->member_type = stabs_type_to_ast(*stabs_member_pointer.member_type.get(), state, absolute_parent_offset_bytes, depth + 1, true, true);
			result = std::move(member_pointer);
			break;
		}
		case StabsTypeDescriptor::BUILTIN: {
			verify(type.as<StabsBuiltInType>().type_id == 16,
				"Unknown built-in type! Please file a bug report.");
			auto builtin = std::make_unique<BuiltIn>();
			builtin->bclass = BuiltInClass::BOOL_8;
			result = std::move(builtin);
			break;
		}
	}
	assert(result);
	return result;
}

std::unique_ptr<Node> stabs_field_to_ast(const StabsField& field, const StabsToAstState& state, s32 absolute_parent_offset_bytes, s32 depth) {
	AST_DEBUG_PRINTF("%-*s  field %s\n", depth * 4, "", field.name.c_str());
	
	if(detect_bitfield(field, state)) {
		// Process bitfields.
		std::unique_ptr<BitField> bitfield = std::make_unique<BitField>();
		bitfield->name = (field.name == " ") ? "" : field.name;
		bitfield->relative_offset_bytes = field.offset_bits / 8;
		bitfield->absolute_offset_bytes = absolute_parent_offset_bytes + bitfield->relative_offset_bytes;
		bitfield->size_bits = field.size_bits;
		bitfield->underlying_type = stabs_type_to_ast(*field.type, state, bitfield->absolute_offset_bytes, depth + 1, true, false);
		bitfield->bitfield_offset_bits = field.offset_bits % 8;
		if(field.is_static) {
			bitfield->storage_class = SC_STATIC;
		}
		bitfield->access_specifier = stabs_field_visibility_to_access_specifier(field.visibility);
		return bitfield;
	}
	
	// Process a normal field.
	s32 relative_offset_bytes = field.offset_bits / 8;
	s32 absolute_offset_bytes = absolute_parent_offset_bytes + relative_offset_bytes;
	std::unique_ptr<Node> child = stabs_type_to_ast(*field.type, state, absolute_offset_bytes, depth + 1, true, false);
	child->name = (field.name == " ") ? "" : field.name;
	child->relative_offset_bytes = relative_offset_bytes;
	child->absolute_offset_bytes = absolute_offset_bytes;
	child->size_bits = field.size_bits;
	if(field.is_static) {
		child->storage_class = SC_STATIC;
	}
	child->access_specifier = stabs_field_visibility_to_access_specifier(field.visibility);
	return child;
}

static bool detect_bitfield(const StabsField& field, const StabsToAstState& state) {
	// Static fields can't be bitfields.
	if(field.is_static) {
		return false;
	}
	
	// Resolve type references.
	const StabsType* type = field.type.get();
	while(!type->has_body || type->descriptor == StabsTypeDescriptor::CONST_QUALIFIER || type->descriptor == StabsTypeDescriptor::VOLATILE_QUALIFIER) {
		if(!type->has_body) {
			if(type->anonymous) {
				return false;
			}
			auto next_type = state.stabs_types->find(type->type_number);
			if(next_type == state.stabs_types->end() || next_type->second == type) {
				return false;
			}
			type = next_type->second;
		} else if(type->descriptor == StabsTypeDescriptor::CONST_QUALIFIER) {
			type = type->as<StabsConstQualifierType>().type.get();
		} else {
			type = type->as<StabsVolatileQualifierType>().type.get();
		}
	}
	
	// Determine the size of the underlying type.
	s32 underlying_type_size_bits = 0;
	switch(type->descriptor) {
		case ccc::StabsTypeDescriptor::RANGE: {
			underlying_type_size_bits = builtin_class_size(type->as<StabsRangeType>().range_class) * 8;
			break;
		}
		case ccc::StabsTypeDescriptor::CROSS_REFERENCE: {
			if(type->as<StabsCrossReferenceType>().type == StabsCrossReferenceType::ENUM) {
				underlying_type_size_bits = 4;
			} else {
				return false;
			}
			break;
		}
		case ccc::StabsTypeDescriptor::TYPE_ATTRIBUTE: {
			underlying_type_size_bits = type->as<StabsSizeTypeAttributeType>().size_bits;
			break;
		}
		case ccc::StabsTypeDescriptor::BUILTIN: {
			underlying_type_size_bits = 8; // bool
			break;
		}
		default: {
			return false;
		}
	}
	return field.size_bits != underlying_type_size_bits;
}

// Some enums have two symbols associated with them: One named " " and another
// one referencing the first.
void remove_duplicate_enums(std::vector<std::unique_ptr<Node>>& ast_nodes) {
	for(size_t i = 0; i < ast_nodes.size(); i++) {
		Node& node = *ast_nodes[i].get();
		if(node.descriptor == NodeDescriptor::INLINE_ENUM && node.name.empty()) {
			bool match = false;
			for(std::unique_ptr<Node>& other : ast_nodes) {
				bool is_match = other.get() != &node
					&& other->descriptor == NodeDescriptor::INLINE_ENUM
					&& !other->name.empty()
					&& other->as<InlineEnum>().constants == node.as<InlineEnum>().constants;
				if(is_match) {
					match = true;
					break;
				}
			}
			if(match) {
				ast_nodes.erase(ast_nodes.begin() + i);
				i--;
			}
		}
	}
}

void remove_duplicate_self_typedefs(std::vector<std::unique_ptr<Node>>& ast_nodes) {
	for(size_t i = 0; i < ast_nodes.size(); i++) {
		Node& node = *ast_nodes[i].get();
		if(node.descriptor == TYPE_NAME && node.as<TypeName>().type_name == node.name) {
			bool match = false;
			for(std::unique_ptr<Node>& other : ast_nodes) {
				bool is_match = other.get() != &node
					&& (other->descriptor == INLINE_ENUM
						|| other->descriptor == INLINE_STRUCT_OR_UNION)
					&& other->name == node.name;
				if(is_match) {
					match = true;
					break;
				}
			}
			if(match) {
				ast_nodes.erase(ast_nodes.begin() + i);
				i--;
			}
		}
	}
}

void TypeDeduplicatorOMatic::process_file(SourceFile& file, s32 file_index, const std::vector<std::unique_ptr<SourceFile>>& files) {
	for(std::unique_ptr<Node>& node : file.data_types) {
		auto existing_node_iterator = name_to_deduplicated_index.find(node->name);
		if(existing_node_iterator == name_to_deduplicated_index.end()) {
			// No types with this name have previously been processed.
			node->files = {file_index};
			name_to_deduplicated_index[node->name] = deduplicated_nodes_grouped_by_name.size();
			deduplicated_nodes_grouped_by_name.emplace_back().emplace_back((s32) flat_nodes.size());
			if(node->stabs_type_number > -1) {
				file.stabs_type_number_to_deduplicated_type_index[node->stabs_type_number] = (s32) flat_nodes.size();
			}
			flat_nodes.emplace_back(std::move(node));
		} else {
			// Types with this name have previously been processed, we need
			// to figure out if this one matches any of the previous ones.
			std::vector<s32>& nodes_with_the_same_name = deduplicated_nodes_grouped_by_name[existing_node_iterator->second];
			bool match = false;
			for(s32 existing_node_index : nodes_with_the_same_name) {
				std::unique_ptr<Node>& existing_node = flat_nodes[existing_node_index];
				assert(existing_node.get());
				assert(node.get());
				TypeLookupInfo lookup;
				lookup.files = &files;
				lookup.nodes = &flat_nodes;
				CompareResult compare_result = compare_nodes(*existing_node.get(), *node.get(), lookup, true);
				if(compare_result.type == CompareResultType::DIFFERS) {
					// The new node doesn't match this existing node.
					bool is_anonymous_enum = existing_node->descriptor == INLINE_ENUM
						&& existing_node->name.empty();
					if(!is_anonymous_enum) {
						existing_node->compare_fail_reason = compare_fail_reason_to_string(compare_result.fail_reason);
						node->compare_fail_reason = compare_fail_reason_to_string(compare_result.fail_reason);
					}
				} else {
					// The new node matches this existing node.
					existing_node->files.emplace_back(file_index);
					if(node->stabs_type_number > -1) {
						file.stabs_type_number_to_deduplicated_type_index[node->stabs_type_number] = existing_node_index;
					}
					if(compare_result.type == CompareResultType::MATCHES_FAVOUR_RHS) {
						// The new node matches the old one, but the new one
						// is slightly better, so we swap them.
						existing_node.swap(node);
						std::swap(node->files, existing_node->files);
						std::swap(node->compare_fail_reason, existing_node->compare_fail_reason);
					}
					match = true;
					break;
				}
			}
			if(!match) {
				// This type doesn't match the others with the same name
				// that have already been processed.
				node->files = {file_index};
				nodes_with_the_same_name.emplace_back((s32) flat_nodes.size());
				if(node->stabs_type_number > -1) {
					file.stabs_type_number_to_deduplicated_type_index[node->stabs_type_number] = (s32) flat_nodes.size();
				}
				flat_nodes.emplace_back(std::move(node));
			}
		}
	}
	
	// Free all the nodes that are no longer needed.
	file.data_types.clear();
}

std::vector<std::unique_ptr<Node>> TypeDeduplicatorOMatic::finish() {
	for(std::vector<s32>& node_group : deduplicated_nodes_grouped_by_name) {
		if(node_group.size() > 1) {
			for(s32 index : node_group) {
				flat_nodes[index]->conflict = true;
			}
		}
	}
	
	return std::move(flat_nodes);
}

CompareResult compare_nodes(const Node& node_lhs, const Node& node_rhs, const TypeLookupInfo& lookup, bool check_intrusive_fields) {
	CompareResult result = CompareResultType::MATCHES_NO_SWAP;
	if(node_lhs.descriptor != node_rhs.descriptor) return CompareFailReason::DESCRIPTOR;
	if(check_intrusive_fields) {
		if(node_lhs.storage_class != node_rhs.storage_class) return CompareFailReason::STORAGE_CLASS;
		if(node_lhs.name != node_rhs.name) return CompareFailReason::NAME;
		if(node_lhs.relative_offset_bytes != node_rhs.relative_offset_bytes) return CompareFailReason::RELATIVE_OFFSET_BYTES;
		if(node_lhs.absolute_offset_bytes != node_rhs.absolute_offset_bytes) return CompareFailReason::ABSOLUTE_OFFSET_BYTES;
		if(node_lhs.size_bits != node_rhs.size_bits) return CompareFailReason::SIZE_BITS;
		if(node_lhs.is_const != node_rhs.is_const) return CompareFailReason::CONSTNESS;
	}
	// We intentionally don't compare files, conflict symbol or compare_fail_reason here.
	switch(node_lhs.descriptor) {
		case ARRAY: {
			const auto [lhs, rhs] = Node::as<Array>(node_lhs, node_rhs);
			if(compare_nodes_and_merge(result, *lhs.element_type.get(), *rhs.element_type.get(), lookup)) return result;
			if(lhs.element_count != rhs.element_count) return CompareFailReason::ARRAY_ELEMENT_COUNT;
			break;
		}
		case BITFIELD: {
			const auto [lhs, rhs] = Node::as<BitField>(node_lhs, node_rhs);
			if(lhs.bitfield_offset_bits != rhs.bitfield_offset_bits) return CompareFailReason::BITFIELD_OFFSET_BITS;
			if(compare_nodes_and_merge(result, *lhs.underlying_type.get(), *rhs.underlying_type.get(), lookup)) return result;
			break;
		}
		case BUILTIN: {
			const auto [lhs, rhs] = Node::as<BuiltIn>(node_lhs, node_rhs);
			if(lhs.bclass != rhs.bclass) return CompareFailReason::BUILTIN_CLASS;
			break;
		}
		case FUNCTION_DEFINITION: {
			verify_not_reached("Tried to compare function definition AST nodes.");
		}
		case FUNCTION_TYPE: {
			const auto [lhs, rhs] = Node::as<FunctionType>(node_lhs, node_rhs);
			if(lhs.return_type.has_value() != rhs.return_type.has_value()) return CompareFailReason::FUNCTION_RETURN_TYPE_HAS_VALUE;
			if(lhs.return_type.has_value()) {
				if(compare_nodes_and_merge(result, *lhs.return_type->get(), *rhs.return_type->get(), lookup)) return result;
			}
			if(lhs.parameters.has_value() && rhs.parameters.has_value()) {
				if(lhs.parameters->size() != rhs.parameters->size()) return CompareFailReason::FUNCTION_PARAMAETER_COUNT;
				for(size_t i = 0; i < lhs.parameters->size(); i++) {
					if(compare_nodes_and_merge(result, *(*lhs.parameters)[i].get(), *(*rhs.parameters)[i].get(), lookup)) return result;
				}
			} else if(lhs.parameters.has_value() != rhs.parameters.has_value()) {
				return CompareFailReason::FUNCTION_PARAMETERS_HAS_VALUE;
			}
			if(lhs.modifier != rhs.modifier) return CompareFailReason::FUNCTION_MODIFIER;
			if(lhs.is_constructor != rhs.is_constructor) return CompareFailReason::FUNCTION_IS_CONSTRUCTOR;
			break;
		}
		case INITIALIZER_LIST: {
			verify_not_reached("Tried to compare initializer list AST nodes.");
			break;
		}
		case INLINE_ENUM: {
			const auto [lhs, rhs] = Node::as<InlineEnum>(node_lhs, node_rhs);
			if(lhs.constants != rhs.constants) return CompareFailReason::ENUM_CONSTANTS;
			break;
		}
		case INLINE_STRUCT_OR_UNION: {
			const auto [lhs, rhs] = Node::as<InlineStructOrUnion>(node_lhs, node_rhs);
			if(lhs.base_classes.size() != rhs.base_classes.size()) return CompareFailReason::BASE_CLASS_COUNT;
			for(size_t i = 0; i < lhs.base_classes.size(); i++) {
				if(compare_nodes_and_merge(result, *lhs.base_classes[i].get(), *rhs.base_classes[i].get(), lookup)) return result;
			}
			if(lhs.fields.size() != rhs.fields.size()) return CompareFailReason::FIELDS_SIZE;
			for(size_t i = 0; i < lhs.fields.size(); i++) {
				if(compare_nodes_and_merge(result, *lhs.fields[i].get(), *rhs.fields[i].get(), lookup)) return result;
			}
			if(lhs.member_functions.size() != rhs.member_functions.size()) return CompareFailReason::MEMBER_FUNCTION_COUNT;
			for(size_t i = 0; i < lhs.member_functions.size(); i++) {
				if(compare_nodes_and_merge(result, *lhs.member_functions[i].get(), *rhs.member_functions[i].get(), lookup)) return result;
			}
			break;
		}
		case LITERAL: {
			verify_not_reached("Tried to compare literal AST nodes.");
			break;
		}
		case POINTER: {
			const auto [lhs, rhs] = Node::as<Pointer>(node_lhs, node_rhs);
			if(compare_nodes_and_merge(result, *lhs.value_type.get(), *rhs.value_type.get(), lookup)) return result;
			break;
		}
		case POINTER_TO_DATA_MEMBER: {
			const auto [lhs, rhs] = Node::as<PointerToDataMember>(node_lhs, node_rhs);
			if(compare_nodes_and_merge(result, *lhs.class_type.get(), *rhs.class_type.get(), lookup)) return result;
			if(compare_nodes_and_merge(result, *lhs.member_type.get(), *rhs.member_type.get(), lookup)) return result;
			break;
		}
		case REFERENCE: {
			const auto [lhs, rhs] = Node::as<Reference>(node_lhs, node_rhs);
			if(compare_nodes_and_merge(result, *lhs.value_type.get(), *rhs.value_type.get(), lookup)) return result;
			break;
		}
		case SOURCE_FILE: {
			verify_not_reached("Tried to compare source file AST nodes.");
		}
		case TYPE_NAME: {
			const auto [lhs, rhs] = Node::as<TypeName>(node_lhs, node_rhs);
			// Don't check the source so that REFERENCE and CROSS_REFERENCE are
			// treated as the same.
			if(lhs.type_name != rhs.type_name) return CompareFailReason::TYPE_NAME;
			// The whole point of comparing nodes is to merge matching nodes
			// from different translation units, so we don't check the file
			// index or the STABS type number, since those vary between
			// different files.
			break;
		}
		case VARIABLE: {
			const auto [lhs, rhs] = Node::as<Variable>(node_lhs, node_rhs);
			if(lhs.variable_class != rhs.variable_class) return CompareFailReason::VARIABLE_CLASS;
			if(lhs.storage != rhs.storage) return CompareFailReason::VARIABLE_STORAGE;
			if(lhs.block != rhs.block) return CompareFailReason::VARIABLE_BLOCK;
			if(compare_nodes_and_merge(result, *lhs.type.get(), *rhs.type.get(), lookup)) return result;
			break;
		}
	}
	return result;
}

static bool compare_nodes_and_merge(CompareResult& dest, const Node& node_lhs, const Node& node_rhs, const TypeLookupInfo& lookup) {
	CompareResult result = compare_nodes(node_lhs, node_rhs, lookup, true);
	try_to_match_wobbly_typedefs(result, node_lhs, node_rhs, lookup);
	if(dest.type != result.type) {
		if(dest.type == CompareResultType::DIFFERS || result.type == CompareResultType::DIFFERS) {
			// If any of the inner types differ, the outer type does too.
			dest.type = CompareResultType::DIFFERS;
		} else if(dest.type == CompareResultType::MATCHES_CONFUSED || result.type == CompareResultType::MATCHES_CONFUSED) {
			// Propagate confusion.
			dest.type = CompareResultType::MATCHES_CONFUSED; 
		} else if(dest.type == CompareResultType::MATCHES_FAVOUR_LHS && result.type == CompareResultType::MATCHES_FAVOUR_RHS) {
			// Propagate confusion.
			dest.type = CompareResultType::MATCHES_CONFUSED; 
		} else if(dest.type == CompareResultType::MATCHES_FAVOUR_RHS && result.type == CompareResultType::MATCHES_FAVOUR_LHS) {
			// One of the results favours the LHS node and the other favours the
			// RHS node so we are confused.
			dest.type = CompareResultType::MATCHES_CONFUSED; 
		} else if(dest.type == CompareResultType::MATCHES_FAVOUR_LHS || result.type == CompareResultType::MATCHES_FAVOUR_LHS) {
			// One of the results favours the LHS node and the other is neutral
			// so go with the LHS node.
			dest.type = CompareResultType::MATCHES_FAVOUR_LHS;
		} else if(dest.type == CompareResultType::MATCHES_FAVOUR_RHS || result.type == CompareResultType::MATCHES_FAVOUR_RHS) {
			// One of the results favours the RHS node and the other is neutral
			// so go with the RHS node.
			dest.type = CompareResultType::MATCHES_FAVOUR_RHS;
		}
	}
	if(dest.fail_reason == CompareFailReason::NONE) {
		dest.fail_reason = result.fail_reason;
	}
	return dest.type == CompareResultType::DIFFERS;
}

static void try_to_match_wobbly_typedefs(CompareResult& result, const Node& node_lhs, const Node& node_rhs, const TypeLookupInfo& lookup) {
	// Detect if one side has a typedef when the other just has the plain type.
	// This was previously a common reason why type deduplication would fail.
	const Node* type_name_node = &node_lhs;
	const Node* raw_node = &node_rhs;
	for(s32 i = 0; result.type == CompareResultType::DIFFERS && i < 2; i++) {
		if(type_name_node->descriptor == TYPE_NAME) {
			const TypeName& type_name = type_name_node->as<TypeName>();
			if(type_name.referenced_file_index > -1 && type_name.referenced_stabs_type_number > -1) {
				const std::unique_ptr<SourceFile>& file = lookup.files->at(type_name.referenced_file_index);
				auto index = file->stabs_type_number_to_deduplicated_type_index.find(type_name.referenced_stabs_type_number);
				if(index != file->stabs_type_number_to_deduplicated_type_index.end()) {
					const Node& referenced_type = *lookup.nodes->at(index->second);
					// Don't compare 'intrusive' fields e.g. the offset.
					CompareResult new_result = compare_nodes(referenced_type, *raw_node, lookup, false);
					if(new_result.type != CompareResultType::DIFFERS) {
						result.type = (i == 0)
							? CompareResultType::MATCHES_FAVOUR_LHS
							: CompareResultType::MATCHES_FAVOUR_RHS;
					}
				}
			}
		}
		std::swap(type_name_node, raw_node);
	}
}

const char* compare_fail_reason_to_string(CompareFailReason reason) {
	switch(reason) {
		case CompareFailReason::NONE: return "error";
		case CompareFailReason::DESCRIPTOR: return "descriptor";
		case CompareFailReason::STORAGE_CLASS: return "storage classe";
		case CompareFailReason::NAME: return "name";
		case CompareFailReason::RELATIVE_OFFSET_BYTES: return "relative offset";
		case CompareFailReason::ABSOLUTE_OFFSET_BYTES: return "absolute offset";
		case CompareFailReason::BITFIELD_OFFSET_BITS: return "bitfield offset";
		case CompareFailReason::SIZE_BITS: return "size";
		case CompareFailReason::CONSTNESS: return "constness";
		case CompareFailReason::ARRAY_ELEMENT_COUNT: return "array element count";
		case CompareFailReason::BUILTIN_CLASS: return "builtin class";
		case CompareFailReason::FUNCTION_RETURN_TYPE_HAS_VALUE: return "function return type has value";
		case CompareFailReason::FUNCTION_PARAMAETER_COUNT: return "function paramaeter count";
		case CompareFailReason::FUNCTION_PARAMETERS_HAS_VALUE: return "function parameter";
		case CompareFailReason::FUNCTION_MODIFIER: return "function modifier";
		case CompareFailReason::FUNCTION_IS_CONSTRUCTOR: return "function is constructor";
		case CompareFailReason::ENUM_CONSTANTS: return "enum constant";
		case CompareFailReason::BASE_CLASS_COUNT: return "base class count";
		case CompareFailReason::FIELDS_SIZE: return "fields size";
		case CompareFailReason::MEMBER_FUNCTION_COUNT: return "member function count";
		case CompareFailReason::VTABLE_GLOBAL: return "vtable global";
		case CompareFailReason::TYPE_NAME: return "type name";
		case CompareFailReason::VARIABLE_CLASS: return "variable class";
		case CompareFailReason::VARIABLE_TYPE: return "variable type";
		case CompareFailReason::VARIABLE_STORAGE: return "variable storage";
		case CompareFailReason::VARIABLE_BLOCK: return "variable block";
	}
	return "";
}

const char* node_type_to_string(const Node& node) {
	switch(node.descriptor) {
		case ARRAY: return "array";
		case BITFIELD: return "bitfield";
		case BUILTIN: return "builtin";
		case FUNCTION_DEFINITION: return "function_definition";
		case FUNCTION_TYPE: return "function_type";
		case INITIALIZER_LIST: return "initializer_list";
		case INLINE_ENUM: return "enum";
		case INLINE_STRUCT_OR_UNION: {
			const InlineStructOrUnion& struct_or_union = node.as<InlineStructOrUnion>();
			if(struct_or_union.is_struct) {
				return "struct";
			} else {
				return "union";
			}
		}
		case LITERAL: return "literal";
		case POINTER: return "pointer";
		case POINTER_TO_DATA_MEMBER: return "pointer_to_data_member";
		case REFERENCE: return "reference";
		case SOURCE_FILE: return "source_file";
		case TYPE_NAME: return "type_name";
		case VARIABLE: return "variable";
	}
	return "CCC_BAD_NODE_DESCRIPTOR";
}

const char* storage_class_to_string(StorageClass storage_class) {
	switch(storage_class) {
		case SC_NONE: return "none";
		case SC_TYPEDEF: return "typedef";
		case SC_EXTERN: return "extern";
		case SC_STATIC: return "static";
		case SC_AUTO: return "auto";
		case SC_REGISTER: return "register";
	}
	return "";
}

const char* global_variable_location_to_string(GlobalVariableLocation location) {
	switch(location) {
		case GlobalVariableLocation::NIL: return "nil";
		case GlobalVariableLocation::DATA: return "data";
		case GlobalVariableLocation::BSS: return "bss";
		case GlobalVariableLocation::ABS: return "abs";
		case GlobalVariableLocation::SDATA: return "sdata";
		case GlobalVariableLocation::SBSS: return "sbss";
		case GlobalVariableLocation::RDATA: return "rdata";
		case GlobalVariableLocation::COMMON: return "common";
		case GlobalVariableLocation::SCOMMON: return "scommon";
	}
	return "";
}

const char* access_specifier_to_string(AccessSpecifier specifier) {
	switch(specifier) {
		case AS_PUBLIC: return "public";
		case AS_PROTECTED: return "protected";
		case AS_PRIVATE: return "private";
	}
	return "";
}

AccessSpecifier stabs_field_visibility_to_access_specifier(StabsFieldVisibility visibility) {
	AccessSpecifier access_specifier = AS_PUBLIC;
	switch(visibility) {
		case ccc::StabsFieldVisibility::NONE: access_specifier = AS_PUBLIC; break;
		case ccc::StabsFieldVisibility::PUBLIC: access_specifier = AS_PUBLIC; break;
		case ccc::StabsFieldVisibility::PROTECTED: access_specifier = AS_PROTECTED; break;
		case ccc::StabsFieldVisibility::PRIVATE: access_specifier = AS_PRIVATE; break;
		case ccc::StabsFieldVisibility::PUBLIC_OPTIMIZED_OUT: access_specifier = AS_PUBLIC; break;
	}
	return access_specifier;
}

}
