// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

#include "ccc/ccc.h"
#include "mips/insn.h"
#include "platform/file.h"

using namespace ccc;

int main(int argc, char** argv)
{
	CCC_EXIT_IF_FALSE(argc == 2, "Incorrect number of arguments.");
	
	fs::path input_path(argv[1]);
	Result<std::vector<u8>> image = platform::read_binary_file(input_path);
	CCC_EXIT_IF_ERROR(image);
	
	Result<ElfFile> elf = ElfFile::parse(std::move(*image));
	CCC_EXIT_IF_ERROR(elf);
	
	const ElfSection* text = elf->lookup_section(".text");
	CCC_EXIT_IF_FALSE(text, "ELF contains no .text section!");
	
	std::optional<u32> text_address = elf->file_offset_to_virtual_address(text->header.offset);
	CCC_EXIT_IF_FALSE(text_address.has_value(), "Failed to translate file offset to virtual address.");
	
	Result<std::span<const mips::Insn>> insns = elf->get_array_virtual<mips::Insn>(*text_address, text->header.size / 4);
	CCC_EXIT_IF_ERROR(insns);
	
	for (u32 i = 0; i < text->header.size / 4; i++) {
		mips::Insn insn = (*insns)[i];
		const mips::InsnInfo& info = insn.info();
		u32 insn_address = *text_address + i;
		
		printf("%08x:\t\t%08x %s ", insn_address, insn.value, info.mnemonic);
		for (s32 i = 0; i < 16 - (s32) strlen(info.mnemonic); i++) {
			printf(" ");
		}
		bool first_operand = true;
		mips::FlowType last_flow_type = mips::FlowType::IMMED;
		for (const mips::FlowInfo& flow : info.data_flows) {
			if (flow.is_past_end()) {
				break;
			}
			if (flow.field != mips::InsnField::NONE) {
				bool is_mem_access = last_flow_type == mips::FlowType::IMMED && flow.type == mips::FlowType::REG;
				if (!first_operand) {
					if (is_mem_access) {
						printf("(");
					} else {
						printf(",");
					}
				}
				u32 field = insn.field(flow.field);
				switch (flow.type) {
					case mips::FlowType::IMMED: {
						if (flow.field == mips::InsnField::IMMED) {
							s16 f = (s16) field;
							printf("%s0x%x", (f < 0) ? "-" : "", abs(f));
						} else {
							printf("0x%x", field);
						}
						break;
					}
					case mips::FlowType::REG: {
						if (field < mips::REGISTER_STRING_TABLE_SIZES[(s32) flow.reg_class]) {
							printf("%s", mips::REGISTER_STRING_TABLES[(s32) flow.reg_class][insn.field(flow.field)]);
						} else {
							printf("error");
						}
						break;
					}
					case mips::FlowType::FIXED_REG: {
						CCC_ASSERT(0);
					}
				}
				if (!first_operand && is_mem_access) {
					printf(")");
				}
				first_operand = false;
				last_flow_type = flow.type;
			}
		}
		printf("\n");
	}
}
