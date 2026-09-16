# Direct Dispatch Architecture (`direct_dispatch_context.hpp`, `direct_dispatch_hub.hpp`)

This document details the internal architecture, execution model, memory layout, and stack-allocation mechanics of the Direct Dispatch Framework.

---

## 1. Architectural Motivation: Why Direct Dispatch?

SQLite's standard User-Defined Function (UDF) engine is engineered around the **VDBE (Virtual Database Engine)**. In this classic architecture, invoking a function requires:

1. SQL statement preparation and compilation into bytecode instructions (`OP_Function`).
2. Opcode dispatch loop inside `sqlite3_step()`.
3. Packing arguments into dynamic arrays of `sqlite3_value*` pointers.
4. Setting up a VDBE-managed `sqlite3_context` handle.
5. Invoking the C callback function through function pointer indirection.
6. Unpacking the result from `sqlite3_result_*` and pushing it onto the VDBE evaluation stack.

While optimal for SQL query processing and relational filtering, this pipeline incurs measurable overhead for high-frequency in-process operations (such as in-memory bloom filter lookups, graph traversals, vector similarities, or key-value point reads).

**Direct Dispatch** eliminates this entire virtual machine pipeline, executing native C++ handlers with zero bytecode interpretation and zero heap allocation.

---

## 2. Pipeline Comparison

```
STANDARD SQLITE VDBE PIPELINE:
[ SQL Query ] 
      │
      ▼
[ SQLite Parser / Planner ]
      │
      ▼
[ VDBE Bytecode Generation (OP_Function) ]
      │
      ▼
[ VDBE Execution Loop ] ──── (Instruction decoding, opcode dispatch)
      │
      ▼
[ sqlite3_value** Packing ] ── (Heap/VDBE register allocations)
      │
      ▼
[ sqlite3_context Setup ] ── (VDBE memory frame setup)
      │
      ▼
[ C Trampoline Proxy ]
      │
      ▼
[ User UDF Execution ]

────────────────────────────────────────────────────────────────────────────

DIRECT DISPATCH PIPELINE:
[ Native C++ Request ]
      │
      ▼
[ DirectDispatchHub::dispatch("name", argc, filler) ]
      │
      ├─► O(1) Robin Hood Hash Lookup in duo::HashMap (xxHash3)
      │
      ├─► CPU Stack Allocation via withSqliteRowOwned (1..16 args on stack frame)
      │
      ├─► DirectDispatchContext (Result held in-situ: 24B SqliteValueOwned)
      │
      ▼
[ Direct C++ Handler Execution (Raw Function Pointer) ]
      │
      ▼
[ Result immediately available on stack with 0 heap frees ]
```

---

## 3. Memory & Allocation Architecture

### 3.1 In-Situ Result Storage (`DirectDispatchContext`)

Unlike `SqliteContext` which wraps a pointer to a VDBE-allocated `sqlite3_context`, `DirectDispatchContext` embeds its result directly inside the object:

```cpp
class DirectDispatchContext {
private:
    sqlite3*         m_db;
    void*            m_user_data;
    SqliteValueOwned m_result;     // Exactly 24 bytes, embedded in-situ
    const char*      m_error_msg;
    int              m_error_code;
    bool             m_has_error;
    // ...
};
```

#### Memory Layout (64-bit Architecture):
```
Offset 0x00: sqlite3* m_db                     (8 bytes)
Offset 0x08: void* m_user_data                 (8 bytes)
Offset 0x10: SqliteValueOwned m_result         (24 bytes)
             ├─ Payload Union (i64/dbl/ptr/pData) [8 bytes]
             ├─ int32_t heap_len                  [4 bytes]
             ├─ char affinity                     [1 byte]
             ├─ uint8_t reserved[8]               [8 bytes]
             ├─ bool is_borrowed                  [1 byte]
             ├─ SqliteOwnedValueSubTag subtag     [1 byte]
             └─ SqliteOwnedValueTag tag           [1 byte]
Offset 0x28: const char* m_error_msg           (8 bytes)
Offset 0x30: int m_error_code                  (4 bytes)
Offset 0x34: bool m_has_error                  (1 byte)
Offset 0x35: [padding]                         (3 bytes)
Total Size: 56 bytes (Stack-allocated, 0 heap allocations)
```

Because `m_result` is embedded by value:
- Calling `ctx.result_int(42)`, `ctx.result_double(3.14)`, or `ctx.result_null()` performs zero pointer indirection.
- Reading `ctx.result()` accesses the value immediately without cache misses.
- `take_result()` transfers ownership in $O(1)$ move operations without copying heap buffers.

---

### 3.2 Stack Argument Allocation (`withSqliteRowOwned`)

The `DirectDispatchHub::dispatch` method leverages `withSqliteRowOwned` to allocate argument rows entirely on the active CPU stack:

```cpp
template <typename ArgFiller>
inline bool dispatch_impl(DirectDispatchHandler handler, int argc, ArgFiller&& filler, DirectDispatchContext* out_ctx) {
    return withSqliteRowOwned(argc, [&](SqliteRowOwnedWrapper row) {
        filler(row);
        // ... invoke handler ...
    });
}
```

#### Mechanics of `withSqliteRowOwned`:
1. **$0 \le \text{argc} \le 16$ (Fast Stack Path)**:
   - A contiguous buffer of `alignas(SqliteValueOwned) uint8_t stack_buf[16 * sizeof(SqliteValueOwned)]` is allocated in the current stack frame (384 bytes).
   - Elements are initialized in-place using placement-new.
   - Zero heap allocations (`malloc`/`free`) occur.
2. **$\text{argc} > 16$ (Heap Fallback Path)**:
   - For unusually wide argument lists ($>16$), a temporary heap buffer is allocated and safely destroyed via RAII upon scope exit.
3. **Destruction Guarantee**:
   - `withSqliteRowOwned` automatically calls destructors for all active argument values upon exiting the lambda scope, preventing leaks of transient text or blob allocations.

---

### 3.3 Zero-Copy Borrowed Buffers (`SQLITE_STATIC`)

When returning text or blob data with `SQLITE_STATIC`, `DirectDispatchContext` avoids copying string buffers:

```cpp
void result_text(const char* val, int n_bytes = -1, void (*destructor)(void*) = SQLITE_STATIC) noexcept {
    if (destructor == SQLITE_STATIC) {
        // Zero allocation: directly borrows the buffer pointer
        m_result = SqliteValueOwned::borrow_text(val, n_bytes);
    } else {
        // Deep copy into owned SBO/heap string
        m_result = SqliteValueOwned(val, n_bytes);
    }
}
```

The embedded `bool is_borrowed` flag in `SqliteValueOwned` ensures that borrowed static buffers are never accidentally passed to `sqlite3_free` or system `free`.

---

## 4. Dual-Execution Architecture

A central design pillar of the framework is **Dual-Execution Compatibility**: enabling developers to write single, type-agnostic UDF implementations that compile for both SQLite SQL queries and Direct Dispatch.

```
                              ┌───────────────────────────────────┐
                              │ Templated Function Implementation │
                              │ template <typename Context,       │
                              │           typename Args>          │
                              │ void udf_fn(Context&, Args)       │
                              └─────────────────┬─────────────────┘
                                                │
                     ┌──────────────────────────┴──────────────────────────┐
                     ▼                                                     ▼
      ┌─────────────────────────────┐                       ┌─────────────────────────────┐
      │   SqliteUdf::define (SQL)   │                       │  DirectDispatchHub (Direct) │
      ├─────────────────────────────┤                       ├─────────────────────────────┤
      │ Context = SqliteContext     │                       │ Context =                   │
      │ Args    = SqliteUdfArgs     │                       │   DirectDispatchContext     │
      │ Execution = VDBE Bytecode   │                       │ Args    =                   │
      │                             │                       │   SqliteRowOwnedWrapper     │
      │                             │                       │ Execution = Direct C++ Call │
      └─────────────────────────────┘                       └─────────────────────────────┘
```

### Context Comparison Matrix

| Capability | `SqliteContext` (VDBE) | `DirectDispatchContext` (Direct) |
| :--- | :--- | :--- |
| **Backing Engine** | SQLite VDBE Virtual Machine | Standalone Native C++ Stack |
| **Result Storage** | Indirect via `sqlite3_result_*` | In-situ 24-byte `SqliteValueOwned` |
| **Error Handling** | Sets VDBE error flag & message | Records error code & message in-situ |
| **Shared State** | `ctx.state<T>()` via `SqliteExtState` | `ctx.state<T>()` via `SqliteExtState` |
| **Conn State** | `ctx.conn_state<T>()` via `SqliteConnState` | `ctx.conn_state<T>()` via `SqliteConnState` |
| **DB Handle** | `ctx.db()` returns `sqlite3*` | `ctx.db()` returns `sqlite3*` |
| **Subtypes** | `sqlite3_result_subtype()` | Stored in `SqliteValueOwned` subtag byte |
| **Memory Cleanup** | Handled by VDBE instruction cleanup | Handled by stack frame unwinding |

### Template Argument Best Practice

When instantiating `register_udf`, specify the plain context type without reference qualifiers:
```cpp
// RECOMMENDED:
hub.register_udf<udf_fn<DirectDispatchContext, SqliteRowOwnedWrapper>>("name");

// AVOID (Redundant reference qualifier):
hub.register_udf<udf_fn<DirectDispatchContext&, SqliteRowOwnedWrapper>>("name");
```
Because `udf_fn` takes `Context& ctx`, specifying `Context = DirectDispatchContext` causes the parameter to deduce cleanly as `DirectDispatchContext& ctx`.

---

## 5. Registry & Dispatcher Architecture (`DirectDispatchHub`)

### 5.1 Internal Container: DuoSTL Robin Hood Hash Map

`DirectDispatchHub` uses `duo::HashMap<duo::String, DirectDispatchHandler>` for function registration and lookup:

- **Freestanding C++17**: Compiled with zero standard library headers (`-nostdlib++`).
- **Robin Hood Open Addressing**: Uses Distance-to-Initial-Bucket (DIB) displacement algorithm to guarantee tight probe-length variance ($O(1)$ lookup).
- **Zero-Tombstone Backward-Shift Deletion**: Removing entries via `erase()` performs backward-shifting rather than inserting tombstone markers, preserving optimal query performance across heavy churn.
- **xxHash3 Hashing**: Computes 64-bit non-cryptographic hashes with SIMD-vectorized throughput.

### 5.2 Uniform Overload Strategy

To maximize caller flexibility and avoid unnecessary string copying, all entry-point methods provide three uniform overloads:

1. `const char*`: Null-terminated C string (wrapped via SBO `duo::String(name)`).
2. `const duo::String&`: Existing owned DuoSTL string.
3. `duo::StringView`: Lightweight, non-owning string slice (`{const char* data, size_t len}`).

Internally, dispatch and invoke route into centralized implementation helpers:
```cpp
dispatch(const char*, ...)      ──┐
dispatch(const duo::String&, ...) ──┼──► dispatch_impl(find(name), argc, filler, out_ctx)
dispatch(duo::StringView, ...)  ──┘

invoke(const char*, ...)        ──┐
invoke(const duo::String&, ...)   ──┼──► invoke_impl(find(name), ctx, args)
invoke(duo::StringView, ...)    ──┘
```

---

## 6. Exception-Free Determinism (`-fno-exceptions`)

The entire framework operates strictly under `-fno-exceptions` and `-fno-rtti`:
- If an invalid argument count is detected, `ctx.result_error("...", SQLITE_MISUSE)` records the error deterministically.
- If a function name is not found in the registry, `out_ctx->result_error("...", SQLITE_NOTFOUND)` is recorded and `false` is returned.
- Memory allocation failures during string/blob conversions set `SQLITE_NOMEM` via `result_error_nomem()`.
- Callers check `ctx.is_error()`, `ctx.error_code()`, and `ctx.error_message()` without exception catching overhead.

---

## 7. Embedded Scripting Architecture: Lua Engine Integration

A major architectural use case for `DirectDispatchHub` is serving as a **zero-overhead execution bridge for embedded scripting engines like Lua / LuaJIT**.

### 7.1 The Traditional vs Direct Dispatch Architectural Bridge

```
TRADITIONAL LUA-TO-SQLITE INTEROP:
[ Lua Script ] ──► lua_pcall()
                         │
                         ▼
             [ sqlite3_prepare_v2("SELECT my_func(?)") ]
                         │
                         ▼
             [ SQL Parser / VDBE Bytecode Generation ]
                         │
                         ▼
             [ sqlite3_bind_* / sqlite3_step ]
                         │
                         ▼
             [ sqlite3_column_* Extraction ] ──► Push to Lua Stack
             (High latency: SQL parsing, statement caching, VDBE registers)

────────────────────────────────────────────────────────────────────────────

DIRECT DISPATCH LUA BRIDGE:
[ Lua Script ] ──► my_ext.call("my_func", arg1, arg2)
                         │
                         ▼
             [ Single Generic C-API Dispatcher ]
                         │
                         ▼
             [ DirectDispatchHub::dispatch ]
                         │
        ┌────────────────┴────────────────┐
        ▼                                 ▼
[ Lua Stack Strings ]             [ CPU Stack Frame ]
(lua_tolstring)                   (withSqliteRowOwned)
        │                                 │
        ▼                                 ▼
SqliteValueOwned::borrow_text     Contiguous 384B Buffer
(100% Zero-Copy Borrowing)        (Zero Heap Allocations)
        │                                 │
        └────────────────┬────────────────┘
                         ▼
             [ Direct C++ Handler Execution ]
                         │
                         ▼
             [ In-Situ Result: ctx.result() ]
                         │
                         ▼
             [ Direct push to Lua Stack (lua_push*) ]
```

### 7.2 Zero-Copy Borrowing from Lua Stack Memory
Lua manages strings in an internal string intern pool on its VM heap. Calling `lua_tolstring(L, idx, &len)` returns a direct pointer to this null-terminated character buffer.

Instead of copying these bytes into dynamic memory, `DirectDispatchHub` wraps the pointer via `SqliteValueOwned::borrow_text(ptr, len)`. 
- **Flags set**: `is_borrowed = true`.
- **Heap traffic**: Exactly 0 bytes allocated.
- **Lifetime guarantee**: The Lua stack frame is pinned for the entire duration of the C-API call, guaranteeing pointer validity while the C++ UDF executes.

### 7.3 Unified Dual Registry (SQL + Lua)
Because both SQLite queries and Lua scripts invoke the exact same C++ handlers through `DirectDispatchHub`:
- Extension developers write the business logic **once**.
- Shared state (`ctx.state<T>()` and `ctx.conn_state<T>()`) is synchronized across SQL and Lua threads.
- Performance matches hand-written C extensions while preserving the safety, bounds checking, and lifecycle management of SQLite.

