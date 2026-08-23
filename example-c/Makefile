CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -I../include

# OS-specific extension suffix
ifeq ($(OS),Windows_NT)
    EXT_SUFFIX := .dll
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        EXT_SUFFIX := .dylib
    else
        EXT_SUFFIX := .so
    endif
endif

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
