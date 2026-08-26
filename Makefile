.PHONY: test test-ext-state test-cpp-value test-cpp-value-keys test-locks test-cpp-allocator test-cpp-smart-ptr test-cpp-udf test-cpp-aggregate test-cpp-statement test-cpp-tvf test-cpp-transaction test-cpp-db test-cpp-buffer test-cpp-blob-stream test-cpp-backup test-cpp-vtab test-cpp-extension test-time test-oom test-multi-tu example example-c leak-check-integration clean

test: test-ext-state test-cpp-value test-locks test-time test-oom test-multi-tu test-cpp-allocator test-cpp-smart-ptr test-cpp-udf test-cpp-aggregate test-cpp-statement test-cpp-tvf test-cpp-transaction test-cpp-db test-cpp-buffer test-cpp-blob-stream test-cpp-backup test-cpp-vtab test-cpp-extension

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

test-cpp-value-keys: test-cpp-value

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
	$(MAKE) -C tests/locks clean
	$(MAKE) -C tests/time clean
	$(MAKE) -C tests/allocator clean
	$(MAKE) -C tests/smart_ptr clean
	$(MAKE) -C tests/cpp_vtab clean
	$(MAKE) -C tests/cpp_extension clean
	$(MAKE) -C examples clean
	$(MAKE) -C example-c clean
