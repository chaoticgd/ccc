CXX=g++
CXXFLAGS=-std=c++17 -Wall

stdump: stdump.cpp libccc.a
	$(CXX) $(CXXFLAGS) -c stdump.cpp -o stdump.o
	$(CXX) stdump.o libccc.a -o stdump
libccc.a: ccc/util.o ccc/elf.o ccc/mdebug.o ccc/stabs.o
	ar rcs $@ $^
ccc/%.o: ccc/%.c ccc/ccc.h
	$(CXX) $(CXXFLAGS) -c ccc/%.c -o %.o
