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
		u32 insn_bytes = *(u32*) &bytes[i]; 
		mips::Insn insn(insn_bytes);
		const mips::InsnInfo& info = insn.info();
		u32 insn_address = text_address + i;
		printf("%08x:\t\t%08x %s ", insn_address, insn_bytes, info.mnemonic);
		for(s32 i = 0; i < 16 - strlen(info.mnemonic); i++) {
			printf(" ");
		}
		bool first_operand = true;
		mips::FlowType last_flow_type = mips::FlowType::IMMED;
		for(const mips::FlowInfo& flow : info.data_flows) {
			if(flow.is_past_end()) {
				break;
			}
			if(flow.field != mips::InsnField::NONE) {
				bool is_mem_access = last_flow_type == mips::FlowType::IMMED && flow.type == mips::FlowType::REG;
				if(!first_operand) {
					if(is_mem_access) {
						printf("(");
					} else {
						printf(", ");
					}
				}
				u32 field = insn.field(flow.field);
				switch(flow.type) {
					case mips::FlowType::IMMED: {
						if(flow.field == mips::InsnField::IMMED) {
							s16 f = (s16) field;
							printf("%s0x%x", (f < 0) ? "-" : "", abs(f));
						} else {
							printf("0x%x", field);
						}
						break;
					}
					case mips::FlowType::REG: {
						assert(field < mips::REGISTER_STRING_TABLE_SIZES[(s32) flow.reg_class]);
						printf("%s", mips::REGISTER_STRING_TABLES[(s32) flow.reg_class][insn.field(flow.field)]);
						break;
					}
					case mips::FlowType::FIXED_REG: {
						assert(0);
					}
				}
				if(!first_operand && is_mem_access) {
					printf(")");
				}
				first_operand = false;
				last_flow_type = flow.type;
			}
		}
		printf("\n");
	}
}
