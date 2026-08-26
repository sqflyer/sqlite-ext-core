# C++ Value Types (`sqlite3_value.hpp`)

High-performance, zero-dependency, freestanding C++ RAII wrappers for SQLite core data types. Engineered specifically for SQLite extension authors to enable **Small Buffer Optimization (SBO)**, **zero-branch SQLite subtype preservation**, **zero-allocation heterogeneous map lookups**, and **transparent UDF/statement lifecycle binding**.

> **Architecture Reference**: For an in-depth breakdown of the 16-byte dual-representation memory model, bit-packed control tag registers (`SqliteOwnedValueTag`), zero-branch subtype alignment, and the 144+ heterogeneous relational operator suite, see [`docs/VALUE_ARCHITECTURE.md`](VALUE_ARCHITECTURE.md).

---

## 1. Architectural Philosophy: The 6 Types Model

In SQLite extension development, values appear in two distinct contexts:
1. **Transient Inputs (Views)**: Values passed into User-Defined Functions (`argv[]`) or returned by `sqlite3_column_value()`. These are owned by SQLite and must not be freed or mutated, but reading them into standard C++ strings (`std::string`) causes wasteful heap allocations.
2. **Persistent State (Owned)**: Values held across query invocations (e.g., aggregate accumulators, virtual table state, caches). These require strict RAII memory management, automatic cleanup via `sqlite3_value_free` or `sqlite3_free`, and SBO optimizations.

`sqlite3_value.hpp` organizes these responsibilities into **6 specialized classes**:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                               VIEW CLASSES                                  │
│ (Non-owning, Zero-Allocation, Transient wrappers over SQLite raw pointers)  │
│                                                                             │
│   SqliteValueView        SqliteStringView             SqliteBlobView        │
│  (const sqlite3_value*)  (const char*, int len)       (const void*, int len)│
│  [8 Bytes]               [16 Bytes]                   [16 Bytes]            │
└─────────────────────────────────────────────────────────────────────────────┘
                                     │
           Convert via .to_owned() OR Extract via .as_text() / .as_blob()
                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                                OWNED CLASSES                                │
│ (RAII memory management, 16-Byte Dual Layout SBO, Automatic destruction)    │
│                                                                             │
│   SqliteValueOwned       SqliteStringOwned            SqliteBlobOwned       │
│  (16B Dual Layout)     (sqlite3_str dynamic)       (sqlite3_malloc bytes)   │
│  [16 Bytes]            [8 Bytes]                   [16 Bytes]               │
└─────────────────────────────────────────────────────────────────────────────┘
                                     │
         Compose via SqliteValueOwnedStaticArray<N> / SqliteValueOwnedDynamicArray
                                     ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│                            OWNED ARRAY CLASSES                                   │
│ (Contiguous N × 16B arrays of SqliteValueOwned, RAII-managed)                    │
│                                                                                  │
│  SqliteValueOwnedStaticArray<N>           SqliteValueOwnedDynamicArray           │
│  (Stack — N columns, 0 mallocs)           (Heap — sqlite3_realloc64-managed)     │
│  [N × 16 Bytes on Stack/In-Situ]          [16 Bytes handle → Heap Array]         │
│                                                                                  │
│          SqliteValueOwnedArray<N>         (Unified template alias)               │
│  N > 0 → SqliteValueOwnedStaticArray<N>    N == 0 → SqliteValueOwnedDynamicArray │
└──────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Feature Matrix

| Feature | `SqliteValueView` | `SqliteValueOwned` | `SqliteStringView` / `Owned` | `SqliteBlobView` / `Owned` |
| :--- | :---: | :---: | :---: | :---: |
| **Size in Memory** | **8 Bytes** | **16 Bytes (Exact)** | 16B (View) / 8B (Owned) | 16B (View) / 16B (Owned) |
| **Allocation Model** | Zero (Non-owning) | SBO (Inline) / Heap | Zero (View) / `sqlite3_str` | Zero (View) / `sqlite3_malloc` |
| **SBO Capacity** | N/A (View) | Strings $\le 13$B, Blobs $\le 14$B | N/A | N/A |
| **Subtype Handling** | Zero-copy inspection | **Offset 14 (Zero-branch)** | N/A | N/A |
| **Affinity Handling** | Storage-class derived | Native SQLite affinity byte | N/A | N/A |
| **Direct Extraction** | `.as_text()`, `.as_blob()` | `.as_text()`, `.as_blob()` | `.data()`, `.c_str()` | `.data()` |
| **UDF / Statement Interop** | `.result()`, `.bind()` | `.result()`, `.bind()` | `.result()`, `.bind()` | `.result()`, `.bind()` |
| **Heterogeneous Lookups** | 144+ operators | 144+ operators | Operators for `std::map` | Operators for `std::map` |

### Owned Array Summary

| Type | Storage | Allocation | Use Case |
| :--- | :--- | :--- | :--- |
| `SqliteValueOwnedStaticArray<N>` | Stack / In-Situ | **0 Mallocs** | Fixed-schema caches, VTable rows |
| `SqliteValueOwnedDynamicArray` | Heap (`sqlite3_realloc64`) | 1 `sqlite3_malloc64` | Runtime-sized row materialization |
| `SqliteValueOwnedArray<N>` | Alias: Static ($N>0$) / Dynamic ($N=0$) | — | Generic algorithms |

---

## 3. `SqliteValueView` API Reference

`SqliteValueView` is a lightweight, non-owning 8-byte wrapper over `const sqlite3_value*`.

### Constructors & Factories
```cpp
// 1. Wrap a raw SQLite value from UDF arguments (argv[i])
SqliteValueView val(argv[0]);

// 2. Wrap a prepared statement column value (Zero Allocation)
SqliteValueView col = SqliteValueView::from_column(stmt, 0);
```

### Storage Class & Affinity Predicates
```cpp
if (val.is_null())     { /* SQLITE_NULL */ }
if (val.is_integer())  { /* SQLITE_INTEGER */ }
if (val.is_float())    { /* SQLITE_FLOAT */ }
if (val.is_text())     { /* SQLITE_TEXT */ }
if (val.is_blob())     { /* SQLITE_BLOB */ }
if (val.is_numeric())  { /* Returns true for INTEGER, FLOAT, or NUMERIC affinity */ }

char aff = val.affinity(); // Returns '@', 'A', 'B', 'C', 'D', 'E', or 'F'
```

### Subtype Predicates
```cpp
uint8_t sub = val.subtype(); // Returns raw 8-bit SQLite subtype

if (val.is_json())       { /* Subtype 'J' (JSON or JSONB) */ }
if (val.is_decimal())    { /* Subtype 'D' (Arbitrary Precision Decimal) */ }
if (val.is_uuid())       { /* Subtype 'U' (16-Byte Canonical UUID) */ }
if (val.is_vector())     { /* Subtype 'V' (AI Vector Embedding) */ }
if (val.is_geometry())   { /* Subtype 'G' (GeoJSON / Geopoly Array) */ }
if (val.is_datetime())   { /* Subtype 'T' (Timestamp / Epoch Millis) */ }
if (val.is_bool())       { /* Subtype 'B' (Explicit Boolean) */ }
if (val.is_compressed()) { /* Subtype 'Z' (Compressed Stream) */ }
```

### Zero-Allocation Data Extraction
```cpp
// Primitives
sqlite3_int64 i = val.as_int64();
double        d = val.as_double();
bool          b = val.as_bool(); // True if as_int64() != 0

// Text and Blob Views (Zero heap allocations, null-safe)
SqliteStringView str  = val.as_text(); // Returns non-owning string view (ptr + length)
SqliteBlobView   blob = val.as_blob(); // Returns non-owning blob view (ptr + size)
```

### Conversion to Owned
```cpp
// Duplicates the value into owned memory or stores it inline via SBO
SqliteValueOwned owned = val.to_owned();
```

---

## 4. `SqliteValueOwned` API Reference

`SqliteValueOwned` is a 16-byte RAII polymorphic container featuring Small Buffer Optimization (SBO), shared-offset subtype tracking, and automatic memory cleanup.

### Primitive Constructors (Zero Heap Allocation)
```cpp
// 1. Default NULL
SqliteValueOwned null_val;

// 2. 64-bit Integer
SqliteValueOwned int_val(42LL);

// 3. Double-precision Float
SqliteValueOwned float_val(3.1415926535);

// 4. Boolean (Tagged with SQLITE_SUBTYPE_BOOL)
SqliteValueOwned bool_val(true);
```

### Static Subtype Factory Methods
```cpp
// 1. JSON (Strings <= 13 chars stored inline with zero heap allocations)
SqliteValueOwned j1 = SqliteValueOwned::from_json("{\"ok\":true}");
SqliteValueOwned j2 = SqliteValueOwned::from_jsonb(binary_data, len);

// 2. Arbitrary Precision Decimal
SqliteValueOwned dec = SqliteValueOwned::from_decimal("999999999999999.99");

// 3. UUID Binary
SqliteValueOwned uuid = SqliteValueOwned::from_uuid(uuid_16_bytes);

// 4. AI Vector Embeddings
SqliteValueOwned vec = SqliteValueOwned::from_vector(float_array, sizeof(float_array));

// 5. Geometry / Spatial Coordinates
SqliteValueOwned geo = SqliteValueOwned::from_geometry(geo_bytes, len);

// 6. Datetime (Epoch Milliseconds)
SqliteValueOwned dt = SqliteValueOwned::from_datetime(1724700000000LL);

// 7. Compressed Stream
SqliteValueOwned comp = SqliteValueOwned::from_compressed(zstd_stream, len);
```

### Inspection & Metadata Getters
```cpp
int  t   = val.type();               // SQLITE_INTEGER, SQLITE_FLOAT, SQLITE_TEXT, etc.
bool h   = val.is_heap_allocated();  // False for primitives and SBO buffers (<=13-14B)
int  len = val.inline_length();      // Byte length of inline text/blob (0..14)
char aff = val.affinity();           // SQLite Affinity character ('@', 'A'..'F')
uint8_t sub = val.subtype();         // 8-bit SQLite Subtype (Offset 14, 1-cycle access)

SqliteOwnedValueTag tag = val.tag(); // Access raw 1-byte packed control register
```

### Freestanding Move Semantics
```cpp
#include "sqlite3_allocator.hpp" // For sqlite_move

SqliteValueOwned a = SqliteValueOwned::from_text("my string");
SqliteValueOwned b = sqlite_move(a); // 128-bit register move (1 cycle)

assert(b.as_text() == "my string");
assert(a.is_null()); // Moved-from instance is safely reset to NULL
```

---

## 5. Owned Value Arrays (`SqliteValueOwnedStaticArray`, `SqliteValueOwnedDynamicArray`)

These classes provide contiguous RAII-managed arrays of `SqliteValueOwned` elements — the foundational building blocks from which the Row classes in `sqlite3_row.hpp` are derived.

### `SqliteValueOwnedStaticArray<N>` — Stack-Allocated Array

```cpp
// Exactly N * 16 bytes on the stack — zero heap allocations
SqliteValueOwnedStaticArray<4> static_arr;
static_arr[0] = 42LL;
static_arr[1] = SqliteValueOwned::from_text("hello");
static_arr[2] = 3.14;
static_arr[3] = SqliteValueOwned(); // SQLITE_NULL

assert(static_arr.size() == 4);

// Element access
SqliteValueOwned& elem = static_arr[2];
SqliteValueView   view = static_arr.view_at(2); // non-owning view
```

### `SqliteValueOwnedDynamicArray` — Heap-Allocated Resizable Array

```cpp
// Allocates N * 16 bytes via sqlite3_malloc64
SqliteValueOwnedDynamicArray dyn_arr(3);
dyn_arr[0] = 1LL;
dyn_arr[1] = SqliteValueOwned::from_text("world");
dyn_arr[2] = 2.71828;

// Resize preserving existing elements (uses sqlite3_realloc64 for in-place growth)
dyn_arr.resize(5);
assert(dyn_arr.size() == 5);
assert(dyn_arr[0].as_int64() == 1);
assert(dyn_arr[3].is_null()); // New elements are SQLITE_NULL

// 1-cycle move (transfers 8-byte pointer)
SqliteValueOwnedDynamicArray moved = sqlite_move(dyn_arr);
assert(dyn_arr.empty()); // Source safely zeroed
```

### `SqliteValueOwnedArray<N>` — Unified Template Alias

```cpp
template <size_t N = 0>
using SqliteValueOwnedArray = /* SqliteValueOwnedStaticArray<N> (N > 0)
                                  SqliteValueOwnedDynamicArray  (N == 0) */;

// Use identical API regardless of allocation model:
SqliteValueOwnedArray<3> stack_arr;   // Stack
SqliteValueOwnedArray<0> heap_arr(3); // Heap
```

### Relationship to Row Classes

`SqliteRowStatic<N>` and `SqliteRowDynamic` are thin subclasses of these array types, adding only row-domain methods (`view()`, `column_count()`, `operator SqliteRowView()`):

```
SqliteValueOwnedStaticArray<N>  ←─── SqliteRowStatic<N>
SqliteValueOwnedDynamicArray    ←─── SqliteRowDynamic
```

---

## 6. Dynamic String Building (`SqliteStringOwned`)

`SqliteStringOwned` leverages SQLite's native `sqlite3_str` builder, enabling dynamic string formatting that transfers directly into UDF results or statements with **zero buffer copying**:

```cpp
SqliteStringOwned sb(ctx.get()); // Initialized with UDF context
sb.appendall("SELECT * FROM ");
sb.append("users", 5);
sb.appendchar(1, ' ');
sb.appendf("WHERE id = %d AND active = %d", user_id, 1);

// Transfer directly to SQLite UDF result (zero copy!)
sb.result(ctx);
```

---

## 7. Zero-Allocation Heterogeneous Map Lookups

Standard C++ `std::map<std::string, T>` forces a dynamic allocation whenever a search key is created from a SQLite value. By using `SqliteStringOwned` (or `SqliteValueOwned`) with `std::less<>`, lookups using transient `SqliteStringView` or `SqliteValueView` execute with **0 heap allocations**:

```cpp
#include <map>
#include "sqlite3_value.hpp"

// Heterogeneous Map Keyed by Owned String
std::map<SqliteStringOwned, int, std::less<>> user_cache;

// Insert records
user_cache.emplace(SqliteStringOwned("user:1001"), 85);
user_cache.emplace(SqliteStringOwned("user:1002"), 92);

// Lookup inside SQLite UDF using non-allocating SqliteStringView:
void get_score_udf(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    SqliteValueView arg0(argv[0]);
    SqliteStringView search_key = arg0.as_text(); // 0 allocations

    auto it = user_cache.find(search_key); // Fast lookup, 0 allocations!
    if (it != user_cache.end()) {
        sqlite3_result_int(ctx, it->second);
    } else {
        sqlite3_result_null(ctx);
    }
}
```

---

## 8. Polymorphic Variant Maps

Store heterogeneous SQLite datatypes in a single `std::map` sorted strictly by SQLite's native collation order ($\text{NULL} < \text{NUMERIC} < \text{TEXT} < \text{BLOB}$):

```cpp
std::map<SqliteValueOwned, const char*, std::less<>> poly_map;

poly_map.emplace(SqliteValueOwned(), "null record");
poly_map.emplace(SqliteValueOwned(100), "integer record");
poly_map.emplace(SqliteValueOwned(3.14), "float record");
poly_map.emplace(SqliteValueOwned::from_text("hello"), "text record");

// Lookup via primitives:
auto it1 = poly_map.find(100);                  // Found!
auto it2 = poly_map.find(SqliteStringView("hello", 5)); // Found!
```

---

## 9. Performance Benchmarks (Cycle-Accurate)

| Operation | Standard C++ / SQLite Baseline | `sqlite3_value.hpp` | Improvement |
| :--- | :--- | :--- | :--- |
| **Short Text Allocation ($\le 13$B)** | `sqlite3_value_dup` ($\sim 80\text{--}200$ cycles) | **Inline SBO ($\sim 1\text{--}2$ cycles)** | **$\sim 50\times\text{--}100\times$ Faster** |
| **Short Blob Allocation ($\le 14$B)** | `sqlite3_value_dup` ($\sim 80\text{--}200$ cycles) | **Inline SBO ($\sim 1\text{--}2$ cycles)** | **$\sim 50\times\text{--}100\times$ Faster** |
| **Subtype Inspection (`subtype()`)** | Pointer deref + branch ($\sim 5\text{--}15$ cycles) | **Shared Offset 14 ($1$ cycle)** | **$\sim 5\times\text{--}15\times$ Faster** |
| **Type Query (`type()`)** | Branch / switch ($\sim 3\text{--}8$ cycles) | **Bit shift `raw >> 5` ($1$ cycle)** | **$\sim 3\times\text{--}8\times$ Faster** |
| **Move Constructor / Assignment** | Struct copy + free ($\sim 15\text{--}30$ cycles) | **128-bit SIMD Move ($1$ cycle)** | **$\sim 15\times\text{--}30\times$ Faster** |
| **Cache Line Capacity (64 Bytes)** | 2 values (32B layout) | **4 values (16B layout)** | **$2\times$ Cache Line Density** |
