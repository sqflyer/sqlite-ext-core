CXX ?= g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LIBS = -lsqlite3

# If compiling with standard compiler (not emscripten), add -nostdlib++ to enforce zero-dependency
ifeq ($(findstring em++,$(CXX)),)
    CXXFLAGS += -nostdlib++
endif

TARGETS = bin/test_udf bin/test_udf_state

.PHONY: all test clean

all: test

test: $(TARGETS)
	./bin/test_udf
	./bin/test_udf_state

bin/test_udf: test_udf.cpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBS)

bin/test_udf_state: test_udf_state.cpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBS)

clean:
	rm -rf bin
