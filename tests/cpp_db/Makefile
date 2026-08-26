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
LDFLAGS := -lsqlite3 $(SAN_FLAGS)

SQLITE_FLAGS = -DSQLITE_THREADSAFE=1

all: test

bin/test_db: test_db.cpp ../../include/sqlite3_db.hpp ../../include/sqlite3_transaction.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3

test: bin/test_db
	./bin/test_db

clean:
	rm -rf bin
