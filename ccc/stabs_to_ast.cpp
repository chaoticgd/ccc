#include "stabs_to_ast.h"

#define AST_DEBUG(...) //__VA_ARGS__
#define AST_DEBUG_PRINTF(...) AST_DEBUG(printf(__VA_ARGS__);)

namespace ccc {

static bool detect_bitfield(const StabsField& field, const StabsToAstState& state);

Result<std::unique_ptr<ast::Node>> stabs_data_type_symbol_to_ast(const ParsedSymbol& symbol, const StabsToAstState& state) {
	AST_DEBUG_PRINTF("ANALYSING %s\n", symbol.raw->string);
	auto node = stabs_type_to_ast_and_handle_errors(*symbol.name_colon_type.type.get(), state, 0, 0, false, false);
	node->name = (symbol.name_colon_type.name == " ") ? "" : symbol.name_colon_type.name;
	if(symbol.name_colon_type.descriptor == StabsSymbolDescriptor::TYPE_NAME) {
		node->storage_class = ast::SC_TYPEDEF;
	}
	return node;
}

std::unique_ptr<ast::Node> stabs_type_to_ast_and_handle_errors(const StabsType& type, const StabsToAstState& state, s32 abs_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute) {
	Result<std::unique_ptr<ast::Node>> node = stabs_type_to_ast(type, state, abs_parent_offset_bytes, depth, substitute_type_name, false);
	if(!node.success()) {
		auto error = std::make_unique<ast::TypeName>();
		error->source = ast::TypeNameSource::ERROR;
		error->type_name = std::string("/* ERROR: ") + node.error().message + " */";
		return std::unique_ptr<ast::Node>(std::move(error));
	}
	return std::move(*node);
}

Result<std::unique_ptr<ast::Node>> stabs_type_to_ast(const StabsType& type, const StabsToAstState& state, s32 abs_parent_offset_bytes, s32 depth, bool substitute_type_name, bool force_substitute) {
	AST_DEBUG_PRINTF("%-*stype desc=%hhx '%c' num=%d name=%s\n",
		depth * 4, "",
		(u8) type.descriptor,
		isprint((u8) type.descriptor) ? (u8) type.descriptor : '!',
		type.type_number,
		type.name.has_value() ? type.name->c_str() : "");
	
	CCC_CHECK(depth <= 200, "Call depth greater than 200 in stabs_type_to_ast, probably infinite recursion.")
	
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
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->source = ast::TypeNameSource::REFERENCE;
			type_name->type_name = *type.name;
			type_name->referenced_file_index = state.file_index;
			type_name->referenced_stabs_type_number = type.type_number;
			return std::unique_ptr<ast::Node>(std::move(type_name));
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
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->source = ast::TypeNameSource::REFERENCE;
			type_name->type_name = type_string;
			type_name->referenced_file_index = state.file_index;
			type_name->referenced_stabs_type_number = type.type_number;
			return std::unique_ptr<ast::Node>(std::move(type_name));
		}
	}
	
	if(!type.has_body) {
		// The definition of the type has been defined previously, so we have to
		// look it up by its type number.
		auto stabs_type = state.stabs_types->find(type.type_number);
		if(type.anonymous || stabs_type == state.stabs_types->end()) {
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->source = ast::TypeNameSource::ERROR;
			type_name->type_name += "CCC_BADTYPELOOKUP(";
			type_name->type_name += std::to_string(type.type_number.file);
			type_name->type_name += ",";
			type_name->type_name += std::to_string(type.type_number.type);
			type_name->type_name += ")";
			return std::unique_ptr<ast::Node>(std::move(type_name));
		}
		return stabs_type_to_ast(*stabs_type->second, state, abs_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
	}
	
	std::unique_ptr<ast::Node> result;
	
	switch(type.descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE: {
			const auto& stabs_type_ref = type.as<StabsTypeReferenceType>();
			if(type.anonymous || stabs_type_ref.type->anonymous || stabs_type_ref.type->type_number != type.type_number) {
				auto node = stabs_type_to_ast(*stabs_type_ref.type, state, abs_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
				CCC_RETURN_IF_ERROR(node);
				result = std::move(*node);
			} else {
				// I still don't know why in STABS void is a reference to
				// itself, maybe because I'm not a philosopher.
				auto type_name = std::make_unique<ast::TypeName>();
				type_name->source = ast::TypeNameSource::REFERENCE;
				type_name->type_name = "void";
				result = std::move(type_name);
			}
			break;
		}
		case StabsTypeDescriptor::ARRAY: {
			auto array = std::make_unique<ast::Array>();
			const auto& stabs_array = type.as<StabsArrayType>();
			
			auto element_node = stabs_type_to_ast(*stabs_array.element_type, state, abs_parent_offset_bytes, depth + 1, true, force_substitute);;
			CCC_RETURN_IF_ERROR(element_node);
			array->element_type = std::move(*element_node);
			
			const auto& index = stabs_array.index_type->as<StabsRangeType>();
			// The low and high values are not wrong in this case.
			CCC_CHECK(index.low_maybe_wrong == 0, "Invalid index type for array.");
			array->element_count = index.high_maybe_wrong + 1;
			result = std::move(array);
			break;
		}
		case StabsTypeDescriptor::ENUM: {
			auto inline_enum = std::make_unique<ast::Enum>();
			const auto& stabs_enum = type.as<StabsEnumType>();
			inline_enum->constants = stabs_enum.fields;
			result = std::move(inline_enum);
			break;
		}
		case StabsTypeDescriptor::FUNCTION: {
			auto function = std::make_unique<ast::FunctionType>();
			
			auto node = stabs_type_to_ast(*type.as<StabsFunctionType>().return_type, state, abs_parent_offset_bytes, depth + 1, true, force_substitute);
			CCC_RETURN_IF_ERROR(node);
			function->return_type = std::move(*node);
			
			result = std::move(function);
			break;
		}
		case StabsTypeDescriptor::VOLATILE_QUALIFIER: {
			const auto& volatile_qualifier = type.as<StabsVolatileQualifierType>();
			
			auto node = stabs_type_to_ast(*volatile_qualifier.type.get(), state, abs_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
			CCC_RETURN_IF_ERROR(node);
			result = std::move(*node);
			
			result->is_volatile = true;
			break;
		}
		case StabsTypeDescriptor::CONST_QUALIFIER: {
			const auto& const_qualifier = type.as<StabsConstQualifierType>();
			
			auto node = stabs_type_to_ast(*const_qualifier.type.get(), state, abs_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
			result = std::move(*node);
			
			result->is_const = true;
			break;
		}
		case StabsTypeDescriptor::RANGE: {
			auto builtin = std::make_unique<ast::BuiltIn>();
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
			auto struct_or_union = std::make_unique<ast::StructOrUnion>();
			struct_or_union->is_struct = type.descriptor == StabsTypeDescriptor::STRUCT;
			struct_or_union->size_bits = (s32) stabs_struct_or_union->size * 8;
			for(const StabsBaseClass& stabs_base_class : stabs_struct_or_union->base_classes) {
				auto base_class = stabs_type_to_ast(*stabs_base_class.type, state, abs_parent_offset_bytes, depth + 1, true, force_substitute);
				CCC_RETURN_IF_ERROR(base_class);
				
				(*base_class)->is_base_class = true;
				(*base_class)->absolute_offset_bytes = stabs_base_class.offset;
				(*base_class)->access_specifier = stabs_field_visibility_to_access_specifier(stabs_base_class.visibility);
				
				struct_or_union->base_classes.emplace_back(std::move(*base_class));
			}
			AST_DEBUG_PRINTF("%-*s beginfields\n", depth * 4, "");
			for(const StabsField& field : stabs_struct_or_union->fields) {
				auto node = stabs_field_to_ast(field, state, abs_parent_offset_bytes, depth);
				CCC_RETURN_IF_ERROR(node);
				struct_or_union->fields.emplace_back(std::move(*node));
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
					auto node = stabs_type_to_ast(*stabs_func.type, state, abs_parent_offset_bytes, depth + 1, true, true);
					CCC_RETURN_IF_ERROR(node);
					if(function_set.name == "__as") {
						(*node)->name = "operator=";
					} else {
						(*node)->name = function_set.name;
					}
					if((*node)->descriptor == ast::FUNCTION_TYPE) {
						ast::FunctionType& function = (*node)->as<ast::FunctionType>();
						function.modifier = stabs_func.modifier;
						function.is_constructor = false;
						if(type.name.has_value()) {
							function.is_constructor |= function_set.name == type.name;
							function.is_constructor |= function_set.name == struct_or_union_name_no_template_parameters;
						}
						function.vtable_index = stabs_func.vtable_index;
					}
					(*node)->access_specifier = stabs_field_visibility_to_access_specifier(stabs_func.visibility);
					struct_or_union->member_functions.emplace_back(std::move(*node));
				}
			}
			AST_DEBUG_PRINTF("%-*s endmemberfuncs\n", depth * 4, "");
			result = std::move(struct_or_union);
			break;
		}
		case StabsTypeDescriptor::CROSS_REFERENCE: {
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->source = ast::TypeNameSource::CROSS_REFERENCE;
			type_name->type_name = type.as<StabsCrossReferenceType>().identifier;
			result = std::move(type_name);
			break;
		}
		case ccc::StabsTypeDescriptor::FLOATING_POINT_BUILTIN: {
			const auto& fp_builtin = type.as<StabsFloatingPointBuiltInType>();
			auto builtin = std::make_unique<ast::BuiltIn>();
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
			auto function = std::make_unique<ast::FunctionType>();
			
			auto return_node = stabs_type_to_ast(*stabs_method.return_type.get(), state, abs_parent_offset_bytes, depth + 1, true, true);
			CCC_RETURN_IF_ERROR(return_node);
			function->return_type = std::move(*return_node);
			
			function->parameters.emplace();
			for(const std::unique_ptr<StabsType>& parameter_type : stabs_method.parameter_types) {
				auto parameter_node = stabs_type_to_ast(*parameter_type, state, abs_parent_offset_bytes, depth + 1, true, true);
				CCC_RETURN_IF_ERROR(parameter_node);
				function->parameters->emplace_back(std::move(*parameter_node));
			}
			result = std::move(function);
			break;
		}
		case StabsTypeDescriptor::POINTER: {
			auto pointer = std::make_unique<ast::PointerOrReference>();
			pointer->is_pointer = true;
			
			auto value_node = stabs_type_to_ast(*type.as<StabsPointerType>().value_type, state, abs_parent_offset_bytes, depth + 1, true, force_substitute);
			CCC_RETURN_IF_ERROR(value_node);
			pointer->value_type = std::move(*value_node);
			
			result = std::move(pointer);
			break;
		}
		case StabsTypeDescriptor::REFERENCE: {
			auto reference = std::make_unique<ast::PointerOrReference>();
			reference->is_pointer = false;
			
			auto value_node = stabs_type_to_ast(*type.as<StabsReferenceType>().value_type, state, abs_parent_offset_bytes, depth + 1, true, force_substitute);
			CCC_RETURN_IF_ERROR(value_node);
			reference->value_type = std::move(*value_node);
			
			result = std::move(reference);
			break;
		}
		case StabsTypeDescriptor::TYPE_ATTRIBUTE: {
			const auto& stabs_type_attribute = type.as<StabsSizeTypeAttributeType>();
			
			auto node = stabs_type_to_ast(*stabs_type_attribute.type, state, abs_parent_offset_bytes, depth + 1, substitute_type_name, force_substitute);
			CCC_RETURN_IF_ERROR(node);
			result = std::move(*node);
			
			result->size_bits = stabs_type_attribute.size_bits;
			break;
		}
		case StabsTypeDescriptor::POINTER_TO_NON_STATIC_MEMBER: {
			const auto& stabs_member_pointer = type.as<StabsPointerToNonStaticDataMember>();
			auto member_pointer = std::make_unique<ast::PointerToDataMember>();
			
			auto class_node = stabs_type_to_ast(*stabs_member_pointer.class_type.get(), state, abs_parent_offset_bytes, depth + 1, true, true);
			CCC_RETURN_IF_ERROR(class_node);
			member_pointer->class_type = std::move(*class_node);
			
			auto member_node = stabs_type_to_ast(*stabs_member_pointer.member_type.get(), state, abs_parent_offset_bytes, depth + 1, true, true);
			CCC_RETURN_IF_ERROR(member_node);
			member_pointer->member_type = std::move(*member_node);
			
			result = std::move(member_pointer);
			break;
		}
		case StabsTypeDescriptor::BUILTIN: {
			CCC_CHECK(type.as<StabsBuiltInType>().type_id == 16,
				"Unknown built-in type!");
			auto builtin = std::make_unique<ast::BuiltIn>();
			builtin->bclass = BuiltInClass::BOOL_8;
			result = std::move(builtin);
			break;
		}
	}
	CCC_ASSERT(result);
	return result;
}

Result<std::unique_ptr<ast::Node>> stabs_field_to_ast(const StabsField& field, const StabsToAstState& state, s32 abs_parent_offset_bytes, s32 depth) {
	AST_DEBUG_PRINTF("%-*s  field %s\n", depth * 4, "", field.name.c_str());
	
	if(detect_bitfield(field, state)) {
		// Process bitfields.
		s32 relative_offset_bytes = field.offset_bits / 8;
		s32 absolute_offset_bytes = abs_parent_offset_bytes + relative_offset_bytes;
		auto bitfield_node = stabs_type_to_ast(*field.type, state, absolute_offset_bytes, depth + 1, true, false);
		
		std::unique_ptr<ast::BitField> bitfield = std::make_unique<ast::BitField>();
		bitfield->name = (field.name == " ") ? "" : field.name;
		bitfield->relative_offset_bytes = relative_offset_bytes;
		bitfield->absolute_offset_bytes = absolute_offset_bytes;
		bitfield->size_bits = field.size_bits;
		bitfield->underlying_type = std::move(*bitfield_node);
		bitfield->bitfield_offset_bits = field.offset_bits % 8;
		if(field.is_static) {
			bitfield->storage_class = ast::SC_STATIC;
		}
		bitfield->access_specifier = stabs_field_visibility_to_access_specifier(field.visibility);
		return std::unique_ptr<ast::Node>(std::move(bitfield));
	}
	
	// Process a normal field.
	s32 relative_offset_bytes = field.offset_bits / 8;
	s32 absolute_offset_bytes = abs_parent_offset_bytes + relative_offset_bytes;
	Result<std::unique_ptr<ast::Node>> node = stabs_type_to_ast(*field.type, state, absolute_offset_bytes, depth + 1, true, false);
	CCC_RETURN_IF_ERROR(node);
	(*node)->name = (field.name == " ") ? "" : field.name;
	(*node)->relative_offset_bytes = relative_offset_bytes;
	(*node)->absolute_offset_bytes = absolute_offset_bytes;
	(*node)->size_bits = field.size_bits;
	if(field.is_static) {
		(*node)->storage_class = ast::SC_STATIC;
	}
	(*node)->access_specifier = stabs_field_visibility_to_access_specifier(field.visibility);
	return node;
}

static bool detect_bitfield(const StabsField& field, const StabsToAstState& state) {
	// Static fields can't be bitfields.
	if(field.is_static) {
		return false;
	}
	
	// Resolve type references.
	const StabsType* type = field.type.get();
	for(s32 i = 0; i < 50; i++) {
		if(!type->has_body) {
			if(type->anonymous) {
				return false;
			}
			auto next_type = state.stabs_types->find(type->type_number);
			if(next_type == state.stabs_types->end() || next_type->second == type) {
				return false;
			}
			type = next_type->second;
		} else if(type->descriptor == StabsTypeDescriptor::TYPE_REFERENCE) {
			type = type->as<StabsTypeReferenceType>().type.get();
		} else if(type->descriptor == StabsTypeDescriptor::CONST_QUALIFIER) {
			type = type->as<StabsConstQualifierType>().type.get();
		} else if(type->descriptor == StabsTypeDescriptor::VOLATILE_QUALIFIER) {
			type = type->as<StabsVolatileQualifierType>().type.get();
		} else {
			break;
		}
		
		// Prevent an infinite loop if there's a cycle (fatal frame).
		if(i == 49) {
			return false;
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
				underlying_type_size_bits = 32;
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
	
	if(underlying_type_size_bits == 0) {
		return false;
	}
	
	return field.size_bits != underlying_type_size_bits;
}

ast::AccessSpecifier stabs_field_visibility_to_access_specifier(StabsFieldVisibility visibility) {
	ast::AccessSpecifier access_specifier = ast::AS_PUBLIC;
	switch(visibility) {
		case ccc::StabsFieldVisibility::NONE: access_specifier = ast::AS_PUBLIC; break;
		case ccc::StabsFieldVisibility::PUBLIC: access_specifier = ast::AS_PUBLIC; break;
		case ccc::StabsFieldVisibility::PROTECTED: access_specifier = ast::AS_PROTECTED; break;
		case ccc::StabsFieldVisibility::PRIVATE: access_specifier = ast::AS_PRIVATE; break;
		case ccc::StabsFieldVisibility::PUBLIC_OPTIMIZED_OUT: access_specifier = ast::AS_PUBLIC; break;
	}
	return access_specifier;
}

}
