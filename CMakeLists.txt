cmake_minimum_required(VERSION 3.14)
project(ccc)

enable_testing()

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
	message(FATAL_ERROR "In-tree build detected. You should do an out-of-tree build instead:\n\tcmake -B bin/")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(BUILD_STATIC)
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
	set(BUILD_SHARED_LIBS OFF)
	set(CMAKE_EXE_LINKER_FLAGS "-static")
endif()

if(MSVC)
	# TODO
else()
	set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall")
	set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall")
	add_link_options($<$<CONFIG:RELEASE>:-s>)
endif()

include(cmake/version_finder.cmake)

add_library(ccc STATIC
	src/ccc/ast.cpp
	src/ccc/ast.h
	src/ccc/ast_json.cpp
	src/ccc/ast_json.h
	src/ccc/data_refinement.cpp
	src/ccc/data_refinement.h
	src/ccc/dependency.cpp
	src/ccc/dependency.h
	src/ccc/elf.cpp
	src/ccc/elf.h
	src/ccc/elf_symtab.cpp
	src/ccc/elf_symtab.h
	src/ccc/mdebug_analysis.cpp
	src/ccc/mdebug_analysis.h
	src/ccc/mdebug_importer.cpp
	src/ccc/mdebug_importer.h
	src/ccc/mdebug_section.cpp
	src/ccc/mdebug_section.h
	src/ccc/mdebug_symbols.cpp
	src/ccc/mdebug_symbols.h
	src/ccc/print_cpp.cpp
	src/ccc/print_cpp.h
	src/ccc/registers.cpp
	src/ccc/registers.h
	src/ccc/sndll.cpp
	src/ccc/sndll.h
	src/ccc/stabs.cpp
	src/ccc/stabs.h
	src/ccc/stabs_to_ast.cpp
	src/ccc/stabs_to_ast.h
	src/ccc/symbol_database.cpp
	src/ccc/symbol_database.h
	src/ccc/symbol_file.cpp
	src/ccc/symbol_file.h
	src/ccc/symbol_json.cpp
	src/ccc/symbol_json.h
	src/ccc/symbol_table.cpp
	src/ccc/symbol_table.h
	src/ccc/util.cpp
	src/ccc/util.h
)
target_link_libraries(ccc rapidjson)

add_library(ccc_mips STATIC
	src/mips/insn.cpp
	src/mips/insn.h
	src/mips/opcodes.h
	src/mips/tables.cpp
	src/mips/tables.h
)

add_library(ccc_platform STATIC
	src/platform/file.cpp
	src/platform/file.h
)

set(TEST_SOURCES
	test/ccc/symbol_database_tests.cpp
)

add_executable(demangle src/demangle.cpp)
target_link_libraries(demangle ccc demanglegnu ccc_versioninfo)

add_executable(objdump src/objdump.cpp)
target_link_libraries(objdump ccc ccc_mips ccc_platform ccc_versioninfo)

add_executable(stdump src/stdump.cpp)
target_link_libraries(stdump ccc ccc_platform ccc_versioninfo)
add_test(NAME stdump_test COMMAND stdump test ${CMAKE_SOURCE_DIR}/test)

add_executable(uncc src/uncc.cpp)
target_link_libraries(uncc ccc ccc_platform ccc_versioninfo demanglegnu)

add_executable(tests src/tests.cpp ${TEST_SOURCES})
target_include_directories(tests PUBLIC src/)
target_link_libraries(tests ccc ccc_platform ccc_versioninfo gtest)
add_test(NAME tests COMMAND tests ${CMAKE_SOURCE_DIR}/testdata)

if(WIN32)
	target_sources(demangle PUBLIC ccc.manifest)
	target_sources(objdump PUBLIC ccc.manifest)
	target_sources(stdump PUBLIC ccc.manifest)
	target_sources(uncc PUBLIC ccc.manifest)
	target_sources(tests PUBLIC ccc.manifest)
endif()

add_subdirectory(thirdparty/demanglegnu)
add_subdirectory(thirdparty/googletest)
add_subdirectory(thirdparty/rapidjson)

# All the files to be included in a release.
set(RELEASE_CONTENTS
	$<TARGET_FILE:demangle>
	$<TARGET_FILE:objdump>
	$<TARGET_FILE:stdump>
	$<TARGET_FILE:uncc>
	"${CMAKE_SOURCE_DIR}/ghidra_scripts/CCCDecompileAllFunctions.java"
	"${CMAKE_SOURCE_DIR}/README.md"
	"${CMAKE_SOURCE_DIR}/License.txt"
	"${CMAKE_SOURCE_DIR}/thirdparty/demanglegnu/DemanglerLicense.txt"
	"${CMAKE_SOURCE_DIR}/thirdparty/demanglegnu/DemanglerLicenseSupplementGPL.txt"
	"${CMAKE_SOURCE_DIR}/thirdparty/googletest/GoogleTestLicense.txt"
	"${CMAKE_SOURCE_DIR}/thirdparty/rapidjson/RapidJSONLicense.txt"
)
include(cmake/zip_crafter.cmake)
