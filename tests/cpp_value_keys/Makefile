.PHONY: test clean

CXX ?= g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS = -lsqlite3 -nostdlib++
BIN_DIR = bin

test: $(BIN_DIR)/test_value_keys
	./$(BIN_DIR)/test_value_keys

$(BIN_DIR)/test_value_keys: test_value_keys.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)
