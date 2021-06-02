CC=g++
LIBCCC_SRC=ccc/elf.cpp

stdump: stdump.cpp libccc.o
	$(CC) stdump.cpp libccc.o -o stdump -std=c++17 -Wall
libccc.o: ccc/ccc.h $(LIBCCC_SRC)
	$(CC) -c $(LIBCCC_SRC) -o libccc.o -std=c++17 -Wall
