# OS Detection
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

# Compiler & Sanitizer Selection:
# - Linux: Uses GCC (g++/gcc) with AddressSanitizer & LeakSanitizer (-fsanitize=address,leak)
# - macOS (Darwin) & Windows (MSYS2): Uses Clang (clang++/clang) with AddressSanitizer (-fsanitize=address)
ifeq ($(UNAME_S),Linux)
    CXX := g++
    CC := gcc
    SAN_FLAGS := -fsanitize=address,leak
else
    # macOS (Darwin) & Windows (MSYS2 / MinGW / CYGWIN)
    ifneq ($(wildcard /clang64/bin),)
        export PATH := /clang64/bin:$(PATH)
        CXX := /clang64/bin/clang++
        CC := /clang64/bin/clang
    else
        CXX := clang++
        CC := clang
    endif
    SAN_FLAGS := -fsanitize=address
endif

export PATH UNAME_S CXX CC SAN_FLAGS

.PHONY: test test-asan test-ext-state test-cpp-value test-cpp-row test-locks test-cpp-allocator test-cpp-smart-ptr test-cpp-udf test-cpp-aggregate test-cpp-statement test-cpp-tvf test-cpp-transaction test-cpp-db test-cpp-buffer test-cpp-blob-stream test-cpp-backup test-cpp-vtab test-cpp-extension test-time test-oom test-multi-tu example example-c leak-check-integration clean

test: test-ext-state test-cpp-value test-cpp-row test-locks test-time test-oom test-multi-tu test-cpp-allocator test-cpp-smart-ptr test-cpp-udf test-cpp-aggregate test-cpp-statement test-cpp-tvf test-cpp-transaction test-cpp-db test-cpp-buffer test-cpp-blob-stream test-cpp-backup test-cpp-vtab test-cpp-extension

test-asan:
	@echo "=== Running AddressSanitizer (ASan) Memory Verification ==="
	@export PATH=/clang64/bin:$$PATH; \
	$(MAKE) test ASAN=1

test-oom:
	$(MAKE) -C tests/oom_safety test

test-multi-tu:
	$(MAKE) -C tests/multi_tu test

test-time:
	$(MAKE) -C tests/time test

test-ext-state:
	$(MAKE) -C tests/ext_state test-c test-cpp

test-cpp-value:
	$(MAKE) -C tests/cpp_value test

test-cpp-row:
	$(MAKE) -C tests/cpp_row test

test-cpp-allocator:
	$(MAKE) -C tests/allocator test

test-cpp-smart-ptr:
	$(MAKE) -C tests/smart_ptr test

test-cpp-udf:
	$(MAKE) -C tests/cpp_udf test

test-cpp-aggregate:
	$(MAKE) -C tests/cpp_aggregate test

test-cpp-statement:
	$(MAKE) -C tests/cpp_statement test

test-cpp-tvf:
	$(MAKE) -C tests/cpp_tvf test

test-cpp-transaction:
	$(MAKE) -C tests/cpp_transaction test

test-cpp-db:
	$(MAKE) -C tests/cpp_db test

test-cpp-buffer:
	$(MAKE) -C tests/cpp_buffer test

test-cpp-blob-stream:
	@$(MAKE) -C tests/cpp_blob_stream test

test-cpp-backup:
	@$(MAKE) -C tests/cpp_backup test

test-cpp-vtab:
	@$(MAKE) -C tests/cpp_vtab test

test-cpp-extension:
	@$(MAKE) -C tests/cpp_extension test

example:
	@$(MAKE) -C examples run

example-c:
	@$(MAKE) -C example-c run

test-locks:
	$(MAKE) -C tests/locks test

leak-check-integration:
	$(MAKE) -C tests/ext_state leak-check

clean:
	rm -rf bin
	$(MAKE) -C tests/ext_state clean
	$(MAKE) -C tests/cpp_value clean
	$(MAKE) -C tests/cpp_udf clean
	$(MAKE) -C tests/cpp_aggregate clean
	$(MAKE) -C tests/cpp_statement clean
	$(MAKE) -C tests/cpp_tvf clean
	$(MAKE) -C tests/cpp_transaction clean
	$(MAKE) -C tests/cpp_db clean
	$(MAKE) -C tests/cpp_buffer clean
	$(MAKE) -C tests/cpp_blob_stream clean
	$(MAKE) -C tests/cpp_backup clean
	$(MAKE) -C tests/oom_safety clean
	$(MAKE) -C tests/multi_tu clean
	$(MAKE) -C tests/threads clean
	$(MAKE) -C tests/locks clean
	$(MAKE) -C tests/time clean
	$(MAKE) -C tests/allocator clean
	$(MAKE) -C tests/smart_ptr clean
	$(MAKE) -C tests/cpp_vtab clean
	$(MAKE) -C tests/cpp_extension clean
	$(MAKE) -C examples clean
	$(MAKE) -C example-c clean
