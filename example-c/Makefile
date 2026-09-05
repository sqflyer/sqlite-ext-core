# OS Detection
UNAME_S ?= $(shell uname -s 2>/dev/null || echo Windows)

# Compiler & Sanitizer Selection:
# - Linux: Uses GCC with AddressSanitizer & LeakSanitizer (-fsanitize=address,leak)
# - macOS (Darwin) & Windows (MSYS2): Uses Clang with AddressSanitizer (-fsanitize=address)
ifeq ($(UNAME_S),Linux)
    CC := gcc
    SAN_FLAGS := -fsanitize=address,leak
    POSIX_FLAGS := -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
    EXT_SUFFIX := .so
else
    # macOS (Darwin) & Windows (MSYS2 / MinGW / CYGWIN)
    ifneq ($(wildcard /clang64/bin),)
        export PATH := /clang64/bin:$(PATH)
        CC := /clang64/bin/clang
    else
        CC := clang
    endif
    SAN_FLAGS :=
    POSIX_FLAGS :=
    ifeq ($(UNAME_S),Darwin)
        EXT_SUFFIX := .dylib
    else
        EXT_SUFFIX := .dll
    endif
endif

CFLAGS := -std=c99 $(POSIX_FLAGS) -Wall -Wextra -O2 -I../include $(SAN_FLAGS)

BUILD_DIR = build
TARGET = $(BUILD_DIR)/libc_example$(EXT_SUFFIX)

all: run

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): example.c | $(BUILD_DIR)
	$(CC) -shared -fPIC $(CFLAGS) -o $@ $<

run: $(TARGET)
	sqlite3 :memory: ".read example.sql"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean
