// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "data_refinement.h"

namespace ccc {

struct DataRefinementContext {
	const SymbolDatabase& database;
	ReadVirtualFunc read_virtual;
};

static Result<RefinedData> refine_node(
	u32 virtual_address, const ast::Node& type, const DataRefinementContext& context);
static Result<RefinedData> refine_builtin(
	u32 virtual_address, ast::BuiltInClass bclass, const DataRefinementContext& context);
static Result<RefinedData> refine_pointer_or_reference(
	u32 virtual_address, const ast::Node& type, const DataRefinementContext& context);
static const char* generate_format_string(s32 size, bool is_signed);
static std::string single_precision_float_to_string(float value);
static std::string string_format(const char* format, va_list args);
static std::string stringf(const char* format, ...);

bool can_refine_variable(const VariableToRefine& variable)
{
	if(!variable.storage) return false;
	if(variable.storage->location == GlobalStorageLocation::BSS) return false;
	if(variable.storage->location == GlobalStorageLocation::SBSS) return false;
	if(!variable.address.valid()) return false;
	if(!variable.type) return false;
	return true;
}

Result<RefinedData> refine_variable(
	const VariableToRefine& variable, const SymbolDatabase& database, const ReadVirtualFunc& read_virtual)
{
	CCC_ASSERT(variable.type);
	DataRefinementContext context{database, read_virtual};
	return refine_node(variable.address.value, *variable.type, context);
}

static Result<RefinedData> refine_node(
	u32 virtual_address, const ast::Node& type, const DataRefinementContext& context)
{
	switch(type.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = type.as<ast::Array>();
			CCC_CHECK(array.element_type->computed_size_bytes > -1, "Cannot compute element size for '%s' array.", array.name.c_str());
			RefinedData list;
			std::vector<RefinedData>& elements = list.value.emplace<std::vector<RefinedData>>();
			for(s32 i = 0; i < array.element_count; i++) {
				s32 offset = i * array.element_type->computed_size_bytes;
				Result<RefinedData> element = refine_node(virtual_address + offset, *array.element_type.get(), context);
				CCC_RETURN_IF_ERROR(element);
				element->field_name = "[" + std::to_string(i) + "]";
				elements.emplace_back(std::move(*element));
			}
			return list;
		}
		case ast::BITFIELD: {
			RefinedData data;
			data.value = "BITFIELD";
			return data;
		}
		case ast::BUILTIN: {
			const ast::BuiltIn& builtin = type.as<ast::BuiltIn>();
			return refine_builtin(virtual_address, builtin.bclass, context);
		}
		case ast::ENUM: {
			const ast::Enum& enumeration = type.as<ast::Enum>();
			RefinedData data;
			s32 value = 0;
			context.read_virtual((u8*) &value, virtual_address, 4);
			for(const auto& [number, name] : enumeration.constants) {
				if(number == value) {
					data.value = name;
					return data;
				}
			}
			data.value = std::to_string(value);
			return data;
		}
		case ast::FORWARD_DECLARED: {
			break;
		}
		case ast::FUNCTION: {
			break;
		}
		case ast::POINTER_OR_REFERENCE: {
			return refine_pointer_or_reference(virtual_address, type, context);
		}
		case ast::POINTER_TO_DATA_MEMBER: {
			return refine_builtin(virtual_address, ast::BuiltInClass::UNSIGNED_32, context);
		}
		case ast::STRUCT_OR_UNION: {
			const ast::StructOrUnion& struct_or_union = type.as<ast::StructOrUnion>();
			RefinedData list;
			std::vector<RefinedData>& children = list.value.emplace<std::vector<RefinedData>>();
			for(s32 i = 0; i < (s32) struct_or_union.base_classes.size(); i++) {
				const std::unique_ptr<ast::Node>& base_class = struct_or_union.base_classes[i];
				Result<RefinedData> child = refine_node(virtual_address + base_class->absolute_offset_bytes, *base_class.get(), context);
				CCC_RETURN_IF_ERROR(child);
				child->field_name = "base class " + std::to_string(i);
				children.emplace_back(std::move(*child));
			}
			for(const std::unique_ptr<ast::Node>& field : struct_or_union.fields) {
				if(field->storage_class == ast::SC_STATIC) {
					continue;
				}
				Result<RefinedData> child = refine_node(virtual_address + field->relative_offset_bytes, *field.get(), context);
				CCC_RETURN_IF_ERROR(child);
				child->field_name = "." + field->name;
				children.emplace_back(std::move(*child));
			}
			return list;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = type.as<ast::TypeName>();
			const DataType* resolved_type = context.database.data_types.symbol_from_handle(type_name.data_type_handle);
			if(resolved_type && resolved_type->type() && !resolved_type->type()->is_currently_processing) {
				resolved_type->type()->is_currently_processing = true;
				Result<RefinedData> child = refine_node(virtual_address, *resolved_type->type(), context);
				resolved_type->type()->is_currently_processing = false;
				return child;
			}
			RefinedData error;
			error.value = "CCC_TYPE_LOOKUP_FAILED";
			return error;
		}
	}
	
	return CCC_FAILURE("Failed to refine global variable (%s).", ast::node_type_to_string(type));
}

static Result<RefinedData> refine_builtin(
	u32 virtual_address, ast::BuiltInClass bclass, const DataRefinementContext& context)
{
	RefinedData data;
	
	switch(bclass) {
		case ast::BuiltInClass::VOID: {
			break;
		}
		case ast::BuiltInClass::UNSIGNED_8:
		case ast::BuiltInClass::UNQUALIFIED_8:
		case ast::BuiltInClass::UNSIGNED_16:
		case ast::BuiltInClass::UNSIGNED_32:
		case ast::BuiltInClass::UNSIGNED_64: {
			u64 value = 0;
			s32 size = builtin_class_size(bclass);
			Result<void> read_result = context.read_virtual((u8*) &value, virtual_address, size);
			CCC_RETURN_IF_ERROR(read_result);
			const char* format = generate_format_string(size, false);
			data.value = stringf(format, value);
			break;
		}
		case ast::BuiltInClass::SIGNED_8:
		case ast::BuiltInClass::SIGNED_16:
		case ast::BuiltInClass::SIGNED_32:
		case ast::BuiltInClass::SIGNED_64: {
			s64 value = 0;
			s32 size = builtin_class_size(bclass);
			Result<void> read_result = context.read_virtual((u8*) &value, virtual_address, size);
			CCC_RETURN_IF_ERROR(read_result);
			const char* format = generate_format_string(size, true);
			data.value = stringf(format, value);
			break;
		}
		case ast::BuiltInClass::BOOL_8: {
			bool value = false;
			Result<void> read_result = context.read_virtual((u8*) &value, virtual_address, 1);
			CCC_RETURN_IF_ERROR(read_result);
			data.value = value ? "true" : "false";
			break;
		}
		case ast::BuiltInClass::FLOAT_32: {
			float value = 0.f;
			static_assert(sizeof(value) == 4);
			Result<void> read_result = context.read_virtual((u8*) &value, virtual_address, 4);
			CCC_RETURN_IF_ERROR(read_result);
			data.value = single_precision_float_to_string(value);
			break;
		}
		case ast::BuiltInClass::FLOAT_64: {
			double value = 0.f;
			static_assert(sizeof(value) == 8);
			Result<void> read_result = context.read_virtual((u8*) &value, virtual_address, 8);
			CCC_RETURN_IF_ERROR(read_result);
			std::string string = stringf("%g", value);
			if(strtof(string.c_str(), nullptr) != value) {
				string = stringf("%.17g", value);
			}
			data.value = std::move(string);
			break;
		}
		case ast::BuiltInClass::UNSIGNED_128:
		case ast::BuiltInClass::SIGNED_128:
		case ast::BuiltInClass::UNQUALIFIED_128:
		case ast::BuiltInClass::FLOAT_128: {
			float value[4];
			Result<void> read_result = context.read_virtual((u8*) value, virtual_address, 16);
			CCC_RETURN_IF_ERROR(read_result);
			data.value = stringf("VECTOR(%s, %s, %s, %s)",
				single_precision_float_to_string(value[0]).c_str(),
				single_precision_float_to_string(value[1]).c_str(),
				single_precision_float_to_string(value[2]).c_str(),
				single_precision_float_to_string(value[3]).c_str());
			break;
		}
	}
	
	return data;
}

static Result<RefinedData> refine_pointer_or_reference(
	u32 virtual_address, const ast::Node& type, const DataRefinementContext& context)
{
	RefinedData data;
	u32 address = 0;
	Result<void> read_result = context.read_virtual((u8*) &address, virtual_address, 4);
	CCC_RETURN_IF_ERROR(read_result);
	
	std::string string;
	if(address != 0) {
		FunctionHandle function_handle = context.database.functions.first_handle_from_address(address);
		const Function* function_symbol = context.database.functions.symbol_from_handle(function_handle);
		
		GlobalVariableHandle global_variable_handle = context.database.global_variables.first_handle_from_address(address);
		const GlobalVariable* global_variable_symbol = context.database.global_variables.symbol_from_handle(global_variable_handle);
		
		if(function_symbol) {
			bool is_pointer = type.descriptor == ast::POINTER_OR_REFERENCE
				&& type.as<ast::PointerOrReference>().is_pointer;
			if(is_pointer) {
				string += "&";
			}
			string += function_symbol->name();
		} else if(global_variable_symbol) {
			bool is_pointer = type.descriptor == ast::POINTER_OR_REFERENCE
				&& type.as<ast::PointerOrReference>().is_pointer;
			bool pointing_at_array = global_variable_symbol->type()
				&& global_variable_symbol->type()->descriptor == ast::ARRAY;
			if(is_pointer && !pointing_at_array) {
				string += "&";
			}
			string += global_variable_symbol->name();
		} else {
			string = stringf("0x%x", address);
		}
	} else {
		string = "NULL";
	}
	data.value = std::move(string);
	
	return data;
}

static const char* generate_format_string(s32 size, bool is_signed)
{
	switch(size) {
		case 1: return is_signed ? "%hhd" : "%hhu";
		case 2: return is_signed ? "%hd" : "%hu";
		case 4: return is_signed ? "%d" : "%hu";
	}
	return is_signed ? ("%" PRId64) : ("%" PRIu64);
}

static std::string single_precision_float_to_string(float value)
{
	std::string result = stringf("%g", value);
	if(strtof(result.c_str(), nullptr) != value) {
		result = stringf("%.9g", value);
	}
	if(result.find(".") == std::string::npos) {
		result += ".";
	}
	result += "f";
	return result;
}

static std::string string_format(const char* format, va_list args)
{
	static char buffer[16 * 1024];
	vsnprintf(buffer, sizeof(buffer), format, args);
	return std::string(buffer);
}

static std::string stringf(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	std::string string = string_format(format, args);
	va_end(args);
	return string;
}

}
