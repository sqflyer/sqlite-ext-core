# C++ Value Keys (`sqlite3_value_keys.hpp`)

Zero-dependency C++ RAII wrappers for SQLite core data types, engineered specifically to enable zero-allocation heterogeneous map lookups and safe polymorphic variants.

## Features
- **Zero-Allocation Lookups**: Provides non-owning `View` wrappers (`SqliteStringView`, `SqliteBlobView`, `SqliteValueView`) to prevent expensive memory allocations during C++ map key lookups.
- **Heterogeneous Lookups**: Natively supports comparing `View`s against heavy, memory-managed `Owned` classes.
- **Polymorphic Variants**: Safely store Integer, Float, Text, and Blob payloads inside the exact same `std::map` using the polymorphic `SqliteValueOwned` wrapper.
- **Ergonomic String Builders**: Easily construct dynamic strings without a database handle using standard `(const char*)` constructors, or safely instantiate them inside User-Defined Functions with `(sqlite3_context*)` wrappers.
- **Zero STL Overhead**: Fully implemented using raw C-pointers and SQLite's native memory profilers (`sqlite3_malloc`). No `<string>` or `<vector>` overhead. Perfect for constrained environments like WASM.

## Setup
Simply `#include "include/sqlite3_value_keys.hpp"` in your SQLite C++ extension project!

For architectural details, please see [VALUE_KEYS_ARCHITECTURE.md](VALUE_KEYS_ARCHITECTURE.md).
