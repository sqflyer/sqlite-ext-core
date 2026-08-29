# C++ Row Key Types (`sqlite3_row_key.hpp`)

High-performance, zero-dependency, freestanding 16-byte Small Buffer Optimized (SBO) primary key and composite index container for SQLite extensions. Engineered specifically for **in-memory B-Trees, Swiss Tables, and MemKV cache rings** to achieve **L1 cache line packing (4 keys per 64 bytes)**, **zero heap allocations for single-column keys**, and **zero-allocation heterogeneous map lookups**.

> **Architecture Reference**: For an in-depth breakdown of the 16-byte dual-representation overlapping union, bit-level tag multiplexing (`SqliteOwnedValueTag` at Offset 15), MurmurHash2 composite combining, and assembly-level CPU cache packing, see [`docs/ROW_KEY_ARCHITECTURE.md`](ROW_KEY_ARCHITECTURE.md).

---

## 1. Overview & Key Capabilities

In embedded database engines and SQLite extension architecture, indexed keys differ fundamentally from tabular row storage:
- **Tabular Rows (`SqliteRowView` / `SqliteRowDynamic`)**: Store complete $M$-column tuples (e.g. `id, name, email, salary, created_at`) spanning dozens of bytes across multiple cache lines.
- **Index Keys (`SqliteRowKeyOwned`)**: Store narrow primary or secondary keys (typically 1 scalar or a composite of 2–3 columns) packed tightly into B-Tree index nodes and hash table buckets.

`sqlite3_row_key.hpp` solves index memory bloat by guaranteeing an **exact 16-byte footprint** across all scenarios:

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                      SQLITE ROW KEY DUAL MEMORY MODEL                           │
│                                                                                 │
│  A. Single-Column Key (N = 1, 95% of database primary keys)                     │
│  ┌─────────────────────────────────────────────────────────────┬──────────┐     │
│  │              In-Situ SqliteValueOwned (15 Bytes)            │ Tag Byte │     │
│  │   (Holds int64, double, small text/blob inline: 0 mallocs)  │  (0x01)  │     │
│  └─────────────────────────────────────────────────────────────┴──────────┘     │
│   Total: EXACTLY 16 Bytes (0 Heap Allocations, Direct Register Storage)         │
│                                                                                 │
│  B. Composite Multi-Column Key (N >= 2 or N = 0)                                │
│  ┌────────────────────────────┬──────────┬──────────┬──────────┬──────────┐     │
│  │ SqliteValueOwned* ptr (8B) │ size(4B) │ cap (2B) │ res (1B) │ Tag (0x00)     │
│  └────────────────────────────┴──────────┴──────────┴──────────┴──────────┘     │
│   Total: EXACTLY 16 Bytes (Overlapping Heap Array Descriptor)                   │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### Core Features

- **Exact 16-Byte Footprint**: Guaranteed `sizeof(SqliteRowKeyOwned) == 16`. Exactly **4 complete keys fit in a single 64-byte L1 CPU cache line**.
- **100% In-Situ for Single-Column Keys**: Stores integers, doubles, small text (up to 13 chars), and small blobs (up to 14 bytes) directly with **zero heap allocations and zero pointer indirections**.
- **Composite Column Extractor**: Extracts indexed key subsets from wide rows in a single step (`SqliteRowKeyOwned(full_row, indices, count)`).
- **Heterogeneous Relational Matrix**: Compares directly against native types (`int`, `sqlite3_int64`, `double`, `bool`, `const char*`), `SqliteStringView`, `SqliteBlobView`, and `SqliteRowOwnedWrapper`.
- **Transparent STL Functors**: Provides `SqliteRowKeyHash`, `SqliteRowKeyEqual`, and `SqliteRowKeyLess` (`is_transparent = void`) for zero-allocation `std::map::find()` and Swiss table lookups.

---

## 2. Feature Matrix

| Feature | `SqliteRowKeyOwned` | `SqliteRowOwnedWrapper` | `SqliteRowDynamic` | `SqliteRowStatic<N>` |
| :--- | :---: | :---: | :---: | :---: |
| **Size in Memory** | **16 Bytes (Exact)** | **16 Bytes (Exact)** | 16B Handle + Heap | $N \times 16$ Bytes |
| **Memory Model** | **SBO In-Situ / Heap** | Transient Span (`ptr+len`) | Heap Dynamic Array | Stack Contiguous |
| **Primary Domain** | **B-Tree & Hash Index Keys** | Non-owning Row Slices | Wide Query Row Storage | Fixed-Schema Rows |
| **L1 Cache Line Density** | **4 Keys / 64B Line** | 4 Spans / 64B Line | Multi-line | Multi-line |
| **Heterogeneous Lookups** | Full ($==, <, \text{hash}$) | Full ($==, <, \text{hash}$) | Relational Operators | Relational Operators |
| **Freestanding Safe** | Yes (`-nostdlib++`) | Yes (`-nostdlib++`) | Yes (`-nostdlib++`) | Yes (`-nostdlib++`) |

---

## 3. API Reference

### Constructors & Lifecycle

```cpp
// 1. Empty key (size = 0)
SqliteRowKeyOwned k_empty;

// 2. Single-column key from native scalar (0 mallocs)
SqliteRowKeyOwned k_int(SqliteValueOwned(42));
SqliteRowKeyOwned k_str(SqliteValueOwned("alice"));
SqliteRowKeyOwned k_view(sqlite_value_view);

// 3. String & Blob View constructors
SqliteRowKeyOwned k_sv(SqliteStringView("active", 6));
SqliteRowKeyOwned k_bv(SqliteBlobView(raw_data, 12));

// 4. Composite key from wide row using column index mapping
SqliteRowDynamic full_row(5); // id, first_name, last_name, dept, salary
int key_cols[] = {0, 3};      // Composite key on (id, dept)
SqliteRowKeyOwned k_comp(full_row, key_cols, 2);

// 5. Non-owning transient view extraction
SqliteRowOwnedWrapper span = k_comp.view();
```

### Accessors & Mutators

```cpp
int  size() const noexcept;        // Column count (1 for scalar, N for composite)
bool empty() const noexcept;       // True if size == 0
void resize(int new_count);        // Expands or shrinks columns (auto-transitions SBO)
void clear() noexcept;             // Clears to size = 0

// Element access (0-indexed)
SqliteValueOwned&       operator[](int index) noexcept;
const SqliteValueOwned& operator[](int index) const noexcept;
SqliteValueOwned*       data() noexcept;
const SqliteValueOwned* data() const noexcept;

// Typed Column Extraction Accessors (SQLITE_DERIVE_ARRAY_ACCESSORS)
sqlite3_int64    as_int64(int index = 0) const noexcept;
int              as_int(int index = 0)   const noexcept;
double           as_double(int index = 0) const noexcept;
SqliteStringView as_text(int index = 0)   const noexcept;
SqliteBlobView   as_blob(int index = 0)   const noexcept;
bool             as_bool(int index = 0)   const noexcept;
bool             is_null(int index = 0)   const noexcept;
int              type(int index = 0)      const noexcept;
uint8_t          subtype(int index = 0)   const noexcept;

// Hashing & View conversion
uint64_t hash() const noexcept;    // 64-bit MurmurHash2 composite hash
SqliteRowOwnedWrapper view() const noexcept;
```

---

## 4. Complete Relational Comparison Matrix

`SqliteRowKeyOwned` implements symmetric relational operators ($==, \ne, <, \le, >, \ge$) across all data types:

| Left-Hand Type | Operator | Right-Hand Type | Zero Heap? | Comparison Rule |
| :--- | :---: | :--- | :---: | :--- |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `SqliteRowKeyOwned` | Yes | SBO in-situ / composite lexicographical compare |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `SqliteRowView` | Yes | Key vs. zero-copy row view (statement/argv/span) |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `SqliteRowOwnedWrapper` | Yes | Key vs. transient 16B row span slice |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `SqliteValueOwned` / `View` | Yes | Key vs. single polymorphic value |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `SqliteStringView` / `Owned` | Yes | Key vs. string slice |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `SqliteBlobView` / `Owned` | Yes | Key vs. binary blob slice |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `int` / `sqlite3_int64` | Yes | Key vs. native integer (`k == 42`, `100 > k`) |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `double` / `float` | Yes | Key vs. floating point (`k == 3.14`) |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `bool` | Yes | Key vs. boolean flag (`k == true`) |
| `SqliteRowKeyOwned` | $==, <, \dots$ | `const char*` | Yes | Key vs. C-string (`k == "admin"`, `"admin" < k`) |

### Lexicographical Tuple Ordering Semantics
- **Single vs. Single**: Follows SQLite collation rules (`NULL < Integer/Real < Text < Blob`).
- **Single vs. Composite**: Prefix matching evaluates single key as 1-tuple. E.g., `(10) < (10, 20)` is **`true`** (shorter prefix is strictly smaller).
- **Composite vs. Composite**: Column-by-column comparison up to `min(len1, len2)`.

---

## 5. Transparent Functors & STL Container Integration

`sqlite3_row_key.hpp` provides transparent functors (`using is_transparent = void;`) enabling **heterogeneous lookups without key construction**:

```cpp
struct SqliteRowKeyHash;  // MurmurHash2 over keys, wrappers, strings, and native types
struct SqliteRowKeyEqual; // Transparent equality (a == b)
struct SqliteRowKeyLess;  // Transparent ordering (a < b)
```

### Example: Zero-Allocation B-Tree Lookups (`std::map`)

```cpp
#include "sqlite3_row_key.hpp"
#include <map>
#include <string>

void btree_indexing_example() {
    // Transparent B-Tree index
    std::map<SqliteRowKeyOwned, std::string, SqliteRowKeyLess> index;

    index[SqliteRowKeyOwned(SqliteValueOwned(10))] = "User 10";
    index[SqliteRowKeyOwned(SqliteValueOwned(20))] = "User 20";
    index[SqliteRowKeyOwned(SqliteValueOwned("admin"))] = "Role Admin";

    // 1. Heterogeneous search with native int (0 mallocs, 0 key construction!)
    auto it1 = index.find(10);
    assert(it1 != index.end() && it1->second == "User 10");

    // 2. Heterogeneous search with C-string (0 mallocs!)
    auto it2 = index.find("admin");
    assert(it2 != index.end() && it2->second == "Role Admin");

    // 3. Range query with lower_bound
    auto it3 = index.lower_bound(15);
    assert(it3 != index.end() && it3->second == "User 20");
}
```

### Example: Transparent Swiss Table / Hash Map (`std::unordered_map`)

```cpp
#include "sqlite3_row_key.hpp"
#include <unordered_map>

void hash_map_example() {
    std::unordered_map<SqliteRowKeyOwned, int, SqliteRowKeyHash, SqliteRowKeyEqual> cache;

    SqliteRowKeyOwned k1(SqliteValueOwned(42));
    cache[k1] = 1000;

    // Direct hash lookup
    assert(cache.find(k1) != cache.end());
}
```
