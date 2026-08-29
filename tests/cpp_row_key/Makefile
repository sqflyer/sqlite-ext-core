# OS Detection
UNAME_S ?= $(shell uname -s 2>/dev/null || echo Windows)

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

CXXFLAGS := -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti $(SAN_FLAGS)
LIBS := -lsqlite3 $(SAN_FLAGS)

.PHONY: all test test-nostd test-std clean

all: test

test: bin/test_row_key bin/test_row_key_std
	./bin/test_row_key
	./bin/test_row_key_std

test-nostd: bin/test_row_key
	./bin/test_row_key

test-std: bin/test_row_key_std
	./bin/test_row_key_std

bin/test_row_key: test_row_key.cpp | bin
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBS)

bin/test_row_key_std: test_row_key_std.cpp | bin
	$(CXX) -std=c++14 -Wall -Wextra -I../../include $(SAN_FLAGS) -o $@ $< $(LIBS)

bin:
	mkdir -p bin

clean:
	rm -rf bin
