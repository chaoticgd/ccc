add_library(demanglegnu STATIC
	alloca.c
	cp-demangle.c
	cplus-dem.c
	d-demangle.c
	dyn-string.c
	getopt1.c
	getopt.c
	rust-demangle.c
	safe-ctype.c
	xexit.c
	xmalloc.c
	xmemdup.c
	xstrdup.c
)
target_include_directories(demanglegnu PUBLIC .)
target_compile_definitions(demanglegnu PRIVATE -DHAVE_LIMITS_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1)

add_executable(demangler_fuzzer testsuite/demangler-fuzzer.c)
add_executable(demangler_test testsuite/test-demangle.c)
target_link_libraries(demangler_fuzzer demanglegnu)
target_link_libraries(demangler_test demanglegnu)
target_compile_definitions(demangler_fuzzer PRIVATE -DHAVE_LIMITS_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1)
target_compile_definitions(demangler_test PRIVATE -DHAVE_LIMITS_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1)
