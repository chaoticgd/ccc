add_library(demanglegnu STATIC
	src/alloca.c
	src/cp-demangle.c
	src/cplus-dem.c
	src/d-demangle.c
	src/dyn-string.c
	src/rust-demangle.c
	src/safe-ctype.c
	src/xexit.c
	src/xmalloc.c
	src/xmemdup.c
	src/xstrdup.c
	include/ansidecl.h
	include/cp-demangle.h
	include/dyn-string.h
	include/environ.h
	include/libiberty.h
	include/safe-ctype.h
)

set(GNU_DEMANGLER_FLAGS -DHAVE_DECL_BASENAME=1 -DHAVE_LIMITS_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1)

target_include_directories(demanglegnu PUBLIC include)
target_compile_definitions(demanglegnu PUBLIC ${GNU_DEMANGLER_FLAGS})
target_link_libraries(demanglegnu getopt_port)

if(BUILD_DEMANGLER_TOOLS)
	add_executable(demangler_fuzzer testsuite/demangler-fuzzer.c)
	target_link_libraries(demangler_fuzzer demanglegnu)
	target_compile_definitions(demangler_fuzzer PUBLIC ${GNU_DEMANGLER_FLAGS})

	add_executable(demangler_test testsuite/test-demangle.c)
	target_link_libraries(demangler_test demanglegnu)
	target_compile_definitions(demangler_test PUBLIC ${GNU_DEMANGLER_FLAGS})
endif()
