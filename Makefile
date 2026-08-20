.PHONY: test test-ext-state clean

test: test-ext-state

test-ext-state:
	$(MAKE) -C tests/ext_state test-c test-cpp

clean:
	$(MAKE) -C tests/ext_state clean
