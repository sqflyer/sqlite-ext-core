.PHONY: test-c test-cpp build-c build-cpp build-go-integration clean

ROOT_DIR := $(shell pwd)
C_EXT_DIR := $(ROOT_DIR)/c_extension
CPP_EXT_DIR := $(ROOT_DIR)/cpp_extension
GO_LOADER_DIR := $(ROOT_DIR)/go_loader
BIN_DIR := $(ROOT_DIR)/../../bin

# OS-specific extension suffix
ifeq ($(OS),Windows_NT)
    EXT_SUFFIX := .dll
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        EXT_SUFFIX := .dylib
    else
        EXT_SUFFIX := .so
    endif
endif

C_EXT_PATH := $(BIN_DIR)/c_ext/libmyext$(EXT_SUFFIX)
CPP_EXT_PATH := $(BIN_DIR)/cpp_ext/libmyext$(EXT_SUFFIX)

test-c: build-c
	@echo "Running Go concurrency & isolation stress test with C extension..."
	cd $(GO_LOADER_DIR) && EXT_PATH=$(C_EXT_PATH) $(BIN_DIR)/concurrency_test
	@echo "Running Go dynamic/lazy loading test with C extension..."
	cd $(GO_LOADER_DIR) && EXT_PATH=$(C_EXT_PATH) $(BIN_DIR)/lazy_test

test-cpp: build-cpp
	@echo "Running Go concurrency & isolation stress test with C++ extension..."
	cd $(GO_LOADER_DIR) && EXT_PATH=$(CPP_EXT_PATH) $(BIN_DIR)/concurrency_test
	@echo "Running Go dynamic/lazy loading test with C++ extension..."
	cd $(GO_LOADER_DIR) && EXT_PATH=$(CPP_EXT_PATH) $(BIN_DIR)/lazy_test

build-go-integration:
	@echo "Building Go integration tests..."
	@mkdir -p $(BIN_DIR)
	cd $(GO_LOADER_DIR) && go build -tags sqlite_extension -o $(BIN_DIR)/concurrency_test concurrency.go
	cd $(GO_LOADER_DIR) && go build -tags sqlite_extension -o $(BIN_DIR)/lazy_test lazy_load.go

build-c: build-go-integration
	@mkdir -p $(BIN_DIR)/c_ext
	@echo "Building C extension..."
	gcc -shared -fPIC -O2 $(C_EXT_DIR)/myext.c -o $(C_EXT_PATH)

build-cpp: build-go-integration
	@mkdir -p $(BIN_DIR)/cpp_ext
	@echo "Building C++ extension..."
	g++ -shared -fPIC -O2 -fno-exceptions -fno-rtti -nostdlib++ $(CPP_EXT_DIR)/myext.cpp -o $(CPP_EXT_PATH)

leak-check: build-c
	@echo "Building C leak check program..."
	gcc $(C_EXT_DIR)/leak_check.c -o $(BIN_DIR)/leak_check -lsqlite3
	@echo "Running Valgrind memory leak check on C program..."
	valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite \
		$(BIN_DIR)/leak_check $(C_EXT_PATH)

clean:
	rm -rf $(BIN_DIR)
