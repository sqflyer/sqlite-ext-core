CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti -nostdlib++

TARGET_DIR = bin
TARGET = $(TARGET_DIR)/test_oom

all: test

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

$(TARGET): test_oom.cpp | $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< -lsqlite3

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET_DIR)

.PHONY: all test clean
