// Decompile all functions and write the result to a file. The result can then
// be read by uncc.
//@author chaoticgd
//@category CCC

import java.io.FileWriter;

import ghidra.app.decompiler.flatapi.FlatDecompilerAPI;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;

public class CCCDecompileAllFunctions extends GhidraScript {
	public void run() throws Exception {
		FlatDecompilerAPI decompiler = new FlatDecompilerAPI(this);
		String outputPath = askString("Choose Output Path", "");
		try(FileWriter writer = new FileWriter(outputPath)) {
			for(Function function : currentProgram.getFunctionManager().getFunctions(true)) {
				writer.write("@function ");
				writer.write(Long.toHexString(function.getEntryPoint().getOffset()));
				writer.write("\n");
				try {
					writer.write(decompiler.decompile(function));
					writer.write("\n");
				} catch(Exception e) {
					println("[" + function.getName() + "] " + e.getMessage());
				}
			}
		}
	}
}
