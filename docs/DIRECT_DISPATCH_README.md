# Direct Dispatch Framework (`direct_dispatch_context.hpp`, `direct_dispatch_hub.hpp`)

The **Direct Dispatch Framework** provides an ultra-low-latency, in-process C++ function execution and registry layer decoupled from SQLite's VDBE (Virtual Database Engine) bytecode virtual machine. 

It allows native C++ subsystems (such as embedded key-value stores, vector indexes, bloom filters, and stream processors) to invoke user-defined functions directly with **zero bytecode evaluation overhead**, **zero dynamic heap allocation**, and complete **dual-execution compatibility** with standard SQLite SQL queries.

---

## 1. Features Matrix

| Feature | Description |
| :--- | :--- |
| **Bypass VDBE Virtual Machine** | Executes registered C++ handlers directly as raw function pointers, eliminating bytecode opcode interpretation cycles. |
| **Stack Argument Allocation** | Uses `withSqliteRowOwned` to allocate 1 to 16 arguments directly on the CPU stack frame (zero heap allocations). |
| **1:1 `SqliteContext` Parity** | `DirectDispatchContext` matches `SqliteContext` API identically (`result_int`, `result_text`, `result_blob`, `result_error`, `state<T>()`). |
| **Dual-Execution Compatibility** | Write a single templated UDF (`template <typename Context, typename Args> void my_udf(Context& ctx, Args args)`) that runs identically in SQL queries and direct C++. |
| **In-Situ Value Result** | Results are written directly into an in-situ 24-byte `SqliteValueOwned` member, avoiding pointer chasing or heap wrappers. |
| **Zero-Copy Borrowed Buffers** | `result_text` and `result_blob` support `SQLITE_STATIC` to borrow zero-copy string and binary buffers without heap allocation. |
| **Robin Hood Hash Registry** | `DirectDispatchHub` is backed by DuoSTL's freestanding `duo::HashMap<duo::String, DirectDispatchHandler>` with xxHash3 O(1) lookup. |
| **Uniform Overloaded APIs** | `register_function`, `register_udf`, `find`, `contains`, `erase`, `dispatch`, and `invoke` support `const char*`, `duo::String`, and `duo::StringView`. |
| **Freestanding & `-nostdlib++`** | 100% header-only, zero dependencies on standard library runtime heaps (`<functional>`, `<vector>`, `<memory>`). |
| **Exception-Free Safety** | Completely functional under `-fno-exceptions` and `-fno-rtti` with deterministic SQLite error code propagation. |

---

## 2. Quickstart Tutorial

### 2.1 Basic Function Registration & Direct Dispatch

```cpp
#include "direct_dispatch_context.hpp"
#include "direct_dispatch_hub.hpp"

// Define a stateless direct handler
static void add_handler(DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
    if (args.size() < 2) {
        ctx.result_error("add requires 2 arguments", SQLITE_MISUSE);
        return;
    }
    sqlite3_int64 a = args[0].as_int64();
    sqlite3_int64 b = args[1].as_int64();
    ctx.result_int64(a + b);
}

void demo_basic_dispatch() {
    // Register function into the global static hub:
    DirectDispatchHub::register_function("add", add_handler);

    // Dispatch with 2 stack-allocated arguments:
    DirectDispatchContext ctx;
    bool ok = DirectDispatchHub::dispatch("add", 2, [](SqliteRowOwnedWrapper row) {
        row[0] = 100LL;
        row[1] = 250LL;
    }, &ctx);

    if (ok) {
        printf("Result: %lld\n", ctx.result().as_int64()); // 350
    }
}
```

---

### 2.2 Dual-Execution UDF (SQL VDBE $\leftrightarrow$ Direct C++)

Write a single templated function that can be bound to SQLite for SQL queries and registered in `DirectDispatchHub` for high-throughput in-memory execution:

```cpp
#include "sqlite3_udf.hpp"
#include "direct_dispatch_hub.hpp"

// Unified templated UDF implementation
template <typename Context, typename Args>
void udf_bloom_check(Context& ctx, Args args) {
    if (args.size() < 2) {
        ctx.result_error("bloom_check requires (key, id)", SQLITE_MISUSE);
        return;
    }
    const char* key = args[0].as_text().data();
    int id = args[1].as_int();
    
    // Simulate bloom filter check
    bool match = (key && key[0] == 'u' && (id % 2 == 0));
    ctx.result_int(match ? 1 : 0);
}

void register_dual_udf(sqlite3* db) {
    // 1. Register for Direct In-Process C++ Dispatch (cross-extension static registry)
    DirectDispatchHub::register_udf<udf_bloom_check<DirectDispatchContext, SqliteRowOwnedWrapper>>("bloom_check");

    // 2. Register for Standard SQLite SQL Queries (VDBE)
    SqliteUdf::define(db, "bloom_check", 2, [](SqliteContext& ctx, SqliteUdfArgs args) {
        udf_bloom_check(ctx, args);
    });
}
```

---

### 2.3 Invocation with Pre-Populated Spans (`invoke`)

When argument values are already held in a span or vector, use `invoke()` to bypass stack allocation:

```cpp
void demo_invoke() {
    DirectDispatchContext ctx;

    SqliteValueOwned args[2] = {
        SqliteValueOwned(40LL),
        SqliteValueOwned(60LL)
    };
    SqliteRowOwnedWrapper row_span(args, 2);

    if (DirectDispatchHub::invoke("add", ctx, row_span)) {
        printf("Sum: %lld\n", ctx.result().as_int64()); // 100
    }
}
```

---

### 2.4 Overloaded APIs (`const char*`, `duo::String`, `duo::StringView`)

`DirectDispatchHub` seamlessly interoperates with zero-copy string views and DuoSTL strings:

```cpp
void demo_string_overloads(DirectDispatchContext& ctx) {
    // const char*
    DirectDispatchHub::dispatch("add", 2, [](SqliteRowOwnedWrapper row) {
        row[0] = 10; row[1] = 20;
    }, &ctx);

    // duo::String
    duo::String str_name("add");
    DirectDispatchHub::dispatch(str_name, 2, [](SqliteRowOwnedWrapper row) {
        row[0] = 10; row[1] = 20;
    }, &ctx);

    // duo::StringView (zero-copy string slice)
    duo::StringView sv_name("add");
    DirectDispatchHub::dispatch(sv_name, 2, [](SqliteRowOwnedWrapper row) {
        row[0] = 10; row[1] = 20;
    }, &ctx);
}
```

---

### 2.5 Accessing Shared Extension State (`ctx.state<T>()`)

`DirectDispatchContext` supports connection-scoped state resolution matching `SqliteContext`:

```cpp
struct AppCache {
    int hits = 0;
    int misses = 0;
};

static void cache_lookup(DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
    AppCache* cache = ctx.state<AppCache>();
    if (cache) {
        cache->hits++;
    }
    ctx.result_int(cache ? cache->hits : -1);
}
```

---

### 2.6 High-Performance Lua Scripting Interoperability

A powerful use case for `DirectDispatchHub` is serving as a **zero-overhead bridge between embedded Lua scripts and native C++ UDFs**. 

Instead of routing Lua function calls through SQL statements (`db:exec("SELECT my_func(...)")`), a single generic Lua C-API binding can dispatch directly into the C++ hub:

```cpp
// Generic Lua C-API dispatcher: my_ext.call("function_name", arg1, arg2, ...)
static int lua_direct_dispatch(lua_State* L) {
    sqlite3* db = static_cast<sqlite3*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* func_name = luaL_checkstring(L, 1);
    int argc = lua_gettop(L) - 1; // Number of arguments passed from Lua

    DirectDispatchContext ctx(db);
    bool ok = DirectDispatchHub::dispatch(func_name, argc, [&](SqliteRowOwnedWrapper row) {
        for (int i = 0; i < argc; ++i) {
            int lua_idx = i + 2;
            int type = lua_type(L, lua_idx);
            if (type == LUA_TNUMBER) {
                if (lua_isinteger(L, lua_idx)) {
                    row[i] = static_cast<sqlite3_int64>(lua_tointeger(L, lua_idx));
                } else {
                    row[i] = static_cast<double>(lua_tonumber(L, lua_idx));
                }
            } else if (type == LUA_TSTRING) {
                size_t len = 0;
                const char* str = lua_tolstring(L, lua_idx, &len);
                // Zero-copy: borrow the string directly from Lua's stack memory
                row[i] = SqliteValueOwned::borrow_text(str, static_cast<int>(len));
            } else if (type == LUA_TBOOLEAN) {
                row[i] = SqliteValueOwned(lua_toboolean(L, lua_idx) ? 1LL : 0LL);
            } else {
                row[i] = SqliteValueOwned(); // SQL NULL
            }
        }
    }, &ctx);

    if (!ok || ctx.is_error()) {
        return luaL_error(L, ctx.error_message() ? ctx.error_message() : "Direct dispatch failed");
    }

    // Push in-situ result directly to Lua stack
    const auto& res = ctx.result();
    if (res.is_integer()) {
        lua_pushinteger(L, res.as_int64());
    } else if (res.is_float()) {
        lua_pushnumber(L, res.as_double());
    } else if (res.is_text()) {
        auto text = res.as_text();
        lua_pushlstring(L, text.data(), text.size());
    } else if (res.is_blob()) {
        auto blob = res.as_blob();
        lua_pushlstring(L, static_cast<const char*>(blob.data()), blob.size());
    } else {
        lua_pushnil(L);
    }
    return 1;
}
```

#### Key Benefits for Lua Environments:
1. **Zero SQL Parsing Overhead**: Bypasses SQL lexing, grammar parsing, query optimization, and VDBE bytecode generation.
2. **Zero Dynamic Allocation**: Lua stack arguments map directly onto the CPU stack frame via `withSqliteRowOwned`.
3. **Zero-Copy String Passing**: Lua strings are borrowed directly via `SqliteValueOwned::borrow_text` without `malloc` or string cloning.
4. **Single Shared Registry**: Both SQL queries (`SELECT my_udf(...)`) and Lua scripts (`ext.call("my_udf", ...)`) share the exact same C++ codebase and per-database state (`ctx.state<T>()`).

---

## 3. Core Classes Reference

### 3.1 `DirectDispatchContext`

| Method | Signature | Description |
| :--- | :--- | :--- |
| `DirectDispatchContext` | `explicit DirectDispatchContext(sqlite3* db = nullptr, void* user_data = nullptr)` | Initializes context with optional DB and user data pointers. |
| `db()` / `db_handle()` | `sqlite3* db() const noexcept` | Retrieves bound raw SQLite database handle. |
| `user_data()` | `void* user_data() const noexcept` | Retrieves bound user data pointer. |
| `state<T>()` | `T* state() noexcept` | Resolves per-database shared state via `SqliteExtState<T>`. |
| `conn_state<T>()` | `T* conn_state() noexcept` | Resolves per-connection private state via `SqliteConnState<T>`. |
| `hybrid_state<Ext, Conn, LockPolicy>()` | `auto hybrid_state() noexcept` | Resolves unified hybrid state (shared per-db + private per-connection). |
| `result_int(int)` | `void result_int(int val) noexcept` | Sets 32-bit integer result. |
| `result_int64(int64)` | `void result_int64(sqlite3_int64 val) noexcept` | Sets 64-bit integer result. |
| `result_double(double)` | `void result_double(double val) noexcept` | Sets IEEE-754 double precision result. |
| `result_null()` | `void result_null() noexcept` | Resets result to `SQLITE_NULL`. |
| `result_text(...)` | `void result_text(const char*, int, void(*)(void*))` | Sets UTF-8 text result (`SQLITE_STATIC` or `SQLITE_TRANSIENT`). |
| `result_blob(...)` | `void result_blob(const void*, int, void(*)(void*))` | Sets binary blob result (`SQLITE_STATIC` or `SQLITE_TRANSIENT`). |
| `result_pointer(...)` | `void result_pointer(void* ptr, const char* name, void(*destructor)(void*))` | Sets typed C/C++ opaque pointer result. |
| `result_error(...)` | `void result_error(const char* msg, int code = SQLITE_ERROR)` | Sets error message and status code. |
| `result()` | `const SqliteValueOwned& result() const noexcept` | Const reference to in-situ result value. |
| `take_result()` | `SqliteValueOwned take_result() noexcept` | Moves ownership of result out of context. |
| `is_error()` | `bool is_error() const noexcept` | Returns true if an error was reported. |

---

### 3.2 `DirectDispatchHub` (Static Class)

`DirectDispatchHub` is a purely static utility class (`DirectDispatchHub() = delete;`) providing a centralized registry where all extensions built with `sqlite-ext-core` can register their functions. All internal registry access is synchronized with an atomic `SqliteTinyLock`, while handler execution runs lock-free.

| Method | Signature | Description |
| :--- | :--- | :--- |
| `register_function` | `static bool register_function(Name, DirectDispatchHandler)` | Thread-safely registers raw function pointer (`const char*`, `duo::String`, or `duo::StringView`). Returns `false` if already registered. |
| `register_udf<Fn>` | `static bool register_udf<Fn>(Name)` | Thread-safely registers templated or free function via non-type template parameter. |
| `unregister_function` | `static bool unregister_function(Name) noexcept` | Thread-safely unregisters a function by name. |
| `unregister` | `static bool unregister(Name) noexcept` | Alias for `unregister_function`. |
| `unregister_udf` | `static bool unregister_udf(Name) noexcept` | Template/alias for unregistering UDFs. |
| `find` | `static DirectDispatchHandler find(Name) noexcept` | O(1) Robin Hood hash lookup returning function pointer or `nullptr`. |
| `contains` | `static bool contains(Name) noexcept` | Checks if function is currently registered. |
| `dispatch` | `static bool dispatch(Name, int argc, ArgFiller&&, DirectDispatchContext*)` | Stack-allocates `argc` arguments via `withSqliteRowOwned` and dispatches handler outside locks. |
| `invoke` | `static bool invoke(Name, DirectDispatchContext&, SqliteRowOwnedWrapper)` | Invokes handler using pre-existing argument wrapper span outside locks. |
| `erase` | `static bool erase(Name) noexcept` | Removes a registered function from the registry. |
| `size()` / `empty()` | `static size_t size()`, `static bool empty()` | Returns registry item count / emptiness. |
| `clear()` | `static void clear() noexcept` | Clears all registered functions from the global registry. |
