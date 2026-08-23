CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -I../../include -fno-exceptions -fno-rtti -nostdlib++
LDFLAGS = -lsqlite3
TARGET_DIR = bin

.PHONY: all test clean

# Create the bin directory
$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

all: $(TARGET_DIR)/test_vtab $(TARGET_DIR)/test_vtab_state

# Compile the C++ stateless test
$(TARGET_DIR)/test_vtab: test_vtab.cpp | $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# Compile the C++ stateful test
$(TARGET_DIR)/test_vtab_state: test_vtab_state.cpp | $(TARGET_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# Run the test
test: all
	./$(TARGET_DIR)/test_vtab
	./$(TARGET_DIR)/test_vtab_state

clean:
	rm -rf $(TARGET_DIR)
