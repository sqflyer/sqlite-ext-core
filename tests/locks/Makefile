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

CFLAGS := -Wall -Wextra -I../../include $(SAN_FLAGS)
CXXFLAGS := -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti $(SAN_FLAGS)
LDFLAGS_C := -lsqlite3 $(SAN_FLAGS)
LDFLAGS_CXX := -lsqlite3 $(SAN_FLAGS)
BIN_DIR = bin

TEST_TARGETS = \
	$(BIN_DIR)/test_atomic \
	$(BIN_DIR)/test_atomic_cpp \
	$(BIN_DIR)/test_tiny_lock \
	$(BIN_DIR)/test_tiny_lock_c \
	$(BIN_DIR)/test_mutex_lock \
	$(BIN_DIR)/test_mutex_lock_c \
	$(BIN_DIR)/test_rw_lock \
	$(BIN_DIR)/test_rw_lock_c \
	$(BIN_DIR)/test_lock_policies \
	$(BIN_DIR)/test_lock_policies_c

test: $(TEST_TARGETS)
	./$(BIN_DIR)/test_atomic
	./$(BIN_DIR)/test_atomic_cpp
	./$(BIN_DIR)/test_tiny_lock
	./$(BIN_DIR)/test_tiny_lock_c
	./$(BIN_DIR)/test_mutex_lock
	./$(BIN_DIR)/test_mutex_lock_c
	./$(BIN_DIR)/test_rw_lock
	./$(BIN_DIR)/test_rw_lock_c
	./$(BIN_DIR)/test_lock_policies
	./$(BIN_DIR)/test_lock_policies_c

$(BIN_DIR)/test_atomic: test_atomic.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

$(BIN_DIR)/test_atomic_cpp: test_atomic_cpp.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR)/test_tiny_lock: test_tiny_lock.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR)/test_tiny_lock_c: test_tiny_lock_c.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

$(BIN_DIR)/test_mutex_lock: test_mutex_lock.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR)/test_mutex_lock_c: test_mutex_lock_c.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

$(BIN_DIR)/test_rw_lock: test_rw_lock.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR)/test_rw_lock_c: test_rw_lock_c.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

$(BIN_DIR)/test_lock_policies: test_lock_policies.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR)/test_lock_policies_c: test_lock_policies_c.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
