# OS Detection
UNAME_S ?= $(shell uname -s 2>/dev/null || echo Windows)

# Compiler & Sanitizer Selection:
# - Linux: Uses GCC with AddressSanitizer & LeakSanitizer (-fsanitize=address,leak)
# - macOS (Darwin) & Windows (MSYS2): Uses Clang with AddressSanitizer (-fsanitize=address)
ifeq ($(UNAME_S),Linux)
    CXX := g++
    CC := gcc
    SAN_FLAGS := -fsanitize=address,leak
    POSIX_FLAGS := -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
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
    POSIX_FLAGS :=
endif

CFLAGS := -std=c99 $(POSIX_FLAGS) -Wall -Wextra -O2 -I../../include $(SAN_FLAGS)
CXXFLAGS := -std=c++11 $(POSIX_FLAGS) -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti $(SAN_FLAGS)

BIN_DIR = bin

TARGETS = $(BIN_DIR)/test_time_c $(BIN_DIR)/test_time

all: $(TARGETS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/test_time_c: test_time_c.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN_DIR)/test_time: test_time.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

test: all
	@echo "Executing Time & Clock tests..."
	./$(BIN_DIR)/test_time_c
	./$(BIN_DIR)/test_time

clean:
	rm -rf $(BIN_DIR)

.PHONY: all test clean
