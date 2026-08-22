CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -O2 -I../../include
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti -nostdlib++

SQLITE_FLAGS = -DSQLITE_THREADSAFE=1

all: test

bin/test_db: test_db.cpp ../../include/sqlite3_db.hpp ../../include/sqlite3_transaction.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3

test: bin/test_db
	./bin/test_db

clean:
	rm -rf bin
