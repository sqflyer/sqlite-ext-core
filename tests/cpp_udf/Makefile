# OS Detection
UNAME_S ?= $(shell uname -s 2>/dev/null || echo Windows)

# Compiler & Sanitizer Selection:
# - Linux: Uses GCC with AddressSanitizer & LeakSanitizer (-fsanitize=address,leak)
# - macOS (Darwin) & Windows (MSYS2): Uses Clang with AddressSanitizer (-fsanitize=address)
ifeq ($(UNAME_S),Linux)
    CXX := g++
    CC := gcc
    SAN_FLAGS := -fsanitize=address,leak
else
    # macOS (Darwin) & Windows (MSYS2 / MinGW / CYGWIN)
    ifneq ($(wildcard /clang64/bin),)
        export PATH := /clang64/bin:$(PATH)
        CXX := /clang64/bin/clang++
        CC := /clang64/bin/clang
    else
        CXX := clang++
        CC := clang
    endif
    SAN_FLAGS := -fsanitize=address
endif

CXXFLAGS := -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti $(SAN_FLAGS)
LIBS := -lsqlite3 $(SAN_FLAGS)

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
