CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -O2 -I../../include
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti -nostdlib++

SQLITE_FLAGS = -DSQLITE_THREADSAFE=1

all: test

bin/test_blob_stream: test_blob_stream.cpp ../../include/sqlite3_blob_stream.hpp ../../include/sqlite3_db.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3

test: bin/test_blob_stream
	./bin/test_blob_stream

clean:
	rm -rf bin
