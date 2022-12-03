#include "ccc/ccc.h"

using namespace ccc;

int main(int argc, char** argv) {
	verify(argc == 2, "Incorrect number of arguments.");
	
	Module mod = loaders::read_elf_file(fs::path(argv[1]));
	std::vector<Module*> modules{&mod};
	
	ModuleSection* text = mod.lookup_section(".text");
	verify(text, "ELF contains no .text section!");
	
	u32 text_address = mod.file_offset_to_virtual_address(text->file_offset);
	std::vector<u8> bytes = read_virtual_vector(text_address, text->size, modules);
	
	for(u64 i = 0; i < text->size; i += 4) {
		mips::Insn insn(*(u32*) &bytes[i]);
		const mips::InsnInfo& info = insn.info();
		u32 insn_address = text_address + i;
		printf("%08x %s ", insn_address, info.mnemonic);
		for(const mips::FlowInfo& flow : info.data_flows) {
			if(flow.is_past_end()) {
				break;
			}
			if(flow.field != mips::InsnField::NONE) {
				if(flow.type == mips::FlowType::REG && flow.reg_class == mips::RegisterClass::GPR) {
					printf("%s ", mips::GPR_STRINGS[insn.field(flow.field) & 31]);
				}
			}
		}
		printf("\n");
	}
}
