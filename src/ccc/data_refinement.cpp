// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "data_refinement.h"

#include "ast.h"
#include "int128.h"

#include <bit>

namespace ccc {

static Result<RefinedData> refine_node(
	u32 virtual_address, const ast::Node& type, const SymbolDatabase& database, const ElfFile& elf, s32 depth);
static Result<RefinedData> refine_bitfield(
	u32 virtual_address, const ast::BitField& bit_field, const SymbolDatabase& database, const ElfFile& elf);
static Result<RefinedData> refine_builtin(
	u32 virtual_address, ast::BuiltInClass bclass, const SymbolDatabase& database, const ElfFile& elf);
static Result<RefinedData> refine_pointer_or_reference(
	u32 virtual_address, const ast::Node& type, const SymbolDatabase& database, const ElfFile& elf);
static std::string builtin_to_string(u128 value, ast::BuiltInClass bclass);
static s64 sign_extend_from_size(u64 value, u32 size);
static std::string single_precision_float_to_string(float value);

CCC_FORMAT_ATTRIBUTE(printf, 1, 2)
static std::string string_format(const char* format, ...);

bool can_refine_variable(const VariableToRefine& variable)
{
	if (!variable.storage)
		return false;
	if (variable.storage->location == GlobalStorageLocation::BSS)
		return false;
	if (variable.storage->location == GlobalStorageLocation::SBSS)
		return false;
	if (!variable.address.has_value())
		return false;
	if (!variable.type)
		return false;
	return true;
}

Result<RefinedData> refine_variable(
	const VariableToRefine& variable, const SymbolDatabase& database, const ElfFile& elf)
{
	CCC_ASSERT(variable.type);
	return refine_node(variable.address.value(), *variable.type, database, elf, 0);
}

static Result<RefinedData> refine_node(
	u32 virtual_address, const ast::Node& type, const SymbolDatabase& database, const ElfFile& elf, s32 depth)
{
	if (depth > 200) {
		CCC_WARN("Call depth greater than 200 in refine_node, probably infinite recursion.");

		RefinedData data;
		data.value = "Call depth greater than 200 in refine_node, probably infinite recursion.";
		return data;
	}

	switch (type.descriptor) {
		case ast::ARRAY: {
			const ast::Array& array = type.as<ast::Array>();
			CCC_CHECK(
				array.element_type->size_bytes.has_value(),
				"Cannot compute element size for '%s' array.", array.name.c_str());
			RefinedData list;
			std::vector<RefinedData>& elements = list.value.emplace<std::vector<RefinedData>>();
			for (u32 i = 0; i < array.element_count.get_or_zero(); i++) {
				u32 offset = i * array.element_type->size_bytes.value();
				Result<RefinedData> element = refine_node(
					virtual_address + offset, *array.element_type.get(), database, elf, depth + 1);
				CCC_RETURN_IF_ERROR(element);
				element->field_name = "[" + std::to_string(i) + "]";
				elements.emplace_back(std::move(*element));
			}
			return list;
		}
		case ast::BITFIELD: {
			const ast::BitField& bit_field = type.as<ast::BitField>();
			return refine_bitfield(virtual_address, bit_field, database, elf);
		}
		case ast::BUILTIN: {
			const ast::BuiltIn& builtin = type.as<ast::BuiltIn>();
			return refine_builtin(virtual_address, builtin.bclass, database, elf);
		}
		case ast::ENUM: {
			const ast::Enum& enumeration = type.as<ast::Enum>();

			s32 value = 0;
			bool read_result = elf.copy_virtual((u8*) &value, virtual_address, 4);
			CCC_CHECK(read_result, "Failed to read enum at 0x%x.", virtual_address);

			RefinedData data;
			for (const auto& [number, name] : enumeration.constants) {
				if (number == value) {
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

			for (size_t i = 0; i < struct_or_union.base_classes.size(); i++) {
				const std::unique_ptr<ast::Node>& base_class = struct_or_union.base_classes[i];
				CCC_CHECK(base_class->offset_bytes.has_value(), "Base class without offset.");
				u32 base_class_address = virtual_address + base_class->offset_bytes.value();
				Result<RefinedData> child = refine_node(
					base_class_address, *base_class.get(), database, elf, depth + 1);
				CCC_RETURN_IF_ERROR(child);
				child->field_name = "base class " + std::to_string(i);
				children.emplace_back(std::move(*child));
			}

			for (const std::unique_ptr<ast::Node>& field : struct_or_union.fields) {
				if (field->storage_class == STORAGE_CLASS_STATIC) {
					continue;
				}
				CCC_CHECK(field->offset_bytes.has_value(), "Field without offset.");
				u32 field_address = virtual_address + field->offset_bytes.value();
				Result<RefinedData> child = refine_node(field_address, *field.get(), database, elf, depth + 1);
				CCC_RETURN_IF_ERROR(child);
				child->field_name = "." + field->name;
				children.emplace_back(std::move(*child));
			}

			return list;
		}
		case ast::TYPE_NAME: {
			const ast::TypeName& type_name = type.as<ast::TypeName>();
			const DataType* resolved_type = database.data_types.symbol_from_handle(type_name.data_type_handle);
			return refine_node(virtual_address, *resolved_type->type(), database, elf, depth + 1);
		}
	}

	return CCC_FAILURE("Failed to refine variable (%s).", ast::node_type_to_string(type));
}

static Result<RefinedData> refine_bitfield(
	u32 virtual_address, const ast::BitField& bit_field, const SymbolDatabase& database, const ElfFile& elf)
{
	ast::BuiltInClass storage_unit_type = bit_field.storage_unit_type(database);

	CCC_CHECK(bit_field.bitfield_offset_bits.has_value(), "Bit-field without bit offset.");
	CCC_CHECK(bit_field.size_bits.has_value(), "Bit-field without bit size.");

	u128 value;
	switch (storage_unit_type) {
		case ast::BuiltInClass::UNSIGNED_8:
		case ast::BuiltInClass::UNQUALIFIED_8: {
			std::optional<u8> storage_unit = elf.get_object_virtual<u8>(virtual_address);
			CCC_CHECK(storage_unit, "Failed to read 8-bit bitfield storage unit at 0x%x.", virtual_address);
			value = unpack_unsigned_bitfield(
				*storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		case ast::BuiltInClass::BOOL_8: {
			std::optional<u8> storage_unit = elf.get_object_virtual<u8>(virtual_address);
			CCC_CHECK(storage_unit, "Failed to read 8-bit bitfield storage unit at 0x%x.", virtual_address);
			value = unpack_unsigned_bitfield(
				*storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		case ast::BuiltInClass::SIGNED_8: {
			std::optional<u8> storage_unit = elf.get_object_virtual<u8>(virtual_address);
			CCC_CHECK(storage_unit, "Failed to read 8-bit bitfield storage unit at 0x%x.", virtual_address);
			value = unpack_signed_bitfield(
				*storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		case ast::BuiltInClass::UNSIGNED_16: {
			std::optional<u16> storage_unit = elf.get_object_virtual<u16>(virtual_address);
			CCC_CHECK(storage_unit, "Failed to read 16-bit bitfield storage unit at 0x%x.", virtual_address);
			value = unpack_unsigned_bitfield(
				*storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		case ast::BuiltInClass::SIGNED_16: {
			std::optional<u16> storage_unit = elf.get_object_virtual<u16>(virtual_address);
			CCC_CHECK(storage_unit, "Failed to read 16-bit bitfield storage unit at 0x%x.", virtual_address);
			value = unpack_signed_bitfield(
				*storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		case ast::BuiltInClass::UNSIGNED_32: {
			std::optional<u32> storage_unit = elf.get_object_virtual<u32>(virtual_address);
			CCC_CHECK(storage_unit, "Failed to read 32-bit bitfield storage unit at 0x%x.", virtual_address);
			value = unpack_unsigned_bitfield(
				*storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		case ast::BuiltInClass::SIGNED_32: {
			std::optional<u32> storage_unit = elf.get_object_virtual<u32>(virtual_address);
			CCC_CHECK(storage_unit, "Failed to read 32-bit bitfield storage unit at 0x%x.", virtual_address);
			value = unpack_signed_bitfield(
				*storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		case ast::BuiltInClass::UNSIGNED_64: {
			std::optional<u64> storage_unit = elf.get_object_virtual<u64>(virtual_address);
			CCC_CHECK(storage_unit, "Failed to read 64-bit bitfield storage unit at 0x%x.", virtual_address);
			value = unpack_unsigned_bitfield(
				*storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		case ast::BuiltInClass::SIGNED_64: {
			std::optional<u64> storage_unit = elf.get_object_virtual<u64>(virtual_address);
			CCC_CHECK(storage_unit, "Failed to read 64-bit bitfield storage unit at 0x%x.", virtual_address);
			value = unpack_signed_bitfield(
				*storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		case ast::BuiltInClass::UNSIGNED_128:
		case ast::BuiltInClass::SIGNED_128:
		case ast::BuiltInClass::UNQUALIFIED_128:
		case ast::BuiltInClass::FLOAT_128: {
			std::optional<u64> low = elf.get_object_virtual<u64>(virtual_address);
			CCC_CHECK(low.has_value(), "Failed to read lower half of 128-bit bitfield storage unit at 0x%x.",
				virtual_address);

			std::optional<u64> high = elf.get_object_virtual<u64>(virtual_address + 8);
			CCC_CHECK(high.has_value(), "Failed to read upper half of 128-bit bitfield storage unit at 0x%x.",
				virtual_address);

			u128 storage_unit;
			storage_unit.low = *low;
			storage_unit.high = *high;

			value = unpack_unsigned_bitfield(
				storage_unit, bit_field.bitfield_offset_bits.value(), bit_field.size_bits.value());
			break;
		}
		default: break;
	}

	RefinedData data;
	data.value = builtin_to_string(value, storage_unit_type);

	return data;
}

static Result<RefinedData> refine_builtin(
	u32 virtual_address, ast::BuiltInClass bclass, const SymbolDatabase& database, const ElfFile& elf)
{
	u128 value;
	switch (bclass) {
		case ast::BuiltInClass::INVALID:
		case ast::BuiltInClass::VOID_TYPE: {
			break;
		}
		case ast::BuiltInClass::UNSIGNED_8:
		case ast::BuiltInClass::SIGNED_8:
		case ast::BuiltInClass::UNQUALIFIED_8:
		case ast::BuiltInClass::BOOL_8:
		case ast::BuiltInClass::UNSIGNED_16:
		case ast::BuiltInClass::SIGNED_16:
		case ast::BuiltInClass::UNSIGNED_32:
		case ast::BuiltInClass::SIGNED_32:
		case ast::BuiltInClass::FLOAT_32:
		case ast::BuiltInClass::UNSIGNED_64:
		case ast::BuiltInClass::SIGNED_64:
		case ast::BuiltInClass::FLOAT_64: {
			u32 size = builtin_class_size(bclass).value();
			bool read_result = elf.copy_virtual((u8*) &value.low, virtual_address, size);
			CCC_CHECK(read_result, "Failed to read built-in at 0x%x.", virtual_address);
			break;
		}
		case ast::BuiltInClass::UNSIGNED_128:
		case ast::BuiltInClass::SIGNED_128:
		case ast::BuiltInClass::UNQUALIFIED_128:
		case ast::BuiltInClass::FLOAT_128: {
			std::optional<u64> low = elf.get_object_virtual<u64>(virtual_address);
			CCC_CHECK(low, "Failed to read lower half of 128-bit built-in at 0x%x.", virtual_address);

			std::optional<u64> high = elf.get_object_virtual<u64>(virtual_address + 8);
			CCC_CHECK(high, "Failed to read upper half of 128-bit built-in at 0x%x.", virtual_address + 8);

			value.low = *low;
			value.high = *high;

			break;
		}
	}

	RefinedData data;
	data.value = builtin_to_string(value, bclass);

	return data;
}

static Result<RefinedData> refine_pointer_or_reference(
	u32 virtual_address, const ast::Node& type, const SymbolDatabase& database, const ElfFile& elf)
{
	RefinedData data;
	u32 pointer = 0;
	bool read_result = elf.copy_virtual((u8*) &pointer, virtual_address, 4);
	CCC_CHECK(read_result, "Failed to read pointer at 0x%x.", virtual_address);

	std::string string;
	if (pointer != 0) {
		FunctionHandle function_handle = database.functions.first_handle_from_starting_address(pointer);
		const Function* function_symbol = database.functions.symbol_from_handle(function_handle);

		GlobalVariableHandle global_variable_handle = database.global_variables.first_handle_from_starting_address(
			pointer);
		const GlobalVariable* global_variable_symbol = database.global_variables.symbol_from_handle(
			global_variable_handle);

		if (function_symbol) {
			bool is_pointer = type.descriptor == ast::POINTER_OR_REFERENCE
				&& type.as<ast::PointerOrReference>().is_pointer;
			if (is_pointer) {
				string += "&";
			}
			string += function_symbol->name();
		} else if (global_variable_symbol) {
			bool is_pointer = type.descriptor == ast::POINTER_OR_REFERENCE
				&& type.as<ast::PointerOrReference>().is_pointer;
			bool pointing_at_array = global_variable_symbol->type()
				&& global_variable_symbol->type()->descriptor == ast::ARRAY;
			if (is_pointer && !pointing_at_array) {
				string += "&";
			}
			string += global_variable_symbol->name();
		} else {
			string = string_format("0x%x", pointer);
		}
	} else {
		string = "NULL";
	}
	data.value = std::move(string);

	return data;
}

static std::string builtin_to_string(u128 value, ast::BuiltInClass bclass)
{
	std::string result;

	switch (bclass) {
		case ast::BuiltInClass::INVALID: {
			result = "CCC_ERROR(\"Invalid built-in type.\")";
			break;
		}
		case ast::BuiltInClass::VOID_TYPE: {
			result = "CCC_ERROR(\"Unexpected void type.\")";
			break;
		}
		case ast::BuiltInClass::UNSIGNED_8:
		case ast::BuiltInClass::UNQUALIFIED_8:
		case ast::BuiltInClass::UNSIGNED_16:
		case ast::BuiltInClass::UNSIGNED_32:
		case ast::BuiltInClass::UNSIGNED_64: {
			result = std::to_string(value.low);
			break;
		}
		case ast::BuiltInClass::SIGNED_8:
		case ast::BuiltInClass::SIGNED_16:
		case ast::BuiltInClass::SIGNED_32:
		case ast::BuiltInClass::SIGNED_64: {
			u32 size = builtin_class_size(bclass).value();
			s64 extended_value = sign_extend_from_size(value.low, size);
			result = std::to_string(extended_value);
			break;
		}
		case ast::BuiltInClass::BOOL_8: {
			result = value.low ? "true" : "false";
			break;
		}
		case ast::BuiltInClass::FLOAT_32: {
			static_assert(sizeof(float) == 4);
			result = single_precision_float_to_string(std::bit_cast<float>(static_cast<u32>(value.low)));
			break;
		}
		case ast::BuiltInClass::FLOAT_64: {
			static_assert(sizeof(double) == 8);

			double float_value = std::bit_cast<double>(value.low);
			std::string string = string_format("%g", float_value);
			if (strtof(string.c_str(), nullptr) != float_value) {
				string = string_format("%.17g", float_value);
			}

			result = std::move(string);

			break;
		}
		case ast::BuiltInClass::UNSIGNED_128:
		case ast::BuiltInClass::SIGNED_128:
		case ast::BuiltInClass::UNQUALIFIED_128:
		case ast::BuiltInClass::FLOAT_128: {
			result = std::string("0x") + value.to_string();
			break;
		}
	}

	return result;
}

static s64 sign_extend_from_size(u64 value, u32 size)
{
	switch (size) {
		case 1: return static_cast<s64>(static_cast<u8>(value));
		case 2: return static_cast<s64>(static_cast<u16>(value));
		case 4: return static_cast<s64>(static_cast<u32>(value));
	}
	return static_cast<s64>(value);
}

static std::string single_precision_float_to_string(float value)
{
	std::string result = string_format("%g", value);
	if (strtof(result.c_str(), nullptr) != value) {
		result = string_format("%.9g", value);
	}
	if (result.find(".") == std::string::npos) {
		result += ".";
	}
	result += "f";
	return result;
}

static std::string string_format(const char* format, ...)
{
	va_list args;
	char buffer[256];

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	return std::string(buffer);
}

}
