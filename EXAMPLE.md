# SQLite Native Extension Examples

The repository includes turnkey, standalone example modules for both **C++11** and **Pure C (C99/C11)**:

---

## 1. C++ Extension Example (`examples/`)

Demonstrates modern C++11 extension development using `sqlite3_ext_creator.hpp` and `sqlite3_ext.hpp` with `-nostdlib++` and `-fno-exceptions`:

- **[`examples/example.cpp`](examples/example.cpp)**: Full C++ extension implementing Scalar UDFs, Aggregate Functions, Table-Valued Functions (TVF), and Shared Connection State.
- **[`examples/example.sql`](examples/example.sql)**: SQL script loading `./build/libexample` and verifying all 4 components.
- **[`examples/Makefile`](examples/Makefile)**: Builds and runs the demo via SQLite CLI.
- **[`examples/README.md`](examples/README.md)**: In-depth step-by-step documentation, memory model walkthrough, and multi-language loading guide (Python, Node.js, C++).

### Run C++ Example
```bash
make example
# or: cd examples && make run
```

---

## 2. Pure C Extension Example (`example-c/`)

Demonstrates Pure C (C99/C11) extension development using `sqlite3_ext_creator.h` and `sqlite3_ext.h`:

- **[`example-c/example.c`](example-c/example.c)**: Pure C extension implementing connection-bound shared state (`sqlite3_ext_state.h`), stateless scalar UDFs, stateful scalar UDFs, custom aggregate functions, and dual entrypoint exports.
- **[`example-c/example.sql`](example-c/example.sql)**: SQL runner script.
- **[`example-c/Makefile`](example-c/Makefile)**: Compiles `example.c` with `gcc` into `build/libc_example.dll` (or `.so`/`.dylib`) and runs `example.sql`.
- **[`example-c/README.md`](example-c/README.md)**: Documentation and expected ASCII table output.

### Run Pure C Example
```bash
make example-c
# or: cd example-c && make run
```
