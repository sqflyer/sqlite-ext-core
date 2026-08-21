.PHONY: test clean

CC ?= gcc
CXX ?= g++
CFLAGS = -Wall -Wextra -I../../include
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS_C = -lsqlite3
LDFLAGS_CXX = -lsqlite3 -nostdlib++

test: test_atomic test_tiny_lock test_mutex_lock test_rw_lock
	./test_atomic
	./test_tiny_lock
	./test_mutex_lock
	./test_rw_lock

test_atomic: test_atomic.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

test_tiny_lock: test_tiny_lock.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

test_mutex_lock: test_mutex_lock.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

test_rw_lock: test_rw_lock.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

clean:
	rm -f test_atomic test_tiny_lock test_mutex_lock test_rw_lock *.exe
