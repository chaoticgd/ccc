cmake_minimum_required(VERSION 2.8.11)
project(ccc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ccc STATIC
	ccc/util.cpp
	ccc/elf.cpp
	ccc/mdebug.cpp
	ccc/stabs.cpp
	ccc/ast.cpp
)

add_executable(stdump stdump.cpp)
target_link_libraries(stdump ccc)
