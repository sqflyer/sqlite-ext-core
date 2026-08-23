CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -O2 -I../../include
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti -nostdlib++

# SQLite linking
LDFLAGS = -lsqlite3

TARGET_DIR = bin

all: test

# Create the bin directory
$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

# Compile the C++ test
$(TARGET_DIR)/test_vtab: test_vtab.cpp | $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# Run the test
test: $(TARGET_DIR)/test_vtab
	./$(TARGET_DIR)/test_vtab

clean:
	rm -rf $(TARGET_DIR)

.PHONY: all test clean
