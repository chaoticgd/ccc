set(CMAKE_SYSTEM_NAME "Linux")
set(CMAKE_SYSTEM_PROCESSOR "x86_64")

set(CMAKE_C_COMPILER "x86_64-linux-musl-gcc")
set(CMAKE_C_COMPILER_AR "x86_64-linux-musl-ar")
set(CMAKE_C_COMPILER_RANLIB "x86_64-linux-musl-ranlib")
set(CMAKE_CXX_COMPILER "x86_64-linux-musl-g++")
set(CMAKE_CXX_COMPILER_AR "x86_64-linux-musl-ar")
set(CMAKE_CXX_COMPILER_RANLIB "x86_64-linux-musl-ranlib")

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")
