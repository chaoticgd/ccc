#include "ccc/ccc.h"

using namespace ccc;

int main(int argc, char** argv) {
	verify(argc == 2, "Incorrect number of arguments.");
	
	Module mod = loaders::read_elf_file(fs::path(argv[1]));
	std::vector<Module*> modules{&mod};
	
	ModuleSection* text = mod.lookup_section(".text");
	verify(text, "ELF contains no .text section!");
	
	u32 text_address = mod.file_offset_to_virtual_address(text->file_offset);
	std::vector<mips::Insn> insns = read_virtual_vector<mips::Insn>(text_address, text->size / 4, modules);
	std::map<u32, ir::Function> funcs = ir::scan_for_functions(text_address, insns);
	
	for(u64 i = 0; i < text->size / 4; i++) {
		mips::Insn insn = insns[i];
		const mips::InsnInfo& info = insn.info();
		u32 insn_address = text_address + i;
		
		auto func = funcs.find(insn_address);
		if(func != funcs.end()) {
			printf("\t\t%s:\n", func->second.name.c_str());
		}
		
		printf("%08x:\t\t%08x %s ", insn_address, insn.value, info.mnemonic);
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
						printf(",");
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
