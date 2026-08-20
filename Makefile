.PHONY: test test-ext-state test-cpp-value-keys clean

test: test-ext-state test-cpp-value-keys

test-ext-state:
	$(MAKE) -C tests/ext_state test-c test-cpp

test-cpp-value-keys:
	$(MAKE) -C tests/cpp_value_keys test

clean:
	$(MAKE) -C tests/ext_state clean
	$(MAKE) -C tests/cpp_value_keys clean
