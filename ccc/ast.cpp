#include "ast.h"

namespace ccc::ast {

std::unique_ptr<Node> stabs_symbol_to_ast(const StabsSymbol& symbol, const std::map<s32, const StabsType*>& stabs_types) {
	if(!symbol.type.has_body) {
		auto node = std::make_unique<TypeName>();
		node->type_name = symbol.name;
		return node;
	}
	
	auto node = stabs_type_to_ast(symbol.type, stabs_types, 0, 0);
	node->name = symbol.name;
	return node;
}

std::unique_ptr<Node> stabs_type_to_ast(const StabsType& type, const std::map<s32, const StabsType*>& stabs_types, s32 absolute_parent_offset_bytes, s32 depth) {
	// This makes sure that if types are referenced by their number, their name
	// is shown instead their entire contents.
	if(depth > 0 && type.name.has_value()) {
		auto type_name = std::make_unique<ast::TypeName>();
		type_name->type_name = *type.name;
		return type_name;
	}
	
	if(!type.has_body) {
		auto stabs_type = stabs_types.find(type.type_number);
		if(type.anonymous || stabs_type == stabs_types.end() || !stabs_type->second) {
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->type_name = stringf("CCC_BADTYPELOOKUP(%d)", type.type_number);
			return type_name;
		}
		if(!stabs_type->second->has_body) {
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->type_name = stringf("CCC_BADRECURSION");
			return type_name;
		}
		return stabs_type_to_ast(*stabs_type->second, stabs_types, absolute_parent_offset_bytes, depth + 1);
	}
	
	std::unique_ptr<Node> result;
	
	switch(type.descriptor) {
		case StabsTypeDescriptor::TYPE_REFERENCE: {
			assert(type.type_reference.type.get());
			result = stabs_type_to_ast(*type.type_reference.type.get(), stabs_types, absolute_parent_offset_bytes, depth + 1);
			break;
		}
		case StabsTypeDescriptor::ARRAY: {
			auto array = std::make_unique<ast::Array>();
			assert(type.array_type.element_type.get());
			array->element_type = stabs_type_to_ast(*type.array_type.element_type.get(), stabs_types, absolute_parent_offset_bytes, depth + 1);
			const StabsType* index = type.array_type.index_type.get();
			verify(index && index->descriptor == StabsTypeDescriptor::RANGE && index->range_type.low == 0,
				"Invalid index type for array.");
			array->element_count = index->range_type.high + 1;
			result = std::move(array);
			break;
		}
		case StabsTypeDescriptor::ENUM: {
			auto inline_enum = std::make_unique<ast::InlineEnum>();
			inline_enum->constants = type.enum_type.fields;
			result = std::move(inline_enum);
			break;
		}
		case StabsTypeDescriptor::FUNCTION: {
			auto function = std::make_unique<ast::Function>();
			assert(type.function_type.type.get());
			function->return_type = stabs_type_to_ast(*type.function_type.type.get(), stabs_types, absolute_parent_offset_bytes, depth + 1);
			result = std::move(function);
			break;
		}
		case StabsTypeDescriptor::RANGE: {
			auto type_name = std::make_unique<ast::TypeName>();
			verify(type.name.has_value(), "Encountered RANGE type with no name.");
			type_name->type_name = *type.name;
			result = std::move(type_name);
			break;
		}
		case StabsTypeDescriptor::STRUCT: {
			auto inline_struct = std::make_unique<ast::InlineStruct>();
			for(const StabsBaseClass& stabs_base_class : type.struct_or_union.base_classes) {
				ast::BaseClass& ast_base_class = inline_struct->base_classes.emplace_back();
				ast_base_class.visibility = stabs_base_class.visibility;
				ast_base_class.offset = stabs_base_class.offset;
				auto base_class_type = stabs_type_to_ast(stabs_base_class.type, stabs_types, absolute_parent_offset_bytes, depth + 1);
				assert(base_class_type->descriptor == TYPE_NAME);
				ast_base_class.type_name = base_class_type->as<TypeName>().type_name;
			}
			for(const StabsField& field : type.struct_or_union.fields) {
				inline_struct->fields.emplace_back(stabs_field_to_ast(field, stabs_types, absolute_parent_offset_bytes, depth));
			}
			result = std::move(inline_struct);
			break;
		}
		case StabsTypeDescriptor::UNION: {
			auto inline_union = std::make_unique<ast::InlineUnion>();
			for(const StabsField& field : type.struct_or_union.fields) {
				inline_union->fields.emplace_back(stabs_field_to_ast(field, stabs_types, absolute_parent_offset_bytes, depth));
			}
			result = std::move(inline_union);
			break;
		}
		case StabsTypeDescriptor::CROSS_REFERENCE: {
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->type_name = type.cross_reference.identifier;
			result = std::move(type_name);
			break;
		}
		case StabsTypeDescriptor::METHOD: {
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->type_name = "METHOD";
			result = std::move(type_name);
			break;
		}
		case StabsTypeDescriptor::REFERENCE: {
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->type_name = "REFERENCE";
			result = std::move(type_name);
			break;
		}
		case StabsTypeDescriptor::POINTER: {
			auto pointer = std::make_unique<ast::Pointer>();
			assert(type.reference_or_pointer.value_type.get());
			pointer->value_type = stabs_type_to_ast(*type.reference_or_pointer.value_type.get(), stabs_types, absolute_parent_offset_bytes, depth + 1);
			result = std::move(pointer);
			break;
		}
		case StabsTypeDescriptor::SLASH: {
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->type_name = "SLASH";
			result = std::move(type_name);
			break;
		}
		case StabsTypeDescriptor::MEMBER: {
			auto type_name = std::make_unique<ast::TypeName>();
			type_name->type_name = "MEMBER";
			result = std::move(type_name);
			break;
		}
	}
	
	if(result == nullptr) {
		auto bad = std::make_unique<ast::TypeName>();
		bad->type_name = "CCC_BADTYPEINFO";
		return bad;
	}
	
	return result;
}

std::unique_ptr<Node> stabs_field_to_ast(const StabsField& field, const std::map<s32, const StabsType*>& stabs_types, s32 absolute_parent_offset_bytes, s32 depth) {
	// Bitfields
	if(field.offset_bits % 8 != 0 || field.size_bits % 8 != 0) {
		std::unique_ptr<BitField> bitfield = std::make_unique<BitField>();
		bitfield->name = field.name;
		bitfield->relative_offset_bytes = field.offset_bits / 8;
		bitfield->absolute_offset_bytes = absolute_parent_offset_bytes + bitfield->relative_offset_bytes;
		bitfield->size_bits = field.size_bits;
		bitfield->underlying_type = stabs_type_to_ast(field.type, stabs_types, bitfield->absolute_offset_bytes, depth + 1);
		bitfield->bitfield_offset_bits = field.offset_bits % 8;
		if(field.is_static) {
			bitfield->storage_class = ast::StorageClass::STATIC;
		}
		return bitfield;
	}
	
	// Normal fields
	s32 relative_offset_bytes = field.offset_bits / 8;
	s32 absolute_offset_bytes = absolute_parent_offset_bytes + relative_offset_bytes;
	std::unique_ptr<Node> child = stabs_type_to_ast(field.type, stabs_types, absolute_offset_bytes, depth + 1);
	child->name = field.name;
	child->relative_offset_bytes = relative_offset_bytes;
	child->absolute_offset_bytes = absolute_offset_bytes;
	child->size_bits = field.size_bits;
	if(field.is_static) {
		child->storage_class = ast::StorageClass::STATIC;
	}
	return child;
}

std::vector<Node> deduplicate_ast(const std::vector<std::pair<std::string, std::vector<Node>>>& per_file_ast) {
	return {};
}

}
