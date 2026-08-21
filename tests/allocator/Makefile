.PHONY: test clean

CXX ?= g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS_CXX = -lsqlite3 -nostdlib++

test: test_allocator
	./test_allocator

test_allocator: test_allocator.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

clean:
	rm -f test_allocator test_allocator.exe
