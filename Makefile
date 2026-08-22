.PHONY: test test-ext-state test-cpp-value-keys test-locks test-allocator test-smart-ptr leak-check-integration clean

test: test-ext-state test-cpp-value-keys test-locks test-allocator test-smart-ptr

test-ext-state:
	$(MAKE) -C tests/ext_state test-c test-cpp

test-cpp-value-keys:
	$(MAKE) -C tests/cpp_value_keys test

test-locks:
	$(MAKE) -C tests/locks test

test-allocator:
	$(MAKE) -C tests/allocator test

test-smart-ptr:
	$(MAKE) -C tests/smart_ptr test

leak-check-integration:
	$(MAKE) -C tests/ext_state leak-check

clean:
	$(MAKE) -C tests/ext_state clean
	$(MAKE) -C tests/cpp_value_keys clean
	$(MAKE) -C tests/locks clean
	$(MAKE) -C tests/allocator clean
	$(MAKE) -C tests/smart_ptr clean
