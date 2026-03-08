add_library(getopt_port STATIC
	src/getopt.c
	include/getopt.h
)

target_include_directories(getopt_port PUBLIC include)
