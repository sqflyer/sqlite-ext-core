# OS Detection
UNAME_S ?= $(shell uname -s 2>/dev/null || echo Windows)

# Compiler & Sanitizer Selection:
# - Linux: Uses GCC with AddressSanitizer & LeakSanitizer (-fsanitize=address,leak)
# - macOS (Darwin) & Windows (MSYS2): Uses Clang with AddressSanitizer (-fsanitize=address)
ifeq ($(UNAME_S),Linux)
    CXX := g++
    CC := gcc
    SAN_FLAGS := -fsanitize=address,leak
    EXT_SUFFIX := .so
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
    ifeq ($(UNAME_S),Darwin)
        EXT_SUFFIX := .dylib
    else
        EXT_SUFFIX := .dll
    endif
endif

CXXFLAGS := -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti $(SAN_FLAGS)

TARGET_DIR = bin
STATELESS_LIB = $(TARGET_DIR)/libtest_ext_stateless$(EXT_SUFFIX)
STATEFUL_LIB  = $(TARGET_DIR)/libtest_ext_stateful$(EXT_SUFFIX)
MIXED_LIB     = $(TARGET_DIR)/libtest_ext_mixed$(EXT_SUFFIX)
LOADER        = $(TARGET_DIR)/test_loader

ALL_LIBS = $(STATELESS_LIB) $(STATEFUL_LIB) $(MIXED_LIB)

all: test

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

$(STATELESS_LIB): test_ext_stateless.cpp | $(TARGET_DIR)
	$(CXX) -shared -fPIC $(CXXFLAGS) -o $@ $<

$(STATEFUL_LIB): test_ext_stateful.cpp | $(TARGET_DIR)
	$(CXX) -shared -fPIC $(CXXFLAGS) -o $@ $<

$(MIXED_LIB): test_ext_mixed.cpp | $(TARGET_DIR)
	$(CXX) -shared -fPIC $(CXXFLAGS) -o $@ $<

$(LOADER): test_loader.cpp | $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3

test: $(ALL_LIBS) $(LOADER)
	./$(LOADER) $(STATELESS_LIB) $(STATEFUL_LIB) $(MIXED_LIB)

clean:
	rm -rf $(TARGET_DIR)

.PHONY: all test clean
