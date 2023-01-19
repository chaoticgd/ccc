// Imports symbols from JSON files written out by stdump.
//@author chaoticgd
//@category _NEW_
import java.io.FileReader;
import java.lang.reflect.Type;
import java.util.*;
import com.google.gson.*;
import com.google.gson.stream.JsonReader;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.services.ConsoleService;
import ghidra.program.model.lang.*;
import ghidra.program.model.data.*;
import ghidra.program.model.data.DataUtilities.ClearDataMode;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;

public class ImportStdumpSymbolsIntoGhidra extends GhidraScript {
	public void run() throws Exception {
		ImporterState importer = new ImporterState();
		importer.program_type_manager = currentProgram.getDataTypeManager();
		importer.console = state.getTool().getService(ConsoleService.class);
		
		String json_path = askString("Enter Path", "Path to .json file:");
		JsonReader reader = new JsonReader(new FileReader(json_path));
		importer.emit_line_numbers = askYesNo("Configure Line Numbers", "Emit source line numbers as EOL comments?");
		importer.mark_inlined_code = askYesNo("Configure Inlined Code", "Mark inlined code using pre comments?");
		
		GsonBuilder gson_builder = new GsonBuilder();
		gson_builder.registerTypeAdapter(ParsedJsonFile.class, new JsonFileDeserializer());
		gson_builder.registerTypeAdapter(AST.Node.class, new NodeDeserializer());
		Gson gson = gson_builder.create();
		importer.ast = gson.fromJson(reader, ParsedJsonFile.class);
		
		import_types(importer);
		import_functions(importer);
		import_globals(importer);
	}
	
	// *************************************************************************

	public class ImporterState {
		DataTypeManager program_type_manager = null;
		ConsoleService console;
		
		ParsedJsonFile ast;
		ArrayList<DataType> types = new ArrayList<>(); // (data type, size in bytes)
		ArrayList<HashMap<Integer, Integer>> stabs_type_number_to_deduplicated_type_index = new ArrayList<>();
		HashMap<String, Integer> type_name_to_deduplicated_type_index = new HashMap<>();
		int current_type = -1;
		
		boolean emit_line_numbers = false;
		boolean mark_inlined_code = false;
		boolean enable_broken_local_variables = false;
	}
	
	public void import_types(ImporterState importer) throws Exception {
		// Gather information required for type lookup.
		for(AST.Node node : importer.ast.files) {
			AST.SourceFile file = (AST.SourceFile) node;
			importer.stabs_type_number_to_deduplicated_type_index.add(file.stabs_type_number_to_deduplicated_type_index);
		}

		for(int i = 0; i < importer.ast.deduplicated_types.size(); i++) {
			AST.Node node = importer.ast.deduplicated_types.get(i);
			if(node.name != null && !node.name.isEmpty()) {
				importer.type_name_to_deduplicated_type_index.put(node.name, i);
			}
		}
		
		// Create all the top-level enums, structs and unions first.
		for(int i = 0; i < importer.ast.deduplicated_types.size(); i++) {
			AST.Node node = importer.ast.deduplicated_types.get(i);
			if(node instanceof AST.InlineEnum) {
				AST.InlineEnum inline_enum = (AST.InlineEnum) node;
				DataType type = inline_enum.create_type(importer);
				importer.types.add(importer.program_type_manager.addDataType(type, null));
			} else if(node instanceof AST.InlineStructOrUnion) {
				AST.InlineStructOrUnion struct_or_union = (AST.InlineStructOrUnion) node;
				DataType type = struct_or_union.create_empty(importer);
				importer.types.add(importer.program_type_manager.addDataType(type, null));
			} else {
				importer.types.add(null);
			}
		}
		
		// Fill in the structs and unions recursively.
		for(int i = 0; i < importer.ast.deduplicated_types.size(); i++) {
			importer.current_type = i;
			AST.Node node = importer.ast.deduplicated_types.get(i);
			if(node instanceof AST.InlineStructOrUnion) {
				AST.InlineStructOrUnion struct_or_union = (AST.InlineStructOrUnion) node;
				DataType type = importer.types.get(i);
				struct_or_union.fill(type, importer);
				importer.types.set(i, type);
			}
		}
	}
	
	
	// *************************************************************************
	
	public void import_functions(ImporterState importer) throws Exception {
		AddressSpace space = getAddressFactory().getDefaultAddressSpace();
		SymbolTable symbol_table = currentProgram.getSymbolTable();
		for(AST.Node node : importer.ast.files) {
			AST.SourceFile source_file = (AST.SourceFile) node;
			for(AST.Node function_node : source_file.functions) {
				AST.FunctionDefinition def = (AST.FunctionDefinition) function_node;
				AST.FunctionType type = (AST.FunctionType) def.type;
				if(def.address_range.valid()) {
					// Find or create the function.
					Address low = space.getAddress(def.address_range.low);
					Address high = space.getAddress(def.address_range.high - 1);
					AddressSet range = new AddressSet(low, high);
					Function function = getFunctionAt(low);
					if(function == null) {
						CreateFunctionCmd cmd;
						if(high.getOffset() < low.getOffset()) {
							cmd = new CreateFunctionCmd(new AddressSet(low), SourceType.ANALYSIS);
						} else {
							cmd = new CreateFunctionCmd(def.name, low, range, SourceType.ANALYSIS);
						}
						boolean success = cmd.applyTo(currentProgram, monitor);
						if(!success) {
							throw new Exception("Failed to create function " + def.name + ": " + cmd.getStatusMsg());
						}
						function = getFunctionAt(low);
					}
					
					// Remove spam like "gcc2_compiled." and remove the
					// existing label for the function name so it can be
					// reapplied below.
					Symbol[] existing_symbols = symbol_table.getSymbols(low);
					for(Symbol existing_symbol : existing_symbols) {
						String name = existing_symbol.getName();
						if(name.equals("__gnu_compiled_cplusplus") || name.equals("gcc2_compiled.") || name.equals(def.name)) {
							symbol_table.removeSymbolSpecial(existing_symbol);
						}
					}
					
					// Ghidra will sometimes find the wrong label and use it as
					// a function name e.g. "gcc2_compiled." so it's important
					// that we set the name explicitly here.
					function.setName(def.name, SourceType.ANALYSIS);
					function.setComment(source_file.path);
					
					// Specify the return type.
					if(type.return_type != null) {
						function.setReturnType(type.return_type.create_type(importer), SourceType.ANALYSIS);
					}
					
					// Add the parameters.
					if(type.parameters.size() > 0) {
						ArrayList<Variable> parameters = new ArrayList<>();
						for(int i = 0; i < type.parameters.size(); i++) {
							AST.Variable variable = (AST.Variable) type.parameters.get(i);
							DataType parameter_type = AST.replace_void_with_undefined1(variable.type.create_type(importer));
							if(parameter_type.getLength() > 16) {
								parameter_type = new PointerDataType(parameter_type);
							}
							parameters.add(new ParameterImpl(variable.name, parameter_type, currentProgram));
						}
						try {
							function.replaceParameters(parameters, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.ANALYSIS);
						} catch(VariableSizeException exception) {
							print("Failed to setup parameters for " + def.name + ": " + exception.getMessage());
						}
					}
					
					if(importer.emit_line_numbers) {
						// Add line numbers as EOL comments.
						for(AST.LineNumberPair pair : def.line_numbers) {
							setEOLComment(space.getAddress(pair.address), "Line " + Integer.toString(pair.line_number));
						}
					}
					
					if(importer.mark_inlined_code) {
						// Add comments to mark inlined code.
						boolean was_inlining = false;
						for(AST.SubSourceFile sub : def.sub_source_files) {
							boolean is_inlining = !sub.relative_path.equals(source_file.relative_path);
							if(is_inlining && !was_inlining) {
								setPreComment(space.getAddress(sub.address), "inlined from " + sub.relative_path);
							} else if(!is_inlining && was_inlining) {
								setPreComment(space.getAddress(sub.address), "end of inlined section");
							}
						was_inlining = is_inlining;
						}
					}
					
					// This is currently far too broken to be enabled by default.
					if(importer.enable_broken_local_variables) {
						// Add local variables.
						int i = 0;
						for(AST.Node child : def.locals) {
							if(child instanceof AST.Variable) {
								AST.Variable src = (AST.Variable) child;
								if(src.storage_class != AST.StorageClass.STATIC) {
									LocalVariable dest = null;
									String name = src.name + "__" + Integer.toString(i);
									DataType local_type = AST.replace_void_with_undefined1(src.type.create_type(importer));
									if(src.storage.type == AST.VariableStorageType.REGISTER) {
										int first_use = src.block_low - def.address_range.low;
										Register register = get_sleigh_register(src.storage, local_type.getLength());
										dest = new LocalVariableImpl(name, first_use, local_type, register, currentProgram, SourceType.ANALYSIS);
									} else if(src.storage.type == AST.VariableStorageType.STACK) {
										dest = new LocalVariableImpl(name, local_type, src.storage.stack_pointer_offset, currentProgram, SourceType.ANALYSIS);
									}
									function.addLocalVariable(dest, SourceType.ANALYSIS);
									i++;
								}
							}
						}
					}
				}
			}
		}
	}
	
	// *************************************************************************
	
	public void import_globals(ImporterState importer) throws Exception {
		AddressSpace space = getAddressFactory().getDefaultAddressSpace();
		for(AST.Node file_node : importer.ast.files) {
			AST.SourceFile file = (AST.SourceFile) file_node;
			for(AST.Node global_node : file.globals) {
				AST.Variable global = (AST.Variable) global_node;
				if(global.storage.global_address > -1) {
					Address address = space.getAddress(global.storage.global_address);
					DataType type = AST.replace_void_with_undefined1(global.type.create_type(importer));
					DataUtilities.createData(currentProgram, address, type, type.getLength(), false, ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
					this.createLabel(address, global.name, true);
				}
			}
		}
	}
	
	// *************************************************************************
	
	public Register get_sleigh_register(AST.VariableStorage storage, int size) throws Exception {
		if(storage.type != AST.VariableStorageType.REGISTER) {
			throw new Exception("Call to get_sleigh_register() with non-register storage information.");
		}
		String sleigh_register_name = null;
		if(storage.register_class.equals("gpr")) {
			if(size > 8) {
				sleigh_register_name = storage.register + "_qw";
			} else if(size > 4) {
				sleigh_register_name = storage.register;
			} else {
				sleigh_register_name = storage.register + "_lo";
			}
		}
		if(sleigh_register_name == null) {
			return null;
		}
		return currentProgram.getRegister(sleigh_register_name);
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
		
		public static class AddressRange {
			int low = -1;
			int high = -1;
			
			public boolean valid() {
				return low > -1;
			}
		}
		
		public static class Node {
			String prefix = ""; // Used for nested structs.
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
			public DataType create_type(ImporterState importer) throws Exception {
				throw new Exception("Method create_type() called on AST node that isn't a type.");
			}
			
			String generate_name() {
				if(conflict || name == null || name.isEmpty()) {
					return prefix + name + "__" + Integer.toString(first_file) + "_" + Integer.toString(stabs_type_number);
				}
				return name;
			}
		}
		
		public static class Array extends Node {
			Node element_type;
			int element_count;
			
			public DataType create_type(ImporterState importer) throws Exception {
				DataType element = replace_void_with_undefined1(element_type.create_type(importer));
				return new ArrayDataType(element, element_count, element.getLength());
			}
		}
		
		public static class BitField extends Node {
			Node underlying_type;
			
			public DataType create_type(ImporterState importer) throws Exception {
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
			
			public DataType create_type(ImporterState importer) throws Exception {
				switch(builtin_class) {
				case VOID:
					return VoidDataType.dataType;
				case UNSIGNED_8:
					return UnsignedCharDataType.dataType;
				case SIGNED_8:
				case UNQUALIFIED_8:
				case BOOL_8:
					return CharDataType.dataType;
				case UNSIGNED_16:
					return ShortDataType.dataType;
				case SIGNED_16:
					return UnsignedShortDataType.dataType;
				case UNSIGNED_32:
					return UnsignedIntegerDataType.dataType;
				case SIGNED_32:
					return IntegerDataType.dataType;
				case FLOAT_32:
					return FloatDataType.dataType;
				case UNSIGNED_64:
					return UnsignedLongDataType.dataType;
				case SIGNED_64:
				case FLOAT_64:
					return LongDataType.dataType;
				case UNSIGNED_128:
					return UnsignedInteger16DataType.dataType;
				case SIGNED_128:
					return Integer16DataType.dataType;
				case UNQUALIFIED_128:
				case FLOAT_128:
					return UnsignedInteger16DataType.dataType;
				case UNKNOWN_PROBABLY_ARRAY:
				}
				throw new Exception("Method create_type() called on unknown builtin.");
			}
		}
		
		public static class LineNumberPair {
			int address;
			int line_number;
		}
		
		public static class SubSourceFile {
			int address;
			String relative_path;
		}
		
		public static class FunctionDefinition extends Node {
			AddressRange address_range = new AddressRange();
			Node type;
			ArrayList<Variable> locals = new ArrayList<>();
			ArrayList<LineNumberPair> line_numbers = new ArrayList<>();
			ArrayList<SubSourceFile> sub_source_files = new ArrayList<>();
		}
		
		public static class FunctionType extends Node {
			Node return_type = null;
			ArrayList<Node> parameters = new ArrayList<Node>();
			String modifiers;
			boolean is_constructor = false;
			
			public DataType create_type(ImporterState importer) throws Exception {
				return Undefined1DataType.dataType;
			}
		}
		
		public static class EnumConstant {
			int value;
			String name;
		}
		
		public static class InlineEnum extends Node {
			ArrayList<EnumConstant> constants = new ArrayList<EnumConstant>();
			
			public DataType create_type(ImporterState importer) throws Exception {
				EnumDataType type = new EnumDataType(generate_name(), 4);
				for(EnumConstant constant : constants) {
					type.add(constant.name, constant.value);
				}
				return type;
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
			
			public DataType create_type(ImporterState importer) throws Exception {
				DataType result = create_empty(importer);
				fill(result, importer);
				return result;
			}
			
			public DataType create_empty(ImporterState importer) {
				String type_name = generate_name();
				int size_bytes = size_bits / 8;
				DataType type;
				if(is_struct) {
					type = new StructureDataType(type_name, size_bytes, importer.program_type_manager);
				} else {
					type = new UnionDataType(type_name);
				}
				return type;
			}
			
			public void fill(DataType dest, ImporterState importer) throws Exception {
				if(is_struct) {
					Structure type = (Structure) dest;
					for(int i = 0; i < base_classes.size(); i++) {
						BaseClass base_class = base_classes.get(i);
						DataType base_type = replace_void_with_undefined1(base_class.type.create_type(importer));
						boolean is_beyond_end = base_class.offset + base_type.getLength() > size_bits / 8;
						if(!is_beyond_end && base_class.offset > -1) {
							type.replaceAtOffset(base_class.offset, base_type, base_type.getLength(), "base_class_" + Integer.toString(i), "");
						}
					}
					for(AST.Node node : fields) {
						if(node.storage_class != StorageClass.STATIC) {
							// Currently we don't try to import bit fields.
							boolean is_bitfield = node instanceof BitField;
							node.prefix += name + "__";
							DataType field = replace_void_with_undefined1(node.create_type(importer));
							boolean is_beyond_end = node.relative_offset_bytes + field.getLength() > size_bits / 8;
							if(!is_bitfield && !is_beyond_end) {
								if(node.relative_offset_bytes + field.getLength() <= size_bits / 8) {
									type.replaceAtOffset(node.relative_offset_bytes, field, field.getLength(), node.name, "");
								}
							}
						}
					}
				} else {
					Union type = (Union) dest;
					for(AST.Node node : fields) {
						if(node.storage_class != StorageClass.STATIC) {
							DataType field = replace_void_with_undefined1(node.create_type(importer));
							type.add(field, field.getLength(), node.name, "");
						}
					}
				}
			}
		}
		
		public static class Pointer extends Node {
			Node value_type;
			
			public int size_bytes(ImporterState importer) throws Exception {
				return 4;
			}
			
			public DataType create_type(ImporterState importer) throws Exception {
				return new PointerDataType(value_type.create_type(importer));
			}
		}
		
		public static class Reference extends Node {
			Node value_type;
			
			public int size_bytes(ImporterState importer) throws Exception {
				return 4;
			}
			
			public DataType create_type(ImporterState importer) throws Exception {
				return new PointerDataType(value_type.create_type(importer));
			}
		}
		
		public static class SourceFile extends Node {
			String path;
			String relative_path;
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
			
			public DataType create_type(ImporterState importer) throws Exception {
				if(type_name.equals("void")) {
					return VoidDataType.dataType;
				}
				Integer index = null;
				if(referenced_file_index > -1 && referenced_stabs_type_number > -1) {
					// Lookup the type by its STABS type number. This path
					// ensures that the correct type is found even if multiple
					// types have the same name.
					HashMap<Integer, Integer> index_lookup = importer.stabs_type_number_to_deduplicated_type_index.get(referenced_file_index);
					index = index_lookup.get(referenced_stabs_type_number);
				}
				if(index == null) {
					// For STABS cross references, no type number is provided,
					// so we must lookup the type by name instead. This is
					// riskier but I think it's the best we can really do.
					index = importer.type_name_to_deduplicated_type_index.get(type_name);
				}
				if(index == null) {
					importer.console.print("Type lookup failed: " + type_name + "\n");
					return Undefined1DataType.dataType;
				}
				DataType type = importer.types.get(index);
				if(type == null) {
					AST.Node node = importer.ast.deduplicated_types.get(index);
					if(index == importer.current_type || node == this) {
						importer.console.print("Circular type definition: " + type_name + "\n");
						return Undefined1DataType.dataType;
					}
					if(node instanceof AST.InlineStructOrUnion) {
						importer.console.print("Bad type name referencing struct or union: " + type_name + "\n");
						return Undefined1DataType.dataType;
					}
					type = node.create_type(importer);
					importer.types.set(index, type);
				}
				return type;
			}
		}
		
		public static enum VariableClass {
			GLOBAL,
			LOCAL,
			PARAMETER
		}
		
		public static enum VariableStorageType {
			GLOBAL,
			REGISTER,
			STACK
		}
		
		public static class VariableStorage {
			VariableStorageType type;
			int global_address = -1;
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
		
		public static DataType replace_void_with_undefined1(DataType type) {
			if(type.isEquivalent(VoidDataType.dataType)) {
				return Undefined1DataType.dataType;
			}
			return type;
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
			} else if(descriptor.equals("function_definition")) {
				AST.FunctionDefinition function = new AST.FunctionDefinition();
				if(object.has("address_range")) {
					function.address_range = read_address_range(object.get("address_range").getAsJsonObject());
				}
				function.type = context.deserialize(object.get("type"), AST.Node.class);
				for(JsonElement local : object.get("locals").getAsJsonArray()) {
					function.locals.add(context.deserialize(local.getAsJsonObject(), AST.Node.class));
				}
				for(JsonElement pair : object.get("line_numbers").getAsJsonArray()) {
					JsonArray src = pair.getAsJsonArray();
					AST.LineNumberPair dest = new AST.LineNumberPair();
					dest.address = src.get(0).getAsInt();
					dest.line_number = src.get(1).getAsInt();
					function.line_numbers.add(dest);
				}
				for(JsonElement sub : object.get("sub_source_files").getAsJsonArray()) {
					JsonObject src = sub.getAsJsonObject();
					AST.SubSourceFile dest = new AST.SubSourceFile();
					dest.address = src.get("address").getAsInt();
					dest.relative_path = src.get("path").getAsString();
					function.sub_source_files.add(dest);
				}
				node = function;
			} else if(descriptor.equals("function_type")) {
				AST.FunctionType function_type = new AST.FunctionType();
				if(object.has("return_type")) {
					function_type.return_type = context.deserialize(object.get("return_type"), AST.Node.class);
				}
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
				source_file.relative_path = object.get("relative_path").getAsString();
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
			String type = src.get("type").getAsString();
			if(type.equals("global")) {
				dest.type = AST.VariableStorageType.GLOBAL;
				dest.global_address = src.get("global_address").getAsInt();
			} else if(type.equals("register")) {
				dest.type = AST.VariableStorageType.REGISTER;
				dest.register = src.get("register").getAsString();
				dest.register_class = src.get("register_class").getAsString();
				dest.dbx_register_number = src.get("dbx_register_number").getAsInt();
				dest.register_index_relative = src.get("register_index").getAsInt();
			} else if(type.equals("stack")) {
				dest.type = AST.VariableStorageType.STACK;
				dest.stack_pointer_offset = src.get("stack_offset").getAsInt();
			} else {
				throw new JsonParseException("Bad variable storage type: " + type);
			}
			return dest;
		}
		
		private AST.AddressRange read_address_range(JsonObject src) {
			AST.AddressRange dest = new AST.AddressRange();
			dest.low = src.get("low").getAsInt();
			dest.high = src.get("high").getAsInt();
			return dest;
		}
	}
}
