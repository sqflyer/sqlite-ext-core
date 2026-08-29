# C++ Extension Creator Test Suite (`tests/cpp_extension`)

Comprehensive test suite verifying the dynamic shared library creation and entrypoint dispatch mechanisms of `sqlite3_ext_creator.hpp` and `sqlite3_ext.hpp`.

---

## 1. Test Suite Matrix

The suite compiles **three distinct shared library extension modules** (`.dll` on Windows, `.so` on Linux, `.dylib` on macOS) and a dynamic host test loader:

| Target | Source File | Type | Key Features Tested |
| :--- | :--- | :--- | :--- |
| `libtest_ext_stateless` | `test_ext_stateless.cpp` | Shared Library | Stateless Scalar UDFs, Aggregate Functions, TVFs, and Virtual Tables |
| `libtest_ext_stateful` | `test_ext_stateful.cpp` | Shared Library | Stateful UDFs, Mutex-guarded `SqliteExtState`, Multi-DB isolation |
| `libtest_ext_mixed` | `test_ext_mixed.cpp` | Shared Library | Mixed Stateless + Stateful UDFs using default `sqlite3_extension_init` |
| `test_loader` | `test_loader.cpp` | Host Executable | Dynamic `sqlite3_load_extension` (`dlopen` / `LoadLibrary`) harness |

---

## 2. Test Specifications & Breakdown

### A. Stateless Extension (`libtest_ext_stateless`)
1. **Scalar UDFs**: `stateless_add(100, 250)` returning `350`, `stateless_greet('Developer')` returning `'Greetings, Developer!'`.
2. **Aggregate Functions**: `stateless_sum_sq(x)` calculating sum of squares.
3. **Table-Valued Functions**: `stateless_range(10, 14)` yielding 5 rows with sum 60.
4. **Virtual Tables**: `stateless_echo` virtual table yielding computed scores.

### B. Stateful Extension (`libtest_ext_stateful`)
1. **Connection-Local State**: `stateful_inc()` / `stateful_get()` counter mutating state from 500 $\to$ 501 $\to$ 502.
2. **String Concatenation**: `stateful_concat()` returning `'SESSION_TEST:apple,banana,cherry'`.
3. **Tag Mutation**: `stateful_set_tag('NEW_TAG')` dynamically altering TVF and virtual table outputs.
4. **Multi-Connection Isolation**: Verifies that opening `db2` starts fresh state at 500 without crosstalk from `db1`.

### C. Mixed Extension (`libtest_ext_mixed`)
1. **Default Entrypoint**: Verifies automatic resolution via SQLite's canonical entrypoint `sqlite3_extension_init`.
2. **Coexistence**: Verifies stateless math functions (`mixed_multiply`, `mixed_iota`) alongside stateful audit logging (`mixed_audit`, `mixed_weighted_avg`).

---

## 3. Dynamic Loading & ASan ODR Handling

When compiling and testing multiple dynamically loaded `.so` shared libraries in a single host process under AddressSanitizer (`-fsanitize=address`):
- Each extension declares its local `sqlite3_api` dispatch table pointer via SQLite's standard `SQLITE_EXTENSION_INIT1`.
- On Linux, ASan's One Definition Rule (ODR) checker is configured via `ASAN_OPTIONS=detect_odr_violation=0` during test execution to permit multiple valid SQLite dynamic extensions in the same process.

---

## 4. Compilation & Execution Guide

### Windows MSVC (`cl.exe`)
```cmd
cd tests\cpp_extension
make.bat clean
make.bat build
make.bat test
```

### MSYS2 / GCC / Clang
```bash
cd /home/dilipvamsi/works/repos/sqlite-ext-core/tests/cpp_extension
make clean
make all
make test
```
