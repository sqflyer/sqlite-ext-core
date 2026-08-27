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
    SAN_FLAGS :=
    ifeq ($(UNAME_S),Darwin)
        EXT_SUFFIX := .dylib
    else
        EXT_SUFFIX := .dll
    endif
endif

CXXFLAGS := -std=c++11 -Wall -Wextra -O2 -I../include -fno-exceptions -fno-rtti $(SAN_FLAGS)

BUILD_DIR = build
TARGET = $(BUILD_DIR)/libexample$(EXT_SUFFIX)

all: run

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): example.cpp | $(BUILD_DIR)
	$(CXX) -shared -fPIC $(CXXFLAGS) -o $@ $<

run: $(TARGET)
	sqlite3 :memory: ".read example.sql"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean
