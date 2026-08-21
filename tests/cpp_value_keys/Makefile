.PHONY: test clean

CXX ?= g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS = -lsqlite3 -nostdlib++

test: test_value_keys
	./test_value_keys

test_value_keys: test_value_keys.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f test_value_keys test_value_keys.exe
