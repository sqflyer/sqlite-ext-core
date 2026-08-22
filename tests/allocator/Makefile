.PHONY: test clean

CXX ?= g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS_CXX = -lsqlite3 -nostdlib++
BIN_DIR = bin

test: $(BIN_DIR)/test_allocator
	./$(BIN_DIR)/test_allocator

$(BIN_DIR)/test_allocator: test_allocator.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS_CXX)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
