#include "ccc/ccc.h"

using namespace ccc;

int main(int argc, char** argv) {
	verify(argc == 2, "Incorrect number of arguments.");
	
	Program program;
	program.images.emplace_back(read_program_image(fs::path(argv[1])));
	parse_elf_file(program, 0);
	
	
	for(const ProgramSection& section : program.sections) {
		if(section.name == ".text") {
			for(u64 i = 0; i < section.size; i += 4) {
				mips::Insn insn(*(u32*) &program.images[0].bytes[section.file_offset + i]);
				const mips::InsnInfo& info = insn.info();
				printf("%s ", info.mnemonic);
				for(const mips::FlowInfo& flow : info.data_flows) {
					if(flow.is_past_end()) {
						break;
					}
					if(flow.field != mips::InsnField::NONE) {
						if(flow.type == mips::FlowType::REG && flow.reg_class == mips::RegisterClass::GPR) {
							printf("reg ");
						}
					}
				}
				printf("\n");
			}
			break;
		}
	}
}
