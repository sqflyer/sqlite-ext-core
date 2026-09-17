# OS Detection
UNAME_S ?= $(shell uname -s 2>/dev/null || echo Windows)

# Compiler & Sanitizer Selection:
ifeq ($(UNAME_S),Linux)
    CXX := g++
    CC := gcc
    SAN_FLAGS := -fsanitize=address,leak
else
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

CFLAGS := -Wall -Wextra -O2 -I../../include $(SAN_FLAGS)
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti $(SAN_FLAGS)
LDFLAGS := -lsqlite3 $(SAN_FLAGS)

all: test

bin/test_duo: test_duo.cpp ../../include/stl/duo_hash.hpp ../../include/stl/duo_linear.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

bin/test_duo_c: test_duo_c.c ../../include/stl/duo_hash.h
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: bin/test_duo bin/test_duo_c
	./bin/test_duo
	./bin/test_duo_c

clean:
	rm -rf bin
