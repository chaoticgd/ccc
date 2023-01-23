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
if(MSVC)
	# TODO
else()
	set(CMAKE_CXX_FLAGS_DEBUG "-g")
	set(CMAKE_CXX_FLAGS_RELEASE "-O3")
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
	set(BUILD_SHARED_LIBS OFF)
	set(CMAKE_EXE_LINKER_FLAGS "-static")
endif()

add_library(ccc STATIC
	ccc/analysis.cpp
	ccc/ast.cpp
	ccc/elf.cpp
	ccc/insn.cpp
	ccc/mdebug.cpp
	ccc/module.cpp
	ccc/print_cpp.cpp
	ccc/print_json.cpp
	ccc/registers.cpp
	ccc/stabs.cpp
	ccc/symbols.cpp
	ccc/tables.cpp
	ccc/util.cpp
)

add_executable(stdump stdump.cpp)
target_link_libraries(stdump ccc)
add_test(NAME stdump_test COMMAND stdump test ${CMAKE_SOURCE_DIR}/test)

add_executable(objdump objdump.cpp)
target_link_libraries(objdump ccc)
