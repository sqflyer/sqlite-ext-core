# SQLite Extension Example Guide

The complete working example, source code, SQL runner, and Makefile have been organized into the **[`examples/`](examples/)** directory:

- **[`examples/example.cpp`](examples/example.cpp)**: Full C++ extension source code implementing Scalar UDFs, Aggregates, TVFs, and Shared State.
- **[`examples/example.sql`](examples/example.sql)**: SQL script demonstrating how to load and query `./build/libexample`.
- **[`examples/Makefile`](examples/Makefile)**: Builds the extension into `./examples/build/libexample` and runs `example.sql`.
- **[`examples/README.md`](examples/README.md)**: Detailed step-by-step documentation and expected terminal output.

To build and run the example immediately:
```bash
cd examples && make run
```
