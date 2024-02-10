// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "data_refinement.h"

#include <cinttypes>

#include "ast.h"

namespace ccc {

static Result<RefinedData> refine_node(
	u32 virtual_address, const ast::Node& type, const SymbolDatabase& database, const ElfFile& elf);
static Result<RefinedData> refine_builtin(
	u32 virtual_address, ast::BuiltInClass bclass, const SymbolDatabase& database, const ElfFile& elf);
static Result<RefinedData> refine_pointer_or_reference(
	u32 virtual_address, const ast::Node& type, const SymbolDatabase& database, const ElfFile& elf);
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
	const VariableToRefine& variable, const SymbolDatabase& database, const ElfFile& elf)
{
	CCC_ASSERT(variable.type);
	return refine_node(variable.address.value, *variable.type, database, elf);
}

static Result<RefinedData> refine_node(
	u32 virtual_address, const ast::Node& type, const SymbolDatabase& database, const ElfFile& elf)
{
	switch(type.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = type.as<ast::Array>();
			CCC_CHECK(array.element_type->computed_size_bytes > -1, "Cannot compute element size for '%s' array.", array.name.c_str());
			RefinedData list;
			std::vector<RefinedData>& elements = list.value.emplace<std::vector<RefinedData>>();
			for(s32 i = 0; i < array.element_count; i++) {
				s32 offset = i * array.element_type->computed_size_bytes;
				Result<RefinedData> element = refine_node(virtual_address + offset, *array.element_type.get(), database, elf);
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
			return refine_builtin(virtual_address, builtin.bclass, database, elf);
		}
		case ast::ENUM: {
			const ast::Enum& enumeration = type.as<ast::Enum>();
			
			s32 value = 0;
			Result<void> read_result = elf.copy_virtual((u8*) &value, virtual_address, 4);
			CCC_RETURN_IF_ERROR(read_result);
			
			RefinedData data;
			for(const auto& [number, name] : enumeration.constants) {
				if(number == value) {
					data.value = name;
					return data;
				}
			}
			data.value = std::to_string(value);
			
			return data;
		}
		case ast::ERROR_NODE: {
			break;
		}
		case ast::FUNCTION: {
			break;
		}
		case ast::POINTER_OR_REFERENCE: {
			return refine_pointer_or_reference(virtual_address, type, database, elf);
		}
		case ast::POINTER_TO_DATA_MEMBER: {
			return refine_builtin(virtual_address, ast::BuiltInClass::UNSIGNED_32, database, elf);
		}
		case ast::STRUCT_OR_UNION: {
			const ast::StructOrUnion& struct_or_union = type.as<ast::StructOrUnion>();
			RefinedData list;
			std::vector<RefinedData>& children = list.value.emplace<std::vector<RefinedData>>();
			
			for(s32 i = 0; i < (s32) struct_or_union.base_classes.size(); i++) {
				const std::unique_ptr<ast::Node>& base_class = struct_or_union.base_classes[i];
				Result<RefinedData> child = refine_node(virtual_address + base_class->offset_bytes, *base_class.get(), database, elf);
				CCC_RETURN_IF_ERROR(child);
				child->field_name = "base class " + std::to_string(i);
				children.emplace_back(std::move(*child));
			}
			
			for(const std::unique_ptr<ast::Node>& field : struct_or_union.fields) {
				if(field->storage_class == STORAGE_CLASS_STATIC) {
					continue;
				}
				Result<RefinedData> child = refine_node(virtual_address + field->offset_bytes, *field.get(), database, elf);
				CCC_RETURN_IF_ERROR(child);
				child->field_name = "." + field->name;
				children.emplace_back(std::move(*child));
			}
			
			return list;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = type.as<ast::TypeName>();
			const DataType* resolved_type = database.data_types.symbol_from_handle(type_name.data_type_handle);
			if(resolved_type && resolved_type->type() && !resolved_type->type()->is_currently_processing) {
				resolved_type->type()->is_currently_processing = true;
				Result<RefinedData> child = refine_node(virtual_address, *resolved_type->type(), database, elf);
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
	u32 virtual_address, ast::BuiltInClass bclass, const SymbolDatabase& database, const ElfFile& elf)
{
	RefinedData data;
	
	switch(bclass) {
		case ast::BuiltInClass::VOID_TYPE: {
			break;
		}
		case ast::BuiltInClass::UNSIGNED_8:
		case ast::BuiltInClass::UNQUALIFIED_8:
		case ast::BuiltInClass::UNSIGNED_16:
		case ast::BuiltInClass::UNSIGNED_32:
		case ast::BuiltInClass::UNSIGNED_64: {
			s32 size = builtin_class_size(bclass);
			
			u64 value = 0;
			Result<void> read_result = elf.copy_virtual((u8*) &value, virtual_address, size);
			CCC_RETURN_IF_ERROR(read_result);
			
			const char* format = generate_format_string(size, false);
			data.value = stringf(format, value);
			
			break;
		}
		case ast::BuiltInClass::SIGNED_8:
		case ast::BuiltInClass::SIGNED_16:
		case ast::BuiltInClass::SIGNED_32:
		case ast::BuiltInClass::SIGNED_64: {
			s32 size = builtin_class_size(bclass);
			
			s64 value = 0;
			Result<void> read_result = elf.copy_virtual((u8*) &value, virtual_address, size);
			CCC_RETURN_IF_ERROR(read_result);
			
			const char* format = generate_format_string(size, true);
			data.value = stringf(format, value);
			
			break;
		}
		case ast::BuiltInClass::BOOL_8: {
			Result<u8> value = elf.get_object_virtual<u8>(virtual_address);
			CCC_RETURN_IF_ERROR(value);
			
			data.value = *value ? "true" : "false";
			
			break;
		}
		case ast::BuiltInClass::FLOAT_32: {
			static_assert(sizeof(float) == 4);
			
			Result<float> value = elf.get_object_virtual<float>(virtual_address);
			CCC_RETURN_IF_ERROR(value);
			
			data.value = single_precision_float_to_string(*value);
			
			break;
		}
		case ast::BuiltInClass::FLOAT_64: {
			static_assert(sizeof(double) == 8);
			
			Result<double> value = elf.get_object_virtual<double>(virtual_address);
			CCC_RETURN_IF_ERROR(value);
			
			std::string string = stringf("%g", *value);
			if(strtof(string.c_str(), nullptr) != *value) {
				string = stringf("%.17g", *value);
			}
			data.value = std::move(string);
			
			break;
		}
		case ast::BuiltInClass::UNSIGNED_128:
		case ast::BuiltInClass::SIGNED_128:
		case ast::BuiltInClass::UNQUALIFIED_128:
		case ast::BuiltInClass::FLOAT_128: {
			Result<std::span<const float>> value = elf.get_array_virtual<float>(virtual_address, 4);
			CCC_RETURN_IF_ERROR(value);
			
			data.value = stringf("VECTOR(%s, %s, %s, %s)",
				single_precision_float_to_string((*value)[0]).c_str(),
				single_precision_float_to_string((*value)[1]).c_str(),
				single_precision_float_to_string((*value)[2]).c_str(),
				single_precision_float_to_string((*value)[3]).c_str());
				
			break;
		}
	}
	
	return data;
}

static Result<RefinedData> refine_pointer_or_reference(
	u32 virtual_address, const ast::Node& type, const SymbolDatabase& database, const ElfFile& elf)
{
	RefinedData data;
	u32 pointer = 0;
	Result<void> read_result = elf.copy_virtual((u8*) &pointer, virtual_address, 4);
	CCC_RETURN_IF_ERROR(read_result);
	
	std::string string;
	if(pointer != 0) {
		FunctionHandle function_handle = database.functions.first_handle_from_starting_address(pointer);
		const Function* function_symbol = database.functions.symbol_from_handle(function_handle);
		
		GlobalVariableHandle global_variable_handle = database.global_variables.first_handle_from_starting_address(pointer);
		const GlobalVariable* global_variable_symbol = database.global_variables.symbol_from_handle(global_variable_handle);
		
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
			string = stringf("0x%x", pointer);
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
