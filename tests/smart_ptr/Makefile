.PHONY: test clean test-c test-cpp

CC ?= gcc
CXX ?= g++
CFLAGS = -Wall -Wextra -I../../include
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS_C = -lsqlite3
LDFLAGS_CXX = -lsqlite3 -nostdlib++
BIN_DIR = bin

test: test-c test-cpp

test-c: $(BIN_DIR)/test_smart_ptr_c
	./$(BIN_DIR)/test_smart_ptr_c

test-cpp: $(BIN_DIR)/test_smart_ptr_cpp
	./$(BIN_DIR)/test_smart_ptr_cpp

$(BIN_DIR)/test_smart_ptr_c: test_smart_ptr.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

$(BIN_DIR)/test_smart_ptr_cpp: test_smart_ptr.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
