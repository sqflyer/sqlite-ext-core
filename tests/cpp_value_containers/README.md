# Value Containers & 8x8 Matrix Dispatch Test Suite (`tests/cpp_value_containers`)

An exhaustive test suite verifying the **dual value container templates** (`SqliteValueTuple<N>`, `SqliteValueVec<N>`), the **scope-guarded stack dispatcher** (`withSqliteRowOwned`), and the **generic $8 \times 8$ compile-time matrix dispatch framework** (`sqlite3_dispatch_8x8.hpp`).

---

## 1. Test Suite Matrix

| Test Source | Standard | Features Tested | Zero-Dependency (`-nostdlib++`) |
| :--- | :---: | :--- | :---: |
| **`test_value_containers.cpp`** | **C++11** | In-situ stack tuples ($N=1..8$), direct heap tuples ($N = 0$), SBO adaptive vector growth/shrink, direct heap vectors ($N = 0$), `withSqliteRowOwned`, SQL binding reflection, SIMD null initializers, and Generic 8x8 matrix dispatch. | **Yes** |
| **`test_value_containers_comparisons.cpp`** | **C++11** | Full cross-container relational comparisons ($A \equiv B, A < B$), length/arity prefix semantics, SQLite type-rank collations, and single-column scalar operators across all fundamental types. | **Yes** |
| **`test_value_containers_std.cpp`** | **C++14 / C++20** | STL integration: Transparent Swiss Tables (`std::unordered_map` / `std::unordered_set`), Transparent B-Trees (`std::map` / `std::set`), `std::vector`, STL algorithms (`std::sort`, `std::binary_search`, `std::lower_bound`), `std::pair`, and `std::tuple`. | No (STL Verification) |

---

## 2. Test Specifications & Breakdown

### A. Core Value Containers (`test_value_containers.cpp`)
1. **`SqliteValueTuple<N>` In-Situ Static Footprint ($N \in [1..8]$)**:
   - Verifies exact $N \times 16\text{B}$ stack array without dynamic heap allocations.
   - Tests bounds safety, direct extraction accessors, in-place element mutation, and MurmurHash2 composite hashing.
2. **`SqliteValueTuple<N>` Direct Heap Tuple ($N = 0$, default `SqliteValueTuple<>`)**:
   - Verifies dynamic heap allocation via `sqlite3_malloc64` and runtime sizing via constructor argument.
3. **`SqliteValueVec<N>` Adaptive Stack SBO & Reversible Heap Spilling ($N \in [1..8]$)**:
   - Tests in-situ stack operation for sizes $\le N$.
   - Tests dynamic heap spilling when resized $> N$.
   - Tests safe return to stack when shrunk back $\le N$.
   - Tests 100% stack data density via backwards active tag scanning (`is_active()`, `0x20` threshold).
4. **`SqliteValueVec<N>` Direct Heap Vector ($N = 0$, default `SqliteValueVec<>`)**:
   - Tests unbounded variable-column payload vectors with 0 stack SBO overhead.
5. **Scope-Guarded Stack Dispatcher (`withSqliteRowOwned`)**:
   - Verifies complete branch coverage (negative sizes, 0..8 stack branches, 9..64 heap allocations) and return forwarding.
6. **SQLite SQL Binding & Row Reflection**:
   - Verifies binding containers directly to prepared statements (`bind_row`) and reading query result sets.
7. **Generic $8 \times 8$ Compile-Time Matrix Dispatch Framework**:
   - Tests `SQLITE_DISPATCH_1D_8`, `SQLITE_DISPATCH_2D_8X8`, and `SQLITE_MAKE_DEFAULT_STORAGE_8X8` single-line container factories.
8. **Static Null Template & Single-Burst SIMD Initialization**:
   - Tests zero-cost null initialization using static SIMD memory blocks.
9. **Generic Array, Initializer List & Variadic Heterogeneous Constructors**:
   - Tests constructing row containers from arrays, initializer lists, and heterogeneous variadic packs.

### B. Relational Comparisons (`test_value_containers_comparisons.cpp`)
1. **Cross-Container Equality Matrix ($A \equiv B$)**:
   - Tests all 16 cross-container permutations across `SqliteValueTuple`, `SqliteValueVec`, `SqliteRowOwnedWrapper`, and `SqliteRowView`.
2. **Lexicographical Column Ordering Differences ($A < B$)**:
   - Tests multi-column ordering invariants for integer, floating-point, and string differences.
3. **Arity & Length Prefix Semantics**:
   - Tests common prefix comparisons and empty container relations.
4. **SQLite Type-Rank Collation**:
   - Tests $\text{NULL (0)} < \text{NUMERIC (1)} < \text{TEXT (2)} < \text{BLOB (3)}$ and strict integer vs float tie-breakers.
5. **1-Column Scalar & Primitive Relational Operators**:
   - Tests direct comparison against `int`, `long`, `int64_t`, `unsigned int`, `unsigned long`, `unsigned long long`, `double`, `float`, `bool`, `const char*`, `SqliteStringView`, `SqliteStringOwned`, `SqliteBlobView`, `SqliteValueOwned`, and `SqliteValueView`.

### C. Standard Library Integration (`test_value_containers_std.cpp`)
1. **Transparent `std::unordered_map` & `std::unordered_set`**:
   - Tests hashing tuples and vectors using `SqliteRowHash` and `SqliteRowEqual`.
2. **Cross-Container Heterogeneous Lookup**:
   - Tests querying a `std::map` keyed by `SqliteValueTuple` using `SqliteValueVec`, `SqliteRowOwnedWrapper`, and `SqliteRowView`.
3. **Transparent `std::map` & `std::set`**:
   - Tests ordered B-Tree sorting, scalar queries, and range lookups via `SqliteRowLess`.
4. **`std::vector` & Move Semantics**:
   - Tests dynamic container reallocation and move semantics without memory leaks.
5. **Standard Algorithms**:
   - Tests `std::sort`, `std::binary_search`, and `std::lower_bound` over container collections.
6. **`std::pair` & `std::tuple` Integration**:
   - Tests composition inside standard pairs and tuples.

---

## 3. Compilation & Execution Guide

### MSYS2 / GCC / Clang
```bash
cd tests/cpp_value_containers
make test
```

### Windows MSVC (`cl.exe`)
```cmd
cd tests\cpp_value_containers
make.bat
```
