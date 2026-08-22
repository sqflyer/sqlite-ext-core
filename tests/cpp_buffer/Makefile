CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -O2 -I../../include
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti -nostdlib++

SQLITE_FLAGS = -DSQLITE_THREADSAFE=1

all: test

bin/test_buffer: test_buffer.cpp ../../include/sqlite3_buffer.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3

test: bin/test_buffer
	./bin/test_buffer

clean:
	rm -rf bin
