CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -I../../include -fno-exceptions -fno-rtti
LDFLAGS = -lsqlite3 -nostdlib++

# Ensure 'bin' directory exists
$(shell mkdir -p bin)

TARGET = bin/test_tvf
SRC = test_tvf.cpp

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf bin
