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
include_directories(.)
