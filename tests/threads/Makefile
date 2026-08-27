.PHONY: all test clean

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

export PATH UNAME_S CXX CC SAN_FLAGS

CFLAGS := -std=c99 -Wall -Wextra -O2 -I../../include -I../../include/async $(SAN_FLAGS)
CXXFLAGS := -std=c++11 -Wall -Wextra -O2 -I../../include -I../../include/async -fno-exceptions -fno-rtti $(SAN_FLAGS)

BIN_DIR = bin

TARGETS = $(BIN_DIR)/test_thread_c $(BIN_DIR)/test_cond_c $(BIN_DIR)/test_coro_c $(BIN_DIR)/test_thread $(BIN_DIR)/test_cond $(BIN_DIR)/test_coro

all: $(TARGETS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/test_thread_c: test_thread_c.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -lsqlite3 $(SAN_FLAGS)

$(BIN_DIR)/test_cond_c: test_cond_c.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -lsqlite3 $(SAN_FLAGS)

$(BIN_DIR)/test_coro_c: test_coro_c.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< -lsqlite3 $(SAN_FLAGS)

$(BIN_DIR)/test_thread: test_thread.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3 $(SAN_FLAGS)

$(BIN_DIR)/test_cond: test_cond.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3 $(SAN_FLAGS)

$(BIN_DIR)/test_coro: test_coro.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3 $(SAN_FLAGS)

test: all
	@echo "Executing Thread, Condition Variable & Coroutine tests..."
	./$(BIN_DIR)/test_thread_c
	./$(BIN_DIR)/test_cond_c
	./$(BIN_DIR)/test_coro_c
	./$(BIN_DIR)/test_thread
	./$(BIN_DIR)/test_cond
	./$(BIN_DIR)/test_coro

clean:
	rm -rf $(BIN_DIR)

.PHONY: all test clean
