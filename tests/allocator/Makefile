.PHONY: test clean

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
LDFLAGS_CXX := -lsqlite3 $(SAN_FLAGS)
BIN_DIR = bin

test: $(BIN_DIR)/test_allocator $(BIN_DIR)/test_allocator_oom
	./$(BIN_DIR)/test_allocator
	./$(BIN_DIR)/test_allocator_oom

$(BIN_DIR)/test_allocator: test_allocator.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR)/test_allocator_oom: test_allocator_oom.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
