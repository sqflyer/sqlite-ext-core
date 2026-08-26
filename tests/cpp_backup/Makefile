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

CFLAGS := -Wall -Wextra -O2 -I../../include $(SAN_FLAGS)
CXXFLAGS := -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti $(SAN_FLAGS)

# SQLite linking
LDFLAGS := -lsqlite3 $(SAN_FLAGS)

TARGET_DIR = bin

all: test

# Create the bin directory
$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

# Compile the C++ test
$(TARGET_DIR)/test_backup: test_backup.cpp | $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# Run the test
test: $(TARGET_DIR)/test_backup
	./$(TARGET_DIR)/test_backup

clean:
	rm -rf $(TARGET_DIR)

.PHONY: all test clean
