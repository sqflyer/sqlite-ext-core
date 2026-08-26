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

CXXFLAGS := -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti $(SAN_FLAGS)

TARGET_DIR = bin
TARGET = $(TARGET_DIR)/test_multi_tu

OBJS = $(TARGET_DIR)/test_tu_a.o $(TARGET_DIR)/test_tu_b.o $(TARGET_DIR)/test_tu_main.o

all: test

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

$(TARGET_DIR)/test_tu_a.o: test_tu_a.cpp test_tu_shared.hpp | $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(TARGET_DIR)/test_tu_b.o: test_tu_b.cpp test_tu_shared.hpp | $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(TARGET_DIR)/test_tu_main.o: test_tu_main.cpp test_tu_shared.hpp | $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) -lsqlite3

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET_DIR)

.PHONY: all test clean
