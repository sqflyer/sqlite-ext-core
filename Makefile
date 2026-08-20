.PHONY: test test-ext-state test-cpp-value-keys leak-check-integration clean

test: test-ext-state test-cpp-value-keys

test-ext-state:
	$(MAKE) -C tests/ext_state test-c test-cpp

test-cpp-value-keys:
	$(MAKE) -C tests/cpp_value_keys test

leak-check-integration:
	$(MAKE) -C tests/ext_state leak-check

clean:
	$(MAKE) -C tests/ext_state clean
	$(MAKE) -C tests/cpp_value_keys clean
