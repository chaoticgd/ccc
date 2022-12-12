#include "ccc/ccc.h"

#include <algorithm>
#include <set>

using namespace ccc;

void print_address(const char* name, u64 address) {
	fprintf(stderr, "%32s @ 0x%08lx\n", name, address);
}

enum class OutputMode {
	PRINT_CPP,
	PRINT_SYMBOLS,
	LIST_FILES,
	HELP,
	BAD_COMMAND
};

enum Flags {
	NO_FLAGS = 0,
	FLAG_PER_FILE = (1 << 0),
	FLAG_VERBOSE = (1 << 1),
	FLAG_OMIT_MEMBER_FUNCTIONS = (1 << 2),
	FLAG_INCLUDE_GENERATED_FUNCTIONS = (1 << 3)
};

struct Options {
	OutputMode mode = OutputMode::BAD_COMMAND;
	fs::path input_file;
	u32 flags;
};

static void print_deduplicated(const SymbolTable& symbol_table, Options options);
static std::vector<std::unique_ptr<ast::Node>> build_deduplicated_ast(std::vector<std::vector<StabsSymbol>>& symbols, const SymbolTable& symbol_table);
static void print_per_file(const SymbolTable& symbol_table, Options options);
static void print_cpp(SymbolTable& symbol_table, const Options& options);
static void filter_ast_by_flags(ast::Node& ast_node, u32 flags);
static void print_symbols(SymbolTable& symbol_table);
static void list_files(SymbolTable& symbol_table);
static SymbolTable read_symbol_table(const fs::path& input_file);
static Options parse_args(int argc, char** argv);
static void print_help();

int main(int argc, char** argv) {
	Options options = parse_args(argc, argv);
	switch(options.mode) {
		case OutputMode::PRINT_CPP: {
			SymbolTable symbol_table = read_symbol_table(options.input_file);
			print_cpp(symbol_table, options);
			return 0;
		}
		case OutputMode::PRINT_SYMBOLS: {
			SymbolTable symbol_table = read_symbol_table(options.input_file);
			print_symbols(symbol_table);
			return 0;
		}
		case OutputMode::LIST_FILES: {
			SymbolTable symbol_table = read_symbol_table(options.input_file);
			return 0;
		}
		case OutputMode::HELP: {
			print_help();
			return 0;
		}
		case OutputMode::BAD_COMMAND: {
			print_help();
			return 1;
		}
	}
}

static void print_cpp(SymbolTable& symbol_table, const Options& options) {
	if((options.flags & FLAG_PER_FILE) == 0) {
		std::vector<std::vector<StabsSymbol>> symbols;
		std::set<std::pair<std::string, RangeClass>> builtins;
		std::vector<std::pair<std::string, std::vector<std::unique_ptr<ast::Node>>>> per_file_ast;
		for(const SymFileDescriptor& fd : symbol_table.files) {
			std::vector<StabsSymbol>& per_file_symbols = symbols.emplace_back(parse_stabs_symbols(fd.symbols));
			const std::map<s32, const StabsType*> types = enumerate_numbered_types(per_file_symbols);
			const std::set<std::pair<std::string, RangeClass>> per_file_builtins = ast::symbols_to_builtins(per_file_symbols);
			for(auto& builtin : per_file_builtins) {
				builtins.emplace(builtin);
			}
			per_file_ast.emplace_back(fd.name, ast::symbols_to_ast(per_file_symbols, types));
		}
		
		for(auto& [file, per_file_ast_nodes] : per_file_ast) {
			for(std::unique_ptr<ast::Node>& ast_node : per_file_ast_nodes) {
				filter_ast_by_flags(*ast_node.get(), options.flags);
			}
		}
		
		std::vector<std::vector<std::unique_ptr<ast::Node>>> ast_nodes = deduplicate_ast(per_file_ast);
		
		// The ast_nodes variable groups types by their name, so duplicates are
		// stored together. We flatten these into a single list for printing.
		std::vector<std::unique_ptr<ast::Node>> flat_ast_nodes;
		for(std::vector<std::unique_ptr<ast::Node>>& versions : ast_nodes) {
			bool warn_duplicates = versions.size() > 1;
			for(std::unique_ptr<ast::Node>& ast_node : versions) {
				flat_ast_nodes.emplace_back(std::move(ast_node));
			}
		}
		
		if(!builtins.empty()) {
			print_cpp_comment_block_beginning(stdout, options.input_file);
			print_cpp_comment_block_builtin_types(stdout, builtins);
			printf("\n");
		}
		
		print_cpp_ast_nodes(stdout, flat_ast_nodes, options.flags & FLAG_VERBOSE);
	} else {
		print_cpp_comment_block_beginning(stdout, options.input_file);
		printf("\n");
		for(const SymFileDescriptor& fd : symbol_table.files) {
			const std::vector<StabsSymbol> symbols = parse_stabs_symbols(fd.symbols);
			const std::map<s32, const StabsType*> types = enumerate_numbered_types(symbols);
			const std::set<std::pair<std::string, RangeClass>> builtins = ast::symbols_to_builtins(symbols);
			std::vector<std::unique_ptr<ast::Node>> ast_nodes = ast::symbols_to_ast(symbols, types);
			
			for(std::unique_ptr<ast::Node>& ast_node : ast_nodes) {
				filter_ast_by_flags(*ast_node.get(), options.flags);
			}
			
			printf("// *****************************************************************************\n");
			printf("// FILE -- %s\n", fd.name.c_str());
			printf("// *****************************************************************************\n");
			printf("\n");
			print_cpp_comment_block_builtin_types(stdout, builtins);
			printf("\n");
			print_cpp_ast_nodes(stdout, ast_nodes, options.flags & FLAG_VERBOSE);
			printf("\n");
		}
	}
}

static void filter_ast_by_flags(ast::Node& ast_node, u32 flags) {
	switch(ast_node.descriptor) {
		case ast::NodeDescriptor::ARRAY:
		case ast::NodeDescriptor::BITFIELD:
		case ast::NodeDescriptor::FUNCTION:
		case ast::NodeDescriptor::INLINE_ENUM: {
			break;
		}
		case ast::NodeDescriptor::INLINE_STRUCT_OR_UNION: {
			auto& struct_or_union = ast_node.as<ast::InlineStructOrUnion>();
			if(flags & FLAG_OMIT_MEMBER_FUNCTIONS) {
				struct_or_union.member_functions.clear();
			} else if(!(flags & FLAG_INCLUDE_GENERATED_FUNCTIONS)) {
				bool default_number_of_function =
					struct_or_union.member_functions.size() == 3;
				std::string name_no_template_args =
					ast_node.name.substr(0, ast_node.name.find("<"));
				for(size_t i = 0; i < struct_or_union.member_functions.size(); i++) {
					ast::Function& function = struct_or_union.member_functions[i]->as<ast::Function>();
					bool should_remove = function.name == "__as"
						|| (default_number_of_function
							&& function.name == name_no_template_args)
						|| function.name == "operator="
						|| function.name.starts_with("$_");
					if(should_remove) {
						struct_or_union.member_functions.erase(struct_or_union.member_functions.begin() + i);
						i--;
					} else if(function.name.starts_with("$vf")) {
						function.name = "CCC_VTABLE";
					}
				}
			}
			break;
		}
		case ast::NodeDescriptor::POINTER: {
			filter_ast_by_flags(*ast_node.as<ast::Pointer>().value_type.get(), flags);
			break;
		}
		case ast::NodeDescriptor::REFERENCE: {
			filter_ast_by_flags(*ast_node.as<ast::Reference>().value_type.get(), flags);
			break;
		}
		case ast::NodeDescriptor::TYPE_NAME: {
			break;
		}
	}
}

static void print_symbols(SymbolTable& symbol_table) {
	for(SymFileDescriptor& fd : symbol_table.files) {
		printf("FILE %s:\n", fd.name.c_str());
		for(Symbol& sym : fd.symbols) {
			const char* symbol_type_str = symbol_type(sym.storage_type);
			const char* symbol_class_str = symbol_class(sym.storage_class);
			printf("\t%8x ", sym.value);
			if(symbol_type_str) {
				printf("%11s ", symbol_type_str);
			} else {
				printf("ST(%5d) ", (u32) sym.storage_type);
			}
			if(symbol_class_str) {
				printf("%6s ", symbol_class_str);
			} else if ((u32)sym.storage_class == 0) {
				printf("       ");
			} else {
				printf("SC(%2d) ", (u32) sym.storage_class);
			}
			printf("%8d %s\n", sym.index, sym.string.c_str());
		}
	}
}

static void list_files(SymbolTable& symbol_table) {
	for(const SymFileDescriptor& fd : symbol_table.files) {
		printf("%s\n", fd.name.c_str());
	}
}

static SymbolTable read_symbol_table(const fs::path& input_file) {
	Module mod = loaders::read_elf_file(input_file);
	ModuleSection* mdebug_section = mod.lookup_section(".mdebug");
	verify(mdebug_section, "No .mdebug section.");
	return parse_mdebug_section(mod, *mdebug_section);
}

static Options parse_args(int argc, char** argv) {
	Options options;
	if(argc < 2) {
		return options;
	}
	const char* command = argv[1];
	if(strcmp(command, "print_cpp") == 0) {
		options.mode = OutputMode::PRINT_CPP;
	} else if(strcmp(command, "print_symbols") == 0) {
		options.mode = OutputMode::PRINT_SYMBOLS;
	} else if(strcmp(command, "list_files") == 0) {
		options.mode = OutputMode::LIST_FILES;
	} else if(strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
		options.mode = OutputMode::HELP;
	}
	for(s32 i = 2; i < argc - 1; i++) {
		const char* flag = argv[i];
		if(strcmp(flag, "--per-file") == 0) {
			options.flags |= FLAG_PER_FILE;
		} else if(strcmp(flag, "--verbose") == 0) {
			options.flags |= FLAG_VERBOSE;
		} else if(strcmp(flag, "--omit-member-functions") == 0) {
			options.flags |= FLAG_OMIT_MEMBER_FUNCTIONS;
		} else if(strcmp(flag, "--include-generated-functions") == 0) {
			options.flags |= FLAG_INCLUDE_GENERATED_FUNCTIONS;
		} else {
			options.mode = OutputMode::BAD_COMMAND;
			return options;
		}
	}
	options.input_file = fs::path(std::string(argv[argc - 1]));
	return options;
}

static void print_help() {
	puts("stdump -- https://github.com/chaoticgd/ccc");
	puts("  MIPS/STABS symbol table parser.");
	puts("");
	puts("Commands:");
	puts("  print_cpp [options] <input file>");
	puts("    Print all the types recovered from the STABS symbols as C++.");
	puts("");
	puts("    --per-file                    Do not deduplicate types from files.");
	puts("    --verbose                     Print additional information such as the raw");
	puts("                                  STABS symbol along with each type.");
	puts("    --omit-member-functions       Do not print member functions.");
	puts("    --include-generated-functions Include member functions that are likely");
	puts("                                  auto-generated.");
	puts("");
	puts("  print_symbols <input file>");
	puts("    List all of the local symbols for each file descriptor.");
	puts("");
	puts("  list_files <input_file>");
	puts("    List the names of each of the source files.");
	puts("");
	puts("  help | --help | -h");
	puts("    Print this help message.");
}
