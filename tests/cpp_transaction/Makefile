CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -O2 -I../../include
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti -nostdlib++

# SQLite requires these to build the amalgamated source
SQLITE_FLAGS = -DSQLITE_THREADSAFE=1

all: test

# Build the test binary
bin/test_transaction: test_transaction.cpp ../../include/sqlite3_transaction.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3

test: bin/test_transaction
	./bin/test_transaction

clean:
	rm -rf bin
