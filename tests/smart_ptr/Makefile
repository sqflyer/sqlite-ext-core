.PHONY: test clean test-c test-cpp

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

CFLAGS := -Wall -Wextra -I../../include $(SAN_FLAGS)
CXXFLAGS := -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti $(SAN_FLAGS)
LDFLAGS_C := -lsqlite3 $(SAN_FLAGS)
LDFLAGS_CXX := -lsqlite3 $(SAN_FLAGS)
BIN_DIR = bin

test: test-c test-cpp

test-c: $(BIN_DIR)/test_smart_ptr_c
	./$(BIN_DIR)/test_smart_ptr_c

test-cpp: $(BIN_DIR)/test_smart_ptr_cpp
	./$(BIN_DIR)/test_smart_ptr_cpp

$(BIN_DIR)/test_smart_ptr_c: test_smart_ptr.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

$(BIN_DIR)/test_smart_ptr_cpp: test_smart_ptr.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
