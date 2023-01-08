// Imports symbols from JSON files written out by stdump.
//@author chaoticgd
//@category _NEW_
import java.io.Console;
import java.io.FileReader;
import java.lang.reflect.Type;
import java.util.*;
import com.google.gson.*;
import com.google.gson.stream.JsonReader;
import generic.stl.Pair;
import ghidra.app.script.GhidraScript;
import ghidra.app.services.ConsoleService;
import ghidra.app.services.DataTypeManagerService;
import ghidra.framework.plugintool.PluginTool;
import ghidra.program.model.mem.*;
import ghidra.program.model.lang.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.util.*;
import ghidra.program.model.reloc.*;
import ghidra.program.model.data.*;
import ghidra.program.model.block.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;

public class ImportStdumpSymbolsIntoGhidra extends GhidraScript {
	public void run() throws Exception {
		String json_path = askString("Enter Path", "Path to .json file:");
		JsonReader reader = new JsonReader(new FileReader(json_path));
		
		GsonBuilder gson_builder = new GsonBuilder();
		gson_builder.registerTypeAdapter(ParsedJsonFile.class, new JsonFileDeserializer());
		gson_builder.registerTypeAdapter(AST.Node.class, new NodeDeserializer());
		Gson gson = gson_builder.create();
		ParsedJsonFile json = gson.fromJson(reader, ParsedJsonFile.class);
		
		TypeImporterState importer = new TypeImporterState();
		importer.nodes = json.deduplicated_types;
		import_types(importer, json.files);
		import_functions(importer);
		import_globals(importer);
	}
	
	// *************************************************************************

	public class TypeImporterState {
		DataTypeManager program_type_manager = null;
		
		List<AST.Node> nodes;
		ArrayList<Pair<DataType, Integer>> types = new ArrayList<>(); // (data type, size in bytes)
		ArrayList<HashMap<Integer, Integer>> stabs_type_number_to_deduplicated_type_index = new ArrayList<>();
		
		int current_type = -1;
		
		ConsoleService console;
	}
	
	public void import_types(TypeImporterState importer, List<AST.Node> source_files) throws Exception {
		importer.program_type_manager = currentProgram.getDataTypeManager();
		int transaction_id = importer.program_type_manager.startTransaction("stdump import script");
		
		importer.console = state.getTool().getService(ConsoleService.class);
		
		// Gather information required for type lookup.
		for(AST.Node node : source_files) {
			AST.SourceFile file = (AST.SourceFile) node;
			importer.stabs_type_number_to_deduplicated_type_index.add(file.stabs_type_number_to_deduplicated_type_index);
		}
		
		for(int i = 0; i < importer.nodes.size(); i++) {
			importer.types.add(null);
		}
		
		// Create and register the types recursively.
		for(int i = 0; i < importer.nodes.size(); i++) {
			if(importer.types.get(i) == null) {
				importer.current_type = i;
				AST.Node node = importer.nodes.get(i);
				print("Importing " + node.getClass().getSimpleName() + " " + node.name + "\n");
				Pair<DataType, Integer> type = node.create_type(importer);
				importer.types.set(i, type);
				importer.program_type_manager.addDataType(type.first, null);
			}
		}
		
		importer.program_type_manager.endTransaction(transaction_id, true);
	}
	
	
	// *************************************************************************
	
	public void import_functions(TypeImporterState importer) {
		
	}
	
	// *************************************************************************
	
	public void import_globals(TypeImporterState importer) {
		
	}
	
	// *************************************************************************
	
	// These should mirror the definitions in ast.h.
	public static class AST {
		public static enum StorageClass {
			NONE,
			TYPEDEF,
			EXTERN,
			STATIC,
			AUTO,
			REGISTER
		}
		
		public static class Node {
			String name;
			StorageClass storage_class = StorageClass.NONE;
			int relative_offset_bytes = -1;
			int absolute_offset_bytes = -1;
			int bitfield_offset_bits = -1;
			int size_bits = -1;
			int order = -1;
			int first_file = -1;
			boolean conflict = false;
			int stabs_type_number = -1;
			
			// Return (data type, size in bytes) pair.
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				throw new Exception("Method create_type() called on AST node that isn't a type.");
			}
			
			String generate_name() {
				if(conflict || name == null || name.isEmpty()) {
					return name + "__" + Integer.toString(first_file) + "_" + Integer.toString(stabs_type_number);
				}
				return name;
			}
		}
		
		public static class Array extends Node {
			Node element_type;
			int element_count;
			
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				Pair<DataType, Integer> element = element_type.create_type(importer);
				DataType type = new ArrayDataType(element.first, element_count, element.second);
				int size_bytes = element_count * element.second;
				return new Pair<>(type, size_bytes);
			}
		}
		
		public static class BitField extends Node {
			Node underlying_type;
			
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				return underlying_type.create_type(importer);
			}
		}
		
		public static enum BuiltInClass {
			VOID,
			UNSIGNED_8, SIGNED_8, UNQUALIFIED_8, BOOL_8,
			UNSIGNED_16, SIGNED_16,
			UNSIGNED_32, SIGNED_32, FLOAT_32,
			UNSIGNED_64, SIGNED_64, FLOAT_64,
			UNSIGNED_128, SIGNED_128, UNQUALIFIED_128, FLOAT_128,
			UNKNOWN_PROBABLY_ARRAY
		}
		
		public static class BuiltIn extends Node {
			BuiltInClass builtin_class;
			
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				switch(builtin_class) {
				case VOID:
					return new Pair<>(VoidDataType.dataType, 1);
				case UNSIGNED_8:
					return new Pair<>(UnsignedCharDataType.dataType, 1);
				case SIGNED_8:
				case UNQUALIFIED_8:
				case BOOL_8:
					return new Pair<>(CharDataType.dataType, 1);
				case UNSIGNED_16:
					return new Pair<>(ShortDataType.dataType, 2);
				case SIGNED_16:
					return new Pair<>(UnsignedShortDataType.dataType, 2);
				case UNSIGNED_32:
					return new Pair<>(UnsignedIntegerDataType.dataType, 4);
				case SIGNED_32:
					return new Pair<>(IntegerDataType.dataType, 4);
				case FLOAT_32:
					return new Pair<>(FloatDataType.dataType, 4);
				case UNSIGNED_64:
					return new Pair<>(UnsignedLongDataType.dataType, 8);
				case SIGNED_64:
				case FLOAT_64:
					return new Pair<>(LongDataType.dataType, 8);
				case UNSIGNED_128:
					return new Pair<>(UnsignedInteger16DataType.dataType, 16);
				case SIGNED_128:
					return new Pair<>(Integer16DataType.dataType, 16);
				case UNQUALIFIED_128:
				case FLOAT_128:
					return new Pair<>(UnsignedInteger16DataType.dataType, 16);
				case UNKNOWN_PROBABLY_ARRAY:
				}
				throw new Exception("Method create_type() called on unknown builtin.");
			}
		}
		
		public static class CompoundStatement extends Node {
			ArrayList<Node> children = new ArrayList<Node>();
		}
		
		public static class FunctionDefinition extends Node {
			Node type;
			Node body;
		}
		
		public static class FunctionType extends Node {
			Node return_type;
			ArrayList<Node> parameters = new ArrayList<Node>();
			String modifiers;
			boolean is_constructor = false;
			
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				return new Pair<>(Undefined8DataType.dataType, 1);
			}
		}
		
		public static class EnumConstant {
			int value;
			String name;
		}
		
		public static class InlineEnum extends Node {
			ArrayList<EnumConstant> constants = new ArrayList<EnumConstant>();
			
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				EnumDataType type = new EnumDataType(generate_name(), 4);
				for(EnumConstant constant : constants) {
					type.add(constant.name, constant.value);
				}
				return new Pair<>(type, 4);
			}
		}
		
		public static class BaseClass {
			int offset;
			Node type;
		}
		
		public static class InlineStructOrUnion extends Node {
			boolean is_struct;
			ArrayList<BaseClass> base_classes = new ArrayList<BaseClass>();
			ArrayList<Node> fields = new ArrayList<Node>();
			ArrayList<Node> member_functions = new ArrayList<Node>();
			
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				String type_name = generate_name();
				int size_bytes = size_bits / 8;
				if(is_struct) {
					StructureDataType type = new StructureDataType(type_name, size_bytes, importer.program_type_manager);
					for(AST.Node node : fields) {
						if(node.storage_class != StorageClass.STATIC) {
							Pair<DataType, Integer> field = node.create_type(importer);
							if(field.second > 0) {
								type.replaceAtOffset(node.relative_offset_bytes, field.first, field.second, node.name, "");
							}
						}
					}
					return new Pair<>(type, size_bytes);
				}
				UnionDataType type = new UnionDataType(type_name);
				for(AST.Node node : fields) {
					if(node.storage_class != StorageClass.STATIC) {
						Pair<DataType, Integer> field = node.create_type(importer);
						type.add(field.first, field.second, node.name, "");
					}
				}
				return new Pair<>(type, size_bytes);
			}
		}
		
		public static class Pointer extends Node {
			Node value_type;
			
			public int size_bytes(TypeImporterState importer) throws Exception {
				return 4;
			}
			
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				return new Pair<>(PointerDataType.dataType, 4);
			}
		}
		
		public static class Reference extends Node {
			Node value_type;
			
			public int size_bytes(TypeImporterState importer) throws Exception {
				return 4;
			}
			
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				return new Pair<>(new PointerDataType(value_type.create_type(importer).first), 4);
			}
		}
		
		public static class SourceFile extends Node {
			String path;
			int text_address;
			ArrayList<Node> types = new ArrayList<Node>();
			ArrayList<Node> functions = new ArrayList<Node>();
			ArrayList<Node> globals = new ArrayList<Node>();
			HashMap<Integer, Integer> stabs_type_number_to_deduplicated_type_index = new HashMap<Integer, Integer>();
		}
		
		public static class TypeName extends Node {
			String type_name;
			int referenced_file_index = -1;
			int referenced_stabs_type_number = -1;
			
			public Pair<DataType, Integer> create_type(TypeImporterState importer) throws Exception {
				if(type_name.equals("void")) {
					return new Pair<>(VoidDataType.dataType, 1);
				}
				if(referenced_file_index == -1 || referenced_stabs_type_number == -1) {
					importer.console.print("Type lookup failed #1: " + type_name + "\n");
					return new Pair<>(Undefined8DataType.dataType, 1);
				}
				HashMap<Integer, Integer> index_lookup = importer.stabs_type_number_to_deduplicated_type_index.get(referenced_file_index);
				Integer index = index_lookup.get(referenced_stabs_type_number);
				if(index == null || index == importer.current_type) {
					importer.console.print("Type lookup failed #2: " + type_name + "\n");
					return new Pair<>(Undefined8DataType.dataType, 1);
				}
				Pair<DataType, Integer> type = importer.types.get(index);
				if(type == null) {
					AST.Node node = importer.nodes.get(index);
					type = node.create_type(importer);
					importer.types.set(index, type);
					importer.program_type_manager.addDataType(type.first, null);
				}
				return type;
			}
		}
		
		public static enum VariableClass {
			GLOBAL,
			LOCAL,
			PARAMETER
		}
		
		public static enum VariableStorageLocation {
			BSS,
			DATA,
			REGISTER,
			STACK
		}
		
		public static class VariableStorage {
			VariableStorageLocation location;
			int bss_or_data_address = -1;
			String register;
			String register_class;
			int dbx_register_number = -1;
			int register_index_relative = -1;
			int stack_pointer_offset = -1;
		}
		
		public static class Variable extends Node {
			VariableClass variable_class;
			VariableStorage storage;
			int block_low = -1;
			int block_high = -1;
			Node type;
		}
	}
	
	// *************************************************************************
	
	public class ParsedJsonFile {
		ArrayList<AST.Node> files = new ArrayList<AST.Node>();
		ArrayList<AST.Node> deduplicated_types = new ArrayList<AST.Node>();
	}
	
	public class JsonFileDeserializer implements JsonDeserializer<ParsedJsonFile> {
		@Override
		public ParsedJsonFile deserialize(JsonElement element, Type type, JsonDeserializationContext context)
				throws JsonParseException {
			ParsedJsonFile result = new ParsedJsonFile();
			JsonObject object = element.getAsJsonObject();
			int supported_version = 1;
			if(!object.has("version")) {
				throw new JsonParseException("JSON file has missing version number field.");
			}
			int version = object.get("version").getAsInt();
			if(version != 1) {
				String version_info = Integer.toString(version) + ", should be " + Integer.toString(supported_version);
				throw new JsonParseException("JSON file is in an unsupported format (version is " + version_info + ")!");
			}
			JsonArray files = object.get("files").getAsJsonArray();
			for(JsonElement file_node : files) {
				result.files.add(context.deserialize(file_node, AST.Node.class));
			}
			JsonArray deduplicated_types = object.get("deduplicated_types").getAsJsonArray();
			for(JsonElement type_node : deduplicated_types) {
				result.deduplicated_types.add(context.deserialize(type_node, AST.Node.class));
			}
			return result;
		}
	}
	
	public class NodeDeserializer implements JsonDeserializer<AST.Node> {
		@Override
		public AST.Node deserialize(JsonElement element, Type type, JsonDeserializationContext context)
				throws JsonParseException {
			JsonObject object = element.getAsJsonObject();
			String descriptor = object.get("descriptor").getAsString();
			AST.Node node;
			if(descriptor.equals("array")) {
				AST.Array array = new AST.Array();
				array.element_type = context.deserialize(object.get("element_type"), AST.Node.class);
				array.element_count = object.get("element_count").getAsInt();
				node = array;
			} else if(descriptor.equals("bitfield")) {
				AST.BitField bitfield = new AST.BitField();
				bitfield.underlying_type = context.deserialize(object.get("underlying_type"), AST.Node.class);
				node = bitfield;
			} else if(descriptor.equals("builtin")) {
				AST.BuiltIn builtin = new AST.BuiltIn();
				String builtin_class = object.get("class").getAsString();
				if(builtin_class.equals("void")) { builtin.builtin_class = AST.BuiltInClass.VOID; }
				else if(builtin_class.equals("8-bit unsigned integer")) { builtin.builtin_class = AST.BuiltInClass.UNSIGNED_8; }
				else if(builtin_class.equals("8-bit signed integer")) { builtin.builtin_class = AST.BuiltInClass.SIGNED_8; }
				else if(builtin_class.equals("8-bit integer")) { builtin.builtin_class = AST.BuiltInClass.UNQUALIFIED_8; }
				else if(builtin_class.equals("8-bit boolean")) { builtin.builtin_class = AST.BuiltInClass.BOOL_8; }
				else if(builtin_class.equals("16-bit unsigned integer")) { builtin.builtin_class = AST.BuiltInClass.UNSIGNED_16; }
				else if(builtin_class.equals("16-bit signed integer")) { builtin.builtin_class = AST.BuiltInClass.SIGNED_16; }
				else if(builtin_class.equals("32-bit unsigned integer")) { builtin.builtin_class = AST.BuiltInClass.UNSIGNED_32; }
				else if(builtin_class.equals("32-bit signed integer")) { builtin.builtin_class = AST.BuiltInClass.SIGNED_32; }
				else if(builtin_class.equals("32-bit floating point")) { builtin.builtin_class = AST.BuiltInClass.FLOAT_32; }
				else if(builtin_class.equals("64-bit unsigned integer")) { builtin.builtin_class = AST.BuiltInClass.UNSIGNED_64; }
				else if(builtin_class.equals("64-bit signed integer")) { builtin.builtin_class = AST.BuiltInClass.SIGNED_64; }
				else if(builtin_class.equals("64-bit floating point")) { builtin.builtin_class = AST.BuiltInClass.FLOAT_64; }
				else if(builtin_class.equals("128-bit unsigned integer")) { builtin.builtin_class = AST.BuiltInClass.UNSIGNED_128; }
				else if(builtin_class.equals("128-bit signed integer")) { builtin.builtin_class = AST.BuiltInClass.SIGNED_128; }
				else if(builtin_class.equals("128-bit integer")) { builtin.builtin_class = AST.BuiltInClass.UNQUALIFIED_128; }
				else if(builtin_class.equals("128-bit floating point")) { builtin.builtin_class = AST.BuiltInClass.FLOAT_128; }
				else { throw new JsonParseException("Bad builtin class."); }
				node = builtin;
			} else if(descriptor.equals("compound_statement")) {
				AST.CompoundStatement compound = new AST.CompoundStatement();
				for(JsonElement child : object.get("children").getAsJsonArray()) {
					compound.children.add(context.deserialize(child, AST.Node.class));
				}
				node = compound;
			} else if(descriptor.equals("function_definition")) {
				AST.FunctionDefinition function = new AST.FunctionDefinition();
				function.type = context.deserialize(object.get("type"), AST.Node.class);
				function.body = context.deserialize(object.get("body"), AST.Node.class);
				node = function;
			} else if(descriptor.equals("function_type")) {
				AST.FunctionType function_type = new AST.FunctionType();
				function_type.return_type = context.deserialize(object.get("return_type"), AST.Node.class);
				if(object.has("parameters")) {
					for(JsonElement parameter : object.get("parameters").getAsJsonArray()) {
						function_type.parameters.add(context.deserialize(parameter, AST.Node.class));
					}
				}
				node = function_type;
			} else if(descriptor.equals("enum")) {
				AST.InlineEnum inline_enum = new AST.InlineEnum();
				for(JsonElement src : object.get("constants").getAsJsonArray()) {
					AST.EnumConstant dest = new AST.EnumConstant();
					JsonObject src_object = src.getAsJsonObject();
					dest.value = src_object.get("value").getAsInt();
					dest.name = src_object.get("name").getAsString();
					inline_enum.constants.add(dest);
				}
				node = inline_enum;
			} else if(descriptor.equals("struct") || descriptor.equals("union")) {
				AST.InlineStructOrUnion struct_or_union = new AST.InlineStructOrUnion();
				struct_or_union.is_struct = descriptor.equals("struct");
				if(struct_or_union.is_struct) {
					for(JsonElement base_class : object.get("base_classes").getAsJsonArray()) {
						AST.BaseClass dest = new AST.BaseClass();
						JsonObject src = base_class.getAsJsonObject();
						dest.offset = src.get("offset").getAsInt();
						dest.type = context.deserialize(src.get("type"), AST.Node.class);
						struct_or_union.base_classes.add(dest);
					}
				}
				for(JsonElement field : object.get("fields").getAsJsonArray()) {
					struct_or_union.fields.add(context.deserialize(field, AST.Node.class));
				}
				for(JsonElement member_function : object.get("member_functions").getAsJsonArray()) {
					struct_or_union.member_functions.add(context.deserialize(member_function, AST.Node.class));
				}
				node = struct_or_union;
			} else if(descriptor.equals("pointer")) {
				AST.Pointer pointer = new AST.Pointer();
				pointer.value_type = context.deserialize(object.get("value_type"), AST.Node.class);
				node = pointer;
			} else if(descriptor.equals("reference")) {
				AST.Reference reference = new AST.Reference();
				reference.value_type = context.deserialize(object.get("value_type"), AST.Node.class);
				node = reference;
			} else if(descriptor.equals("source_file")) {
				AST.SourceFile source_file = new AST.SourceFile();
				source_file.path = object.get("path").getAsString();
				source_file.text_address = object.get("text_address").getAsInt();
				for(JsonElement type_object : object.get("types").getAsJsonArray()) {
					source_file.types.add(context.deserialize(type_object, AST.Node.class));
				}
				for(JsonElement function_object : object.get("functions").getAsJsonArray()) {
					source_file.functions.add(context.deserialize(function_object, AST.Node.class));
				}
				for(JsonElement global_object : object.get("globals").getAsJsonArray()) {
					source_file.globals.add(context.deserialize(global_object, AST.Node.class));
				}
				JsonElement stabs_type_number_to_deduplicated_type_index = object.get("stabs_type_number_to_deduplicated_type_index");
				for(Map.Entry<String, JsonElement> entry : stabs_type_number_to_deduplicated_type_index.getAsJsonObject().entrySet()) {
					int stabs_type_number = Integer.parseInt(entry.getKey());
					int type_index = entry.getValue().getAsInt();
					source_file.stabs_type_number_to_deduplicated_type_index.put(stabs_type_number, type_index);
				}
				node = source_file;
			} else if(descriptor.equals("type_name")) {
				AST.TypeName type_name = new AST.TypeName();
				type_name.type_name = object.get("type_name").getAsString();
				if(object.has("referenced_file_index")) {
					type_name.referenced_file_index = object.get("referenced_file_index").getAsInt();
				}
				if(object.has("referenced_stabs_type_number")) {
					type_name.referenced_stabs_type_number = object.get("referenced_stabs_type_number").getAsInt();
				}
				node = type_name;
			} else if(descriptor.equals("variable")) {
				AST.Variable variable = new AST.Variable();
				String variable_class = object.get("class").getAsString();
				if(variable_class.equals("global")) {
					variable.variable_class = AST.VariableClass.GLOBAL;
				} else if(variable_class.equals("local")) {
					variable.variable_class = AST.VariableClass.LOCAL;
				} else if(variable_class.equals("parameter")) {
					variable.variable_class = AST.VariableClass.PARAMETER;
				} else {
					throw new JsonParseException("Bad variable class: " + variable_class);
				}
				variable.storage = read_variable_storage(object.get("storage").getAsJsonObject());
				if(object.has("block_low")) {
					variable.block_low = object.get("block_low").getAsInt();
				}
				if(object.has("block_high")) {
					variable.block_high = object.get("block_high").getAsInt();
				}
				variable.type = context.deserialize(object.get("type"), AST.Node.class);
				node = variable;
			} else {
				throw new JsonParseException("Bad node descriptor: " + descriptor);
			}
			read_common(node, object);
			return node;
		}
		
		private void read_common(AST.Node dest, JsonObject src) throws JsonParseException {
			if(src.has("name")) {
				dest.name = src.get("name").getAsString();
			}
			if(src.has("storage_class")) {
				String storage_class = src.get("storage_class").getAsString();
				if(storage_class.equals("typedef")) {
					dest.storage_class = AST.StorageClass.TYPEDEF;
				} else if(storage_class.equals("extern")) {
					dest.storage_class = AST.StorageClass.EXTERN;
				} else if(storage_class.equals("static")) {
					dest.storage_class = AST.StorageClass.STATIC;
				} else if(storage_class.equals("auto")) {
					dest.storage_class = AST.StorageClass.AUTO;
				} else if(storage_class.equals("register")) {
					dest.storage_class = AST.StorageClass.REGISTER;
				}
			}
			if(src.has("relative_offset_bytes")) {
				dest.relative_offset_bytes = src.get("relative_offset_bytes").getAsInt();
			}
			if(src.has("absolute_offset_bytes")) {
				dest.absolute_offset_bytes = src.get("absolute_offset_bytes").getAsInt();
			}
			if(src.has("bitfield_offset_bits")) {
				dest.bitfield_offset_bits = src.get("bitfield_offset_bits").getAsInt();
			}
			if(src.has("size_bits")) {
				dest.size_bits = src.get("size_bits").getAsInt();
			}
			if(src.has("order")) {
				dest.order = src.get("order").getAsInt();
			}
			if(src.has("files")) {
				dest.first_file = src.get("files").getAsJsonArray().get(0).getAsInt();
			}
			if(src.has("conflict")) {
				dest.conflict = src.get("conflict").getAsBoolean();
			}
			if(src.has("stabs_type_number")) {
				dest.stabs_type_number = src.get("stabs_type_number").getAsInt();
			}
		}
		
		private AST.VariableStorage read_variable_storage(JsonObject src) {
			AST.VariableStorage dest = new AST.VariableStorage();
			String location = src.get("location").getAsString();
			if(location.equals("bss")) {
				dest.location = AST.VariableStorageLocation.BSS;
				dest.bss_or_data_address = src.get("address").getAsInt();
			} else if(location.equals("data")) {
				dest.location = AST.VariableStorageLocation.DATA;
				dest.bss_or_data_address = src.get("address").getAsInt();
			} else if(location.equals("register")) {
				dest.location = AST.VariableStorageLocation.REGISTER;
				dest.register = src.get("register").getAsString();
				dest.register_class = src.get("register_class").getAsString();
				dest.dbx_register_number = src.get("dbx_register_number").getAsInt();
				dest.register_index_relative = src.get("register_index").getAsInt();
			} else if(location.equals("stack")) {
				dest.location = AST.VariableStorageLocation.STACK;
				dest.stack_pointer_offset = src.get("stack_offset").getAsInt();
			} else {
				throw new JsonParseException("Bad variable location: " + location);
			}
			return dest;
		}
	}
}
