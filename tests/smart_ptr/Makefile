.PHONY: test clean test-c test-cpp

CC ?= gcc
CXX ?= g++
CFLAGS = -Wall -Wextra -I../../include
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS_C = -lsqlite3
LDFLAGS_CXX = -lsqlite3 -nostdlib++

test: test-c test-cpp

test-c: test_smart_ptr_c
	./test_smart_ptr_c

test-cpp: test_smart_ptr_cpp
	./test_smart_ptr_cpp

test_smart_ptr_c: test_smart_ptr.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

test_smart_ptr_cpp: test_smart_ptr.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

clean:
	rm -f test_smart_ptr_c test_smart_ptr_cpp test_smart_ptr_c.exe test_smart_ptr_cpp.exe
