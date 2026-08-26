CC ?= gcc
CXX ?= g++
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -I../../include
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti -nostdlib++

BIN_DIR = bin

TARGETS = $(BIN_DIR)/test_time_c $(BIN_DIR)/test_time

all: $(TARGETS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/test_time_c: test_time_c.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN_DIR)/test_time: test_time.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

test: all
	@echo "Executing Time & Clock tests..."
	./$(BIN_DIR)/test_time_c
	./$(BIN_DIR)/test_time

clean:
	rm -rf $(BIN_DIR)

.PHONY: all test clean
