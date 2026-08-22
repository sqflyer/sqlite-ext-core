CXX ?= g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LIBS = -lsqlite3

# If compiling with standard compiler (not emscripten), add -nostdlib++ to enforce zero-dependency
ifeq ($(findstring em++,$(CXX)),)
    CXXFLAGS += -nostdlib++
endif

.PHONY: all test clean

all: test

test: bin/test_statement
	./bin/test_statement

bin/test_statement: test_statement.cpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBS)

clean:
	rm -rf bin
