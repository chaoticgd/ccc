// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "symbol_json.h"

#include "registers.h"

namespace ccc {

const u32 JSON_FORMAT_VERSION = 15;

template <typename SymbolType>
static void write_symbol_list(
	JsonWriter& json,
	const SymbolList<SymbolType>& list,
	const SymbolDatabase& database,
	const std::set<SymbolSourceHandle>* sources);

static void write_json(JsonWriter& json, const GlobalStorage& storage, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const RegisterStorage& storage, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const StackStorage& storage, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const DataType& symbol, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const Function& symbol, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const GlobalVariable& symbol, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const Label& symbol, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const LocalVariable& symbol, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const Module& symbol, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const ParameterVariable& symbol, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const Section& symbol, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const SourceFile& symbol, const SymbolDatabase& database);
static void write_json(JsonWriter& json, const SymbolSource& symbol, const SymbolDatabase& database);

void write_json(
	JsonWriter& json,
	const SymbolDatabase& database,
	const char* application_name,
	const std::set<SymbolSourceHandle>* sources)
{
	json.StartObject();
	
	json.Key("format");
	json.String("CCC Symbol Database");
	
	json.Key("version");
	json.Uint(JSON_FORMAT_VERSION);
	
	json.Key("application");
	json.String(application_name);
	
	#define CCC_X(SymbolType, symbol_list) \
		if (!std::is_same_v<SymbolType, SymbolSource>) { \
			json.Key(#symbol_list); \
			write_symbol_list<SymbolType>(json, database.symbol_list, database, sources); \
		}
	CCC_FOR_EACH_SYMBOL_TYPE_DO_X
	#undef CCC_X
	
	json.EndObject();
}

template <typename SymbolType>
static void write_symbol_list(
	JsonWriter& json,
	const SymbolList<SymbolType>& list,
	const SymbolDatabase& database,
	const std::set<SymbolSourceHandle>* sources)
{
	json.StartArray();
	for (const SymbolType& symbol : list) {
		if (sources && !sources->contains(symbol.source())) {
			continue;
		}
		
		json.StartObject();
		
		if (!symbol.name().empty()) {
			json.Key("name");
			json.String(symbol.name());
		}
		
		if (symbol.address().valid()) {
			json.Key("address");
			json.Uint(symbol.address().value);
		}
		
		if (symbol.size() != 0) {
			json.Key("size");
			json.Uint(symbol.size());
		}
		
		if (symbol.module_handle().valid()) {
			json.Key("module");
			json.Uint(database.modules.index_from_handle(symbol.module_handle()));
		}
		
		write_json(json, symbol, database);
		
		if (symbol.type()) {
			json.Key("type");
			ast::write_json(json, symbol.type(), database);
		}
		
		json.EndObject();
	}
	json.EndArray();
}

static void write_json(JsonWriter& json, const GlobalStorage& storage, const SymbolDatabase& database)
{
	json.Key("storage");
	json.StartObject();
	json.Key("type");
	json.String("global");
	json.Key("location");
	json.String(global_storage_location_to_string(storage.location));
	json.EndObject();
}

static void write_json(JsonWriter& json, const RegisterStorage& storage, const SymbolDatabase& database)
{
	json.Key("storage");
	json.StartObject();
	auto [register_class, register_index_relative] =
		mips::map_dbx_register_index(storage.dbx_register_number);
	json.Key("type");
	json.String("register");
	json.Key("register");
	json.String(mips::REGISTER_STRING_TABLES[(s32) register_class][register_index_relative]);
	json.Key("register_class");
	json.String(mips::REGISTER_CLASSES[(s32) register_class]);
	json.Key("dbx_register_number");
	json.Int(storage.dbx_register_number);
	json.Key("register_index");
	json.Int(register_index_relative);
	json.Key("is_by_reference");
	json.Bool(storage.is_by_reference);
	json.EndObject();
}

static void write_json(JsonWriter& json, const StackStorage& storage, const SymbolDatabase& database)
{
	json.Key("storage");
	json.StartObject();
	json.Key("type");
	json.String("stack");
	json.Key("offset");
	json.Int(storage.stack_pointer_offset);
	json.EndObject();
}

static void write_json(JsonWriter& json, const DataType& symbol, const SymbolDatabase& database)
{
	if (symbol.files.empty()) {
		json.Key("files");
		json.StartArray();
		for (SourceFileHandle file : symbol.files) {
			json.Uint(file.value);
		}
		json.EndArray();
	}
}

static void write_json(JsonWriter& json, const Function& symbol, const SymbolDatabase& database)
{
	if (!symbol.relative_path.empty()) {
		json.Key("relative_path");
		json.String(symbol.relative_path);
	}
	
	if (symbol.storage_class != STORAGE_CLASS_NONE) {
		json.Key("storage_class");
		json.String(ast::storage_class_to_string(symbol.storage_class));
	}
	
	if (symbol.stack_frame_size > -1) {
		json.Key("stack_frame_size");
		json.Int(symbol.stack_frame_size);
	}
	
	if (!symbol.line_numbers.empty()) {
		json.Key("line_numbers");
		json.StartArray();
		for (const Function::LineNumberPair& pair : symbol.line_numbers) {
			json.StartArray();
			json.Uint(pair.address.value);
			json.Int(pair.line_number);
			json.EndArray();
		}
		json.EndArray();
	}
	
	if (!symbol.sub_source_files.empty()) {
		json.Key("sub_source_files");
		json.StartArray();
		for (const Function::SubSourceFile& sub : symbol.sub_source_files) {
			json.StartArray();
			json.Uint(sub.address.value);
			json.String(sub.relative_path);
			json.EndArray();
		}
		json.EndArray();
	}
	
	if (symbol.is_member_function_ish) {
		json.Key("is_member_function_ish");
		json.Bool(symbol.is_member_function_ish);
	}
	
	if (symbol.parameter_variables().has_value()) {
		json.Key("parameter_variables");
		json.StartArray();
		for (ParameterVariableHandle handle : *symbol.parameter_variables()) {
			s32 index = database.parameter_variables.index_from_handle(handle);
			if (index != -1) {
				json.Int(index);
			}
		}
		json.EndArray();
	}
	
	if (symbol.local_variables().has_value()) {
		json.Key("local_variables");
		json.StartArray();
		for (LocalVariableHandle handle : *symbol.local_variables()) {
			s32 index = database.local_variables.index_from_handle(handle);
			if (index != -1) {
				json.Int(index);
			}
		}
		json.EndArray();
	}
	
	if (symbol.original_hash() != 0) {
		json.Key("hash");
		json.Uint(symbol.original_hash());
	}
}

static void write_json(JsonWriter& json, const GlobalVariable& symbol, const SymbolDatabase& database)
{
	write_json(json, symbol.storage, database);
	
	if (symbol.storage_class != STORAGE_CLASS_NONE) {
		json.Key("storage_class");
		json.String(ast::storage_class_to_string(symbol.storage_class));
	}
}

static void write_json(JsonWriter& json, const Label& symbol, const SymbolDatabase& database) {}

static void write_json(JsonWriter& json, const LocalVariable& symbol, const SymbolDatabase& database)
{
	if (const GlobalStorage* storage = std::get_if<GlobalStorage>(&symbol.storage)) {
		write_json(json, *storage, database);
	}
	
	if (const RegisterStorage* storage = std::get_if<RegisterStorage>(&symbol.storage)) {
		write_json(json, *storage, database);
	}
	
	if (const StackStorage* storage = std::get_if<StackStorage>(&symbol.storage)) {
		write_json(json, *storage, database);
	}
	
	if (symbol.live_range.low.valid() && symbol.live_range.high.valid()) {
		json.Key("live_range");
		json.StartArray();
		json.Uint(symbol.live_range.low.value);
		json.Uint(symbol.live_range.high.value);
		json.EndArray();
	}
}

static void write_json(JsonWriter& json, const Module& symbol, const SymbolDatabase& database) {}

static void write_json(JsonWriter& json, const ParameterVariable& symbol, const SymbolDatabase& database)
{
	if (const RegisterStorage* storage = std::get_if<RegisterStorage>(&symbol.storage)) {
		write_json(json, *storage, database);
	}
	
	if (const StackStorage* storage = std::get_if<StackStorage>(&symbol.storage)) {
		write_json(json, *storage, database);
	}
}

static void write_json(JsonWriter& json, const Section& symbol, const SymbolDatabase& database) {}

static void write_json(JsonWriter& json, const SourceFile& symbol, const SymbolDatabase& database)
{
	if (!symbol.working_dir.empty()) {
		json.Key("working_dir");
		json.String(symbol.working_dir);
	}
	
	if (!symbol.command_line_path.empty()) {
		json.Key("command_line_path");
		json.String(symbol.command_line_path);
	}
	
	if (!symbol.toolchain_version_info.empty()) {
		json.Key("toolchain_version");
		json.StartArray();
		for (const std::string& info : symbol.toolchain_version_info) {
			json.String(info);
		}
		json.EndArray();
	}
	
	if (!symbol.functions().empty()) {
		json.Key("functions");
		json.StartArray();
		for (FunctionHandle handle : symbol.functions()) {
			s32 index = database.functions.index_from_handle(handle);
			if (index != -1) {
				json.Int(index);
			}
		}
		json.EndArray();
	}
	
	if (!symbol.global_variables().empty()) {
		json.Key("global_variables");
		json.StartArray();
		for (GlobalVariableHandle handle : symbol.global_variables()) {
			s32 index = database.global_variables.index_from_handle(handle);
			if (index != -1) {
				json.Int(index);
			}
		}
		json.EndArray();
	}
}

static void write_json(JsonWriter& json, const SymbolSource& symbol, const SymbolDatabase& database) {}

}
