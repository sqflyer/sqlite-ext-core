CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS = -lsqlite3 -nostdlib++
BIN_DIR = bin

# Ensure 'bin' directory exists
$(shell mkdir -p $(BIN_DIR))

.PHONY: all test clean

all: $(BIN_DIR)/test_tvf $(BIN_DIR)/test_tvf_state

$(BIN_DIR)/test_tvf: test_tvf.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR)/test_tvf_state: test_tvf_state.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test: all
	./$(BIN_DIR)/test_tvf
	./$(BIN_DIR)/test_tvf_state

clean:
	rm -rf $(BIN_DIR)
