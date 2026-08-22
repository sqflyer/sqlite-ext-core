.PHONY: test clean

CC ?= gcc
CXX ?= g++
CFLAGS = -Wall -Wextra -I../../include
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS_C = -lsqlite3
LDFLAGS_CXX = -lsqlite3 -nostdlib++
BIN_DIR = bin

test: $(BIN_DIR)/test_atomic $(BIN_DIR)/test_atomic_cpp $(BIN_DIR)/test_tiny_lock $(BIN_DIR)/test_mutex_lock $(BIN_DIR)/test_rw_lock
	./$(BIN_DIR)/test_atomic
	./$(BIN_DIR)/test_atomic_cpp
	./$(BIN_DIR)/test_tiny_lock
	./$(BIN_DIR)/test_mutex_lock
	./$(BIN_DIR)/test_rw_lock

$(BIN_DIR)/test_atomic: test_atomic.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_C)

$(BIN_DIR)/test_atomic_cpp: test_atomic_cpp.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR)/test_tiny_lock: test_tiny_lock.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR)/test_mutex_lock: test_mutex_lock.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR)/test_rw_lock: test_rw_lock.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
