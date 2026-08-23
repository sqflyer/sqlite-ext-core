CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I../include -fno-exceptions -fno-rtti -nostdlib++

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
