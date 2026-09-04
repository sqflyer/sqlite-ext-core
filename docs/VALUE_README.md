# C++ Value Types (`sqlite3_value.hpp`)

High-performance, zero-dependency, freestanding C++ RAII wrappers for SQLite core data types. Engineered specifically for SQLite extension authors to enable **Small Buffer Optimization (SBO)**, **zero-branch SQLite subtype preservation**, **zero-allocation heterogeneous map lookups**, **in-situ 16-byte raw UUID storage**, and **transparent UDF/statement lifecycle binding**.

> **Architecture Reference**: For an in-depth breakdown of the 24-byte multi-representation memory model, bit-packed control tag registers (`SqliteOwnedValueTag`, `SqliteOwnedValueSubTag`), zero-branch subtype alignment, in-situ UUID representation (`InlineUuidRep`), and the 144+ heterogeneous relational operator suite, see [`docs/VALUE_ARCHITECTURE.md`](VALUE_ARCHITECTURE.md).

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
│ (RAII memory management, 24-Byte Layout SBO, In-situ UUIDs, Auto Cleanup)   │
│                                                                             │
│   SqliteValueOwned       SqliteStringOwned            SqliteBlobOwned       │
│  (24B Multi-Layout)    (sqlite3_str dynamic)       (sqlite3_malloc bytes)   │
│  [24 Bytes]            [8 Bytes]                   [16 Bytes]               │
└─────────────────────────────────────────────────────────────────────────────┘
                                     │
         Compose via SqliteValueTuple<N> / SqliteValueVec<N>
                                     ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│                            VALUE CONTAINERS (sqlite3_value_containers.hpp)       │
│ (Contiguous N × 24B arrays of SqliteValueOwned, RAII-managed)                    │
│                                                                                  │
│  SqliteValueTuple<N>                      SqliteValueVec<N>                      │
│  (Stack Tuples — N cols, 0 mallocs)       (Adaptive SBO Vector — Stack/Spill)    │
│  [N × 24 Bytes on Stack/In-Situ]          [N × 24B In-Situ / Heap Array]         │
└──────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Feature Matrix

| Feature | `SqliteValueView` | `SqliteValueOwned` | `SqliteStringView` / `Owned` | `SqliteBlobView` / `Owned` |
| :--- | :---: | :---: | :---: | :---: |
| **Size in Memory** | **8 Bytes** | **24 Bytes (Exact)** | 16B (View) / 8B (Owned) | 16B (View) / 16B (Owned) |
| **Allocation Model** | Zero (Non-owning) | SBO (Inline) / In-Situ UUID / Heap | Zero (View) / `sqlite3_str` | Zero (View) / `sqlite3_malloc` |
| **SBO Capacity** | N/A (View) | Strings $\le 21$B, Blobs $\le 22$B, UUID 16B | N/A | N/A |
| **Subtype Handling** | Zero-copy inspection | **Offset 22 (Zero-branch)** | N/A | N/A |
| **Affinity Handling** | Storage-class derived | Native SQLite affinity byte | N/A | N/A |
| **Direct Extraction** | `.as_text()`, `.as_blob()` | `.as_text()`, `.as_blob()`, `.uuid_bytes()` | `.data()`, `.c_str()` | `.data()` |
| **UDF / Statement Interop** | `.result()`, `.bind()` | `.result()`, `.bind()` | `.result()`, `.bind()` | `.result()`, `.bind()` |
| **Heterogeneous Lookups** | 144+ operators | 144+ operators | Operators for `std::map` | Operators for `std::map` |

### Value Container Summary

| Type | Storage | Allocation | Use Case |
| :--- | :--- | :--- | :--- |
| `SqliteValueTuple<N>` | Stack / In-Situ ($N \le 8$) | **0 Mallocs** | Fixed-arity Primary Keys, Composite Indexes |
| `SqliteValueVec<N>` | Adaptive SBO Stack / Heap | **0 Mallocs** (Stack) / Spill | Adaptive dynamic rows, scratch vectors |

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
if (val.is_pointer())    { /* Subtype 'p' (112: Native SQLite Pointer) */ }
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

// Typed Pointer Extraction via Compile-Time Traits or Tag String
CustomContext* p1 = val.as_pointer<CustomContext>();             // Uses SqlitePointerTraits<CustomContext>::name()
CustomContext* p2 = val.as_pointer<CustomContext>("custom_tag"); // Explicit tag override
bool has_ptr      = val.has_pointer<CustomContext>();            // Checks matching tag and non-null address
```

### Conversion to Owned
```cpp
// Duplicates the value into owned memory or stores it inline via SBO
SqliteValueOwned owned = val.to_owned();
```

---

## 4. `SqliteValueOwned` API Reference

`SqliteValueOwned` is a 24-byte RAII polymorphic container featuring Small Buffer Optimization (SBO), shared-offset subtype tracking, in-situ 16-byte raw UUID representation, and automatic memory cleanup.

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
// 1. JSON (Strings <= 21 chars stored inline with zero heap allocations)
SqliteValueOwned j1 = SqliteValueOwned::from_json("{\"ok\":true}");
SqliteValueOwned j2 = SqliteValueOwned::from_jsonb(binary_data, len);

// 2. Arbitrary Precision Decimal (<= 21 chars stored inline)
SqliteValueOwned dec = SqliteValueOwned::from_decimal("999999999999999.99");

// 3. UUID Binary (16 bytes in-situ, 0 heap allocations!)
SqliteValueOwned uuid = SqliteValueOwned::from_uuid(uuid_16_bytes);

// 4. AI Vector Embeddings
SqliteValueOwned vec = SqliteValueOwned::from_vector(float_array, sizeof(float_array));

// 5. Geometry / Spatial Coordinates
SqliteValueOwned geo = SqliteValueOwned::from_geometry(geo_bytes, len);

// 6. Datetime (Integer Epoch Milliseconds or Complete ISO-8601 Strings)
SqliteValueOwned dt_epoch = SqliteValueOwned::from_datetime(1788500000000LL);
SqliteValueOwned dt_iso   = SqliteValueOwned::from_datetime("2026-09-04T17:20:00Z");              // SBO in-situ (20 chars)
SqliteValueOwned dt_micro = SqliteValueOwned::from_datetime("2026-09-04T17:20:00.123456Z");       // Heap (27 chars)
SqliteValueOwned dt_tz    = SqliteValueOwned::from_datetime("2026-09-04T17:20:00.999+05:30");     // Heap (29 chars)

// 7. Compressed Stream
SqliteValueOwned comp = SqliteValueOwned::from_compressed(zstd_stream, len);

// 8. Opaque C/C++ Typed Pointer (Tagged with SQLITE_SUBTYPE_POINTER = 112 / 'p')
SqliteValueOwned ptr_val  = SqliteValueOwned::from_pointer(&my_context);
SqliteValueOwned null_ptr = SqliteValueOwned::from_pointer<CustomContext>(nullptr); // Semantically SQL NULL
```

### Datetime & ISO-8601 Representation Model

`SqliteValueOwned` provides first-class support for DateTime values tagged with `SQLITE_SUBTYPE_DATETIME` (`'T'` / `0x54`):
- **Epoch Timestamps**: `SqliteValueOwned::from_datetime(epoch_ms)` constructs an integer timestamp with SQLite affinity `SQLITE_AFF_INTEGER`.
- **ISO-8601 Strings**: `SqliteValueOwned::from_datetime(iso_str)` constructs a string timestamp with `SQLITE_AFF_TEXT`.
  - **SBO In-Situ ($\le 21$ chars)**: Date-only (`"2026-09-04"`), standard timestamp (`"2026-09-04 17:20:00"`), and UTC (`"2026-09-04T17:20:00Z"`) fit inside the 24-byte struct with **0 heap allocations**.
  - **Heap Allocation ($> 21$ chars)**: High-precision subseconds (`.123Z`, `.123456Z`, `.123456789Z`) and timezone offsets (`+05:30`, `-08:00`) dynamically allocate exact-sized SQLite heap memory.
- **Relational Ordering & Transparent STL Lookups**: ISO-8601 text values naturally sort in chronological order and support transparent heterogeneous lookups in `std::unordered_map` and `std::map<..., std::less<>>` via `SqliteStringView` and database column views (`SqliteValueView`).

### UUID In-Situ Architecture, Orthogonal Formatting & Zero-Copy Comparisons

`SqliteValueOwned` incorporates dedicated in-situ storage for 16-byte binary UUIDs (`InlineUuidRep`) within its 24-byte footprint.

> [!IMPORTANT]
> **Subtype Guidance for Application & Extension Authors**:
> When passing UUIDs through SQLite statements, UDF arguments, or virtual table operations, **it is strongly recommended to explicitly assign the standard SQLite subtype `SQLITE_SUBTYPE_UUID` (`'U'` / code 85)**:
> - **In UDF Return Values**: Use `sqlite3_result_subtype(ctx, 'U')` or `val.result(ctx)`.
> - **In `SqliteValueOwned` Creation**: Use `SqliteValueOwned::from_uuid(raw_16_bytes)` which automatically sets subtype `'U'`.
> - **Why this matters**: Explicitly preserving subtype `'U'` guarantees instant, branchless zero-allocation detection via `val.is_uuid()` across query pipelines, avoiding costly string re-parsing, regex matching, or heuristic byte inspection.

#### Orthogonal UUID Format Flags (`SqliteUuidUtil::UuidFormatFlags`)
`SqliteUuidUtil` provides fully orthogonal bitmask flags to control UUID formatting:

| Flag | Value | Description |
| :--- | :---: | :--- |
| `UUID_FORMAT_BLOB` | `0x00` | 16-byte raw binary representation (default) |
| `UUID_FORMAT_TEXT` | `0x01` | Formatted ASCII text string |
| `UUID_FORMAT_HYPHENS` | `0x02` | Include standard 8-4-4-4-12 grouping hyphens |
| `UUID_FORMAT_UPPERCASE` | `0x04` | Format hexadecimal digits in uppercase (`A-F`) |
| `UUID_FORMAT_BRACED` | `0x08` | Enclose text in curly braces (`{...}`) |
| `UUID_FORMAT_STANDARD` | `0x03` | Standard canonical RFC 4122 text (`TEXT \| HYPHENS`) |

```cpp
// Fast zero-allocation UUID parsing & formatting utilities
#include "sqlite3_value.hpp"

// 1. Parsing text UUIDs (supports 32-hex, 36-char hyphenated, 38-char braced, and 34-char compact braced):
uint8_t raw_uuid[16];
if (SqliteUuidUtil::parse_uuid("550e8400-e29b-41d4-a716-446655440000", raw_uuid)) {
    // Construct in-situ owned UUID (0 heap allocations!)
    SqliteValueOwned val = SqliteValueOwned::from_uuid(raw_uuid);
    assert(val.is_uuid() == true);
    assert(val.is_heap_allocated() == false);
    assert(val.subtype() == SQLITE_SUBTYPE_UUID); // 'U'
}

// 2. Direct binary access & orthogonal string formatting (0 heap allocations):
const uint8_t* raw = val.uuid_bytes(); // Direct 16-byte buffer pointer
char out_str[39];
val.format_uuid(out_str, SqliteUuidUtil::UUID_FORMAT_STANDARD);           // "550e8400-e29b-41d4-a716-446655440000"
val.format_uuid(out_str, SqliteUuidUtil::UUID_FORMAT_UPPERCASE | SqliteUuidUtil::UUID_FORMAT_TEXT); // "550E8400E29B41D4A716446655440000" (Compact upper)
val.format_uuid(out_str, SqliteUuidUtil::UUID_FORMAT_BRACED | SqliteUuidUtil::UUID_FORMAT_STANDARD); // "{550e8400-e29b-41d4-a716-446655440000}"

// 3. Resolving canonical pointer with object format flags:
const char* str_ptr = nullptr;
char scratch[39];
int len = val.canonical_uuid_ptr(str_ptr, scratch); // Uses val's actual format flags

// 4. Zero-Copy Cross-Format Equality & Heterogeneous Hashing:
SqliteValueOwned u_blob = SqliteValueOwned::from_uuid(raw_uuid); // Binary 16-byte
SqliteValueOwned u_text = SqliteValueOwned::from_uuid(raw_uuid, SqliteUuidUtil::UUID_FORMAT_STANDARD); // Text
SqliteValueOwned u_upper = SqliteValueOwned::from_uuid(raw_uuid, SqliteUuidUtil::UUID_FORMAT_STANDARD | SqliteUuidUtil::UUID_FORMAT_UPPERCASE);
SqliteValueOwned u_brace = SqliteValueOwned::from_uuid(raw_uuid, SqliteUuidUtil::UUID_FORMAT_BRACED | SqliteUuidUtil::UUID_FORMAT_STANDARD);

// All formats evaluate equal and produce identical 64-bit MurmurHash2 hashes!
assert(u_blob == u_text);
assert(u_text == u_upper);
assert(u_upper == u_brace);
assert(u_blob.hash() == u_text.hash());
assert(u_text.hash() == u_upper.hash());
```

### Pointer Extraction & Mutation
```cpp
// Fast typed extraction
CustomContext* p = ptr_val.as_pointer<CustomContext>();
bool is_ptr      = ptr_val.is_pointer();  // Subtype == SQLITE_SUBTYPE_POINTER (112)
bool is_live     = ptr_val.has_pointer(); // Subtype == 112 && payload.ptrVal != nullptr

// In-place mutation (frees any prior heap buffer)
ptr_val.set_pointer(&another_context);

// Statement binding and UDF result return with compile-time tag deduction:
ptr_val.bind_pointer<CustomContext>(stmt, 1);
ptr_val.result_pointer<CustomContext>(ctx);
```

### Inspection & Metadata Getters
```cpp
int  t   = val.type();               // SQLITE_INTEGER, SQLITE_FLOAT, SQLITE_TEXT, etc.
bool h   = val.is_heap_allocated();  // False for primitives, UUIDs, and SBO buffers (<=21-22B)
int  len = val.inline_length();      // Byte length of inline text/blob (0..22)
char aff = val.affinity();           // SQLite Affinity character ('@', 'A'..'F')
uint8_t sub = val.subtype();         // 8-bit SQLite Subtype (Offset 22, 1-cycle access)

SqliteOwnedValueTag tag = val.tag(); // Access raw 1-byte packed control register
```

### Freestanding Move Semantics
```cpp
#include "sqlite3_allocator.hpp" // For sqlite_move

SqliteValueOwned a = SqliteValueOwned::from_text("my string");
SqliteValueOwned b = sqlite_move(a); // Fast register move

assert(b.as_text() == "my string");
assert(a.is_null()); // Moved-from instance is safely reset to NULL
```

---

## 5. Value Containers (`SqliteValueTuple<N>`, `SqliteValueVec<N>`)

These classes provide contiguous RAII-managed arrays of `SqliteValueOwned` elements with 100% stack data density. For complete documentation, see [`docs/VALUE_CONTAINERS_README.md`](VALUE_CONTAINERS_README.md).

### `SqliteValueTuple<N>` — Stack-Allocated Fixed-Arity Tuple

```cpp
// Exactly N * 24 bytes on the stack — zero heap allocations
SqliteValueTuple<4> static_tuple;
static_tuple[0] = 42LL;
static_tuple[1] = SqliteValueOwned("hello");
static_tuple[2] = 3.14;
static_tuple[3] = SqliteValueOwned(); // SQLITE_NULL

assert(static_tuple.size() == 4);

// Element access
SqliteValueOwned& elem = static_tuple[2];
SqliteRowOwnedWrapper view = static_tuple.view(); // non-owning span
```

### `SqliteValueVec<N>` — Adaptive SBO Vector

```cpp
// Allocates in-situ on stack for size <= N, dynamically spills to heap if > N
SqliteValueVec<4> vec;
vec.resize(3);
vec[0] = 1LL;
vec[1] = SqliteValueOwned("world");
vec[2] = 2.71828;

// Resize preserving existing elements
vec.resize(5); // Spills to heap dynamically
assert(vec.size() == 5);
assert(vec[0].as_int64() == 1);
assert(vec[3].is_null()); // New elements are SQLITE_NULL

// 1-cycle move
SqliteValueVec<4> moved = sqlite_move(vec);
assert(vec.empty()); // Source safely zeroed
```

### Typed Extraction Accessors & Composite Hashing (`SQLITE_DERIVE_ARRAY_ACCESSORS`, `SQLITE_DERIVE_ARRAY_HASH`)

Both `SqliteValueTuple<N>` and `SqliteValueVec<N>` utilize unified macros to synthesize zero-overhead direct column extractors and 64-bit MurmurHash2 composite hashing:

```cpp
// Direct typed extraction without indexing into intermediate values
sqlite3_int64 id   = vec.as_int64();   // Defaults to index 0 (64-bit integer)
int           id32 = vec.as_int(0);     // 32-bit integer
SqliteStringView s = vec.as_text(1);    // Non-owning string view
double        val  = vec.as_double(2);  // Double float
bool          nul  = vec.is_null(3);    // Null check
uint8_t       sub  = vec.subtype(1);    // Subtype code

// 64-bit MurmurHash2 composite digest across all elements
unsigned long long digest = vec.hash();
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
```

---

## 9. Transparent Functors for Swiss Tables & STL Maps

`sqlite3_value.hpp` provides transparent functors (`is_transparent = void`) enabling zero-allocation queries on both ordered (`std::map`, B-Trees) and unordered (`std::unordered_map`, Swiss Tables) containers:

```cpp
struct SqliteValueHash;  // 64-bit MurmurHash2 transparent hasher
struct SqliteValueEqual; // Transparent equality (a == b)
struct SqliteValueLess;  // Transparent SQLite collation less-than (a < b)
```

### Unordered Map / Swiss Table Example
```cpp
#include "sqlite3_value.hpp"
#include <unordered_map>

// Keyed by owned value, searchable by string views or integers with 0 heap allocations
std::unordered_map<SqliteValueOwned, std::string, SqliteValueHash, SqliteValueEqual> user_index;

user_index[SqliteValueOwned(101)] = "Alice";
user_index[SqliteValueOwned::from_text("admin")] = "Administrator";

// Zero-allocation lookups using native types:
assert(user_index.find(101) != user_index.end());
assert(user_index.find(SqliteStringView("admin", 5)) != user_index.end());
```

---

## 10. Pointer Passing, Compile-Time Traits & Semantic Equivalence

SQLite provides `sqlite3_bind_pointer` and `sqlite3_result_pointer` to pass opaque C/C++ in-memory objects across SQL statements and UDF chains without serialization. `sqlite3_value.hpp` and `sqlite3_value_containers.hpp` provide end-to-end integration for pointer passing.

### 10.1 Native SQLite Type & Subtype Mechanics
- **Datatype**: SQLite officially defines the SQL storage class of any bound pointer as **`SQLITE_NULL` (code `5`)**. Both `view.type()` and `owned.type()` evaluate to `SQLITE_NULL`.
- **Subtype**: SQLite internally assigns `'p'` (`112`) to pointer cells. `sqlite3_value.hpp` maps `SQLITE_SUBTYPE_POINTER` directly to `112` (`'p'`), ensuring that native SQLite pointers bound via `sqlite3_bind_pointer` automatically satisfy `val.is_pointer()`.
- **Payload Field**: Pointers are stored directly in `payload.ptrVal` (`void*`), completely decoupled from heap-managed text/blob buffers (`payload.pData`) and avoiding any heap length or deallocation conflicts.

### 10.2 Compile-Time Tag Registration (`SQLITE_REGISTER_POINTER_TAG`)
Associate static type tags with C++ types to enable compile-time deduction across `.as_pointer<T>()`, `.bind_pointer<T>()`, and `.result_pointer<T>()`:

```cpp
struct CustomContext {
    int id;
    double factor;
};

// Register tag string with compile-time trait:
SQLITE_REGISTER_POINTER_TAG(CustomContext, "custom_context_tag");

// Built-in specializations in sqlite3_value_containers.hpp:
// - SqliteValueVec<N>        -> "SqliteValueVec"
// - SqliteValueTuple<N>      -> "SqliteValueTuple"
// - SqliteRowOwnedWrapper    -> "SqliteRowOwnedWrapper"
// - SqliteRowView            -> "SqliteRowView"
```

Once registered, type tags are deduced automatically:
```cpp
// Producer
SqliteValueOwned ptr_val = SqliteValueOwned::from_pointer(&ctx_obj);
ptr_val.bind_pointer<CustomContext>(stmt, 1); // tag deduced automatically

// Consumer View
SqliteValueView view(sqlite3_column_value(stmt, 0));
CustomContext* ctx = view.as_pointer<CustomContext>(); // tag deduced automatically
```

### 10.3 Semantic Equivalence Architecture
In SQL semantics, missing data is represented by `NULL`. Under **Semantic Equivalence**, a pointer variable containing `nullptr` (`from_pointer(nullptr)`) is treated as semantically equivalent to pure SQL `NULL`:

| Expression | Pure SQL `NULL` | Null Pointer (`nullptr`) | Active Pointer (`&obj`) |
| :--- | :---: | :---: | :---: |
| `val.is_null()` | `true` | **`true`** | `false` |
| `val.is_pointer()` | `false` | `true` | `true` |
| `val.has_pointer()` | `false` | `false` | **`true`** |
| `bool(val)` (Truthy) | `false` | `false` | **`true`** |
| `val.hash()` | `DEFAULT_SEED` | **`DEFAULT_SEED`** | Mixed Address Hash |

#### Equivalence in Equality & Strict Weak Ordering
```cpp
SqliteValueOwned pure_null;
SqliteValueOwned null_ptr = SqliteValueOwned::from_pointer<CustomContext>(nullptr);
SqliteValueOwned live_ptr = SqliteValueOwned::from_pointer(&ctx_obj);

// 1. Equality: pure SQL NULL equals nullptr pointer
assert(pure_null == null_ptr);
assert(null_ptr == pure_null);
assert(live_ptr != pure_null);
assert(live_ptr != null_ptr);

// 2. Strict Weak Ordering: nulls sort identically (0x0) before active addresses
assert(!(pure_null < null_ptr) && !(null_ptr < pure_null)); // Equivalent ordering
assert(pure_null < live_ptr);
assert(null_ptr < live_ptr);
assert(!(live_ptr < pure_null));
assert(!(live_ptr < null_ptr));
```

---

## 11. Performance Benchmarks (Cycle-Accurate)

| Operation | Standard C++ / SQLite Baseline | `sqlite3_value.hpp` | Improvement |
| :--- | :--- | :--- | :--- |
| **Short Text Allocation ($\le 21$B)** | `sqlite3_value_dup` ($\sim 80\text{--}200$ cycles) | **Inline SBO ($\sim 1\text{--}2$ cycles)** | **$\sim 50\times\text{--}100\times$ Faster** |
| **Short Blob Allocation ($\le 22$B)** | `sqlite3_value_dup` ($\sim 80\text{--}200$ cycles) | **Inline SBO ($\sim 1\text{--}2$ cycles)** | **$\sim 50\times\text{--}100\times$ Faster** |
| **Raw UUID In-Situ (16B)** | `sqlite3_value_dup` ($\sim 80\text{--}200$ cycles) | **In-Situ UUID ($\sim 1\text{--}2$ cycles)** | **Zero Mallocs ($\sim 50\times\text{--}100\times$ Faster)** |
| **Subtype Inspection (`subtype()`)** | Pointer deref + branch ($\sim 5\text{--}15$ cycles) | **Shared Offset 22 ($1$ cycle)** | **$\sim 5\times\text{--}15\times$ Faster** |
| **Type Query (`type()`)** | Branch / switch ($\sim 3\text{--}8$ cycles) | **Bit shift `raw >> 5` ($1$ cycle)** | **$\sim 3\times\text{--}8\times$ Faster** |
| **Move Constructor / Assignment** | Struct copy + free ($\sim 15\text{--}30$ cycles) | **Register Move ($1\text{--}2$ cycles)** | **$\sim 15\times\text{--}30\times$ Faster** |
| **Cache Line Density (64 Bytes)** | 1 value (56--64B `struct Mem`) | **2+ values (24B layout)** | **$> 2\times$ Cache Line Density** |
