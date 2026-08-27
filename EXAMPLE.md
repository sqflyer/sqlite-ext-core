# SQLite Native Extension Examples

The repository includes turnkey, standalone example modules for C++11, Pure C (C99/C11), and asynchronous coroutine worker pools:

---

## 1. C++ Extension Example (`example-cpp/`)

Demonstrates modern C++11 extension development using `sqlite3_ext_creator.hpp` and `sqlite3_ext.hpp` with `-nostdlib++` and `-fno-exceptions`:

- **[`example-cpp/example.cpp`](example-cpp/example.cpp)**: Full C++ extension implementing Scalar UDFs, Aggregate Functions, Table-Valued Functions (TVF), and Shared Connection State.
- **[`example-cpp/example.sql`](example-cpp/example.sql)**: SQL script loading `./build/libexample` and verifying all 4 components.
- **[`example-cpp/Makefile`](example-cpp/Makefile)**: Builds and runs the demo via SQLite CLI.
- **[`example-cpp/README.md`](example-cpp/README.md)**: In-depth step-by-step documentation, memory model walkthrough, and multi-language loading guide (Python, Node.js, C++).

### Run C++ Example
```bash
make example-cpp
# or: cd example-cpp && make run
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

---

## 3. Pure C Coroutine Extension Example (`example-coro-c/`)

Demonstrates zero-collision tagged extension coroutine pools and cooperative multi-stage fibers in Pure C:

- **[`example-coro-c/example.c`](example-coro-c/example.c)**: Tagged coroutine worker pool acquisition, fiber task dispatching, and `xDestroy` teardown callback.
- **[`example-coro-c/example.sql`](example-coro-c/example.sql)**: SQL script demonstrating asynchronous fiber task execution across multi-database connections.
- **[`example-coro-c/README.md`](example-coro-c/README.md)**: Architecture diagrams, memory model breakdown, and SQL reference.

### Run Pure C Coroutine Example
```bash
make example-coro-c
# or: cd example-coro-c && make run
```

---

## 4. C++ Coroutine Extension Example (`example-coro-cpp/`)

Demonstrates tagged template coroutine worker pools and stateful capturing closures in Modern C++:

- **[`example-coro-cpp/example.cpp`](example-coro-cpp/example.cpp)**: `SqliteExtCoroPool<Tag>`, capturing lambda closures without `<functional>`, and cooperative fiber yields.
- **[`example-coro-cpp/example.sql`](example-coro-cpp/example.sql)**: SQL script demonstrating capturing lambda closures and global atomic metrics.
- **[`example-coro-cpp/README.md`](example-coro-cpp/README.md)**: Architecture diagrams, template tag isolation guide, and SQL reference.

### Run C++ Coroutine Example
```bash
make example-coro-cpp
# or: cd example-coro-cpp && make run
```
