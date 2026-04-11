# Architecture

This document describes what `sqlite-ext-core` exists to solve, how the
pieces fit together, and why the design is shaped the way it is. It is
aimed at contributors and at users who want to understand *how* the
library works, not just *how to call it* — the README covers the latter.

---

## 1. The problem this library solves

SQLite extensions live in an awkward corner of the library-design space.
Three things are simultaneously true about a loadable extension:

1. **It is loaded once per process.** When `sqlite3_load_extension` or
   `sqlite3_auto_extension` fires your `sqlite3_<name>_init` function,
   the shared library is mapped into memory and stays there. Whatever
   globals you initialize during init live for the rest of the process's
   life.

2. **It is used across many database connections.** A long-running
   server (web app, query engine, replication pipeline) will open and
   close many SQLite connections over time, sometimes concurrently,
   sometimes against different database files. Every one of those
   connections goes through the same registered scalar/aggregate
   functions your extension provided at init time.

3. **Each connection has its own state needs.** The state your extension
   cares about — a counter, a cache, a cryptographic key, a connection
   to a backing service, anything — usually needs to be *per-database*,
   not per-process. Two different `.sqlite` files mean two different
   instances of your state. Two connections to the same file should
   share one instance.

These three facts pull against each other. The natural thing to write
from inside an init function is a `static` variable — but a static
variable is *per-process*, which means every database file shares the
same state, which is almost always wrong. The naive fix, "allocate new
state per connection", is also wrong: two connections to the same
database now have divergent state.

There is also a fourth, subtler pitfall. Extensions loaded via
`sqlite3_load_extension` often run inside hosts that **did not export
SQLite symbols to the dynamic linker**. Go's `mattn/go-sqlite3`, Python's
`sqlite3` module, Redis's module system, and several embedded runtimes
all statically link their own libsqlite3 and do not publish its symbols.
If your extension is a Rust crate that uses `libsqlite3-sys`, the
`.so` will fail to load at `dlopen` time with an undefined-symbol error
the moment you try to actually deploy it under one of those hosts. This
makes "just use `libsqlite3-sys`" a non-starter for any extension that
wants to work in more than one host.

The goal of `sqlite-ext-core` is to solve both problems at once:
per-database shared state with correct lifetimes, and a calling
convention that works in every host that can call `dlopen`.

---

## 2. A small example of the mess

Consider a scalar function `count_calls()` that returns how many times
it has been invoked against the current database. Written naively:

```rust
// BROKEN: shared across all databases in the process
static COUNTER: AtomicUsize = AtomicUsize::new(0);

unsafe extern "C" fn count_calls(ctx: *mut sqlite3_context, _: c_int, _: *mut *mut sqlite3_value) {
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    sqlite3_result_int64(ctx, n as i64);
}
```

Open two different databases, call `count_calls()` once against each.
You get `0` and `1`, not `0` and `0`. That is the "per-process static"
trap.

The correct-feeling-but-also-broken fix is to allocate state in the init
function:

```rust
// BROKEN: new state every time the extension is loaded on a new connection
#[no_mangle]
pub unsafe extern "C" fn sqlite3_myext_init(db: *mut sqlite3, ...) -> c_int {
    let state = Box::into_raw(Box::new(AtomicUsize::new(0)));
    sqlite3_create_function_v2(db, c"count_calls", 0, SQLITE_UTF8,
        state as *mut c_void, Some(count_calls), None, None, Some(free_state));
    SQLITE_OK
}
```

This correctly isolates state per connection — but two connections to
the *same* database file now have two separate counters. For most
real extension use cases, you want them to share.

`sqlite-ext-core` turns the second version into "one `State<T>` per
database file, shared automatically across every connection to that
file, cleaned up automatically when the last connection closes":

```rust
use sqlite_ext_core::{DbRegistry, State, destructor_bridge,
                      sqlite3_create_function_v2, SQLITE_UTF8};
use std::sync::LazyLock;

static REGISTRY: LazyLock<DbRegistry<AtomicUsize>> = LazyLock::new(DbRegistry::new);

#[no_mangle]
pub unsafe extern "C" fn sqlite3_myext_init(db: *mut sqlite3, ...) -> c_int {
    sqlite3_extension_init2(p_api);
    let state = REGISTRY.init(None, db, || AtomicUsize::new(0));
    sqlite3_create_function_v2(db, c"count_calls", 0, SQLITE_UTF8,
        state.into_raw(), Some(count_calls), None, None,
        Some(destructor_bridge::<AtomicUsize>));
    SQLITE_OK
}
```

Open two different databases → two counters. Open five connections to
the same database → one shared counter. Close the last connection →
counter is dropped and the registry slot is freed. No manual cleanup,
no lifetime bookkeeping, no global-state trap.

---

## 3. Layered architecture

`sqlite-ext-core` is split into four layers, each of which is small and
has a single responsibility. Each layer only depends on the ones below
it:

```
┌────────────────────────────────────────────────────────────────────┐
│  registry     DbRegistry<T>, State<T>, InternalEntry<T>,           │
│               destructor_bridge<T>, SharedState, get_raw_db_path   │
│                         ↓ depends on                               │
├────────────────────────────────────────────────────────────────────┤
│  wrappers     sqlite3_result_*, sqlite3_value_*, sqlite3_user_data │
│               sqlite3_context_db_handle, sqlite3_create_function_v2│
│                         ↓ depends on                               │
├────────────────────────────────────────────────────────────────────┤
│  api          GlobalApi, ExtensionApi, GLOBAL_API, EXTENSION_API,  │
│               sqlite3_extension_init2(p_api)                       │
│                         ↓ depends on                               │
├────────────────────────────────────────────────────────────────────┤
│  ffi          sqlite3, sqlite3_context, sqlite3_value,             │
│               function-pointer type aliases, SLOT_* indices,       │
│               SQLITE_OK, SQLITE_UTF8                               │
└────────────────────────────────────────────────────────────────────┘
```

**[`ffi`](src/ffi.rs)** — Pure data. Opaque `#[repr(C)]` handle types,
function-pointer type aliases that mirror `sqlite3ext.h`, and the slot
indices used to walk the `sqlite3_api_routines` table. Has no
dependencies, calls no functions, allocates no memory.

**[`api`](src/api.rs)** — Holds two process-wide function-pointer tables
(`GlobalApi`, `ExtensionApi`) and exposes `sqlite3_extension_init2`, the
function an extension's init routine uses to populate them. Once the
tables are populated, every other layer routes its FFI calls through
them. See section 6 for why this matters.

**[`wrappers`](src/wrappers.rs)** — Inline, zero-cost wrappers that
mirror the libsqlite3 C API (`sqlite3_result_int64`, `sqlite3_value_text`,
`sqlite3_create_function_v2`, …) but dispatch through the resolved
`ExtensionApi` pointer instead of through a static link. These exist
purely for ergonomics — scalar-function code written against this layer
reads identically to code written against `libsqlite3-sys`, but works
in every host regardless of symbol visibility.

**[`registry`](src/registry.rs)** — The meat of the library: the
per-database state registry (`DbRegistry<T>`), the user-facing handle
type (`State<T>`), the internal bookkeeping struct (`InternalEntry<T>`),
the C-interop destructor (`destructor_bridge<T>`), the `SharedState`
marker trait, and the path-extraction helper. This is where the lookup
layering and RAII lifecycle management lives.

---

## 4. The lookup layering (the hot path)

The single most important performance property of `sqlite-ext-core` is
that retrieving the per-database state from inside a tight scalar-function
loop is effectively free — roughly one pointer dereference plus one
`Arc` refcount increment. The mechanism that makes this possible is
`DbRegistry::get`'s three-layer lookup:

```
                 caller: scalar_fn_impl(ctx, argc, argv)
                                │
                                ▼
                   DbRegistry::get(Some(ctx), db)
                                │
               ┌────────────────┴──────────────────┐
               │                                   │
          ctx provided?                       ctx is None
               │                                   │
          ┌────▼────┐                              │
          │ Layer 1 │       (hot path)             │
          │ auxdata │  ← sqlite3_get_auxdata(ctx,0)│
          │  check  │                              │
          └────┬────┘                              │
               │                                   │
       ┌───────┴───────┐                           │
       │ slot non-null │                           │
       │     ↓ yes     │                           │
       │  cast to Arc, │                           │
       │  clone, return│ ──── ~2ns, no lock        │
       └───────────────┘                           │
               │                                   │
           slot null                               │
               ▼                                   ▼
          ┌──────────────────────────────────────────┐
          │            Layer 2 (warm path)           │
          │  get_raw_db_path(db) → "main.sqlite"     │
          │  registry.map.lock() → HashMap::get(&s)  │
          │  weak.upgrade() → Option<State<T>>       │
          └──────────────────────────────────────────┘
                             │
                     hit? ──────── no ──→ return None
                             │
                            yes
                             │
                             ▼
          ┌──────────────────────────────────────────┐
          │   Write back to auxdata so the next row  │
          │   hits layer 1 instead of layer 2        │
          │   sqlite3_set_auxdata(ctx, 0, arc_ptr,   │
          │                     destructor_bridge)   │
          └──────────────────────────────────────────┘
                             │
                             ▼
                        return Some(state)
```

The key insight is **SQLite's `auxdata` slot is query-scoped storage
that the database engine maintains itself.** When SQLite prepares a
statement and starts stepping through rows, each invocation of a scalar
function gets access to a small array of slots that persist for the
lifetime of that particular statement execution. The engine
auto-invokes a destructor function when the statement finalizes, so you
can stash an `Arc` refcount into it and SQLite will drop it correctly
for you.

The first row through `DbRegistry::get` misses the auxdata cache, walks
the hash map, gets the answer, and caches the `Arc` pointer back into
the auxdata slot. Every subsequent row in the same statement — which
in a typical analytical query means thousands or millions of rows —
skips the hash map entirely and retrieves the state with a single
pointer dereference. The `Mutex<HashMap>` the registry is built on is
essentially cold storage; it gets touched at most a few times per
query, never inside the hot loop.

`DbRegistry::init` has a fourth layer above these three for the
first-use-ever case: if the hash map lookup also misses, it runs the
`init_fn` closure under the lock, inserts a new `Weak<InternalEntry<T>>`
into the map, and then also writes the fresh `Arc` into auxdata. After
that first call, every subsequent row on every subsequent connection
falls through to either layer 1 or layer 2, not layer 3.

---

## 5. Ownership model and RAII cleanup

The lifecycle of a single `InternalEntry<T>` walks through several
different forms of ownership. Understanding this chain is the only way
to understand why the various `unsafe` pieces in [`registry.rs`](src/registry.rs)
are actually sound.

```
  (1) init closure runs                    → T is created
        ↓
  (2) wrapped in Arc<InternalEntry<T>>     → ref = 1 (strong)
        ↓
  (3) Weak inserted into registry map      → map holds a Weak (non-owning)
        ↓
  (4) State<T> handed back to user code    → ref = 1 (strong, user-held)
        ↓
  (5) user calls state.into_raw()          → ref = 1 (strong, but now owned by C)
        ↓                                    the Arc pointer is leaked into
                                             SQLite's pApp slot; Rust no longer
                                             tracks it

  (6) scalar function fires N times:
        clone_from_raw(pApp)               → temporarily reconstruct the Arc,
                                             clone it to get a second refcount
                                             for this callback's scope,
                                             std::mem::forget the first to
                                             leave SQLite's refcount untouched
        → returned temporary State<T>      → ref = 2 (temp)
        ↓
      temporary dropped at end of callback → ref = 1 (back to SQLite-only)

  (7) connection closes:
        SQLite fires xDestroy on pApp      → calls destructor_bridge<T>(ptr)
        destructor_bridge reconstructs
        and drops the Arc                  → ref = 0
        ↓
      Arc's drop glue runs                 → InternalEntry::drop fires
        ↓
      InternalEntry::drop locks the map    → identity-checks the Weak
                                             under its path, removes the
                                             entry only if it still points
                                             at self (race-safe)
        ↓
      Arc frees InternalEntry<T>           → T's drop runs, path Arc<str>
                                             drops, Weak<RegistryMap<T>>
                                             drops, done.
```

The only tricky step is **(6)**. When a scalar function fires, SQLite
hands us back the raw `*mut c_void` we leaked into `pApp` at step (5).
We need to read from that `Arc<InternalEntry<T>>` without consuming
SQLite's refcount, because the next row's scalar function invocation
still expects the same pointer to be live. The dance is:

1. `Arc::from_raw(raw)` — reconstructs the Rust-side view of the Arc.
   This does *not* increment the refcount; it creates a Rust value that
   thinks it owns exactly one refcount.
2. `arc.clone()` — bumps the refcount from 1 to 2. The clone is the
   temporary `State<T>` we'll return.
3. `std::mem::forget(arc)` — leaks the original Rust-side Arc so that
   its drop never runs, which would have decremented the refcount back
   down to 1 and then on function return dropped the clone back to 0
   — prematurely destroying state that SQLite still owns.

The net effect: we get a temporary `State<T>` whose refcount represents
*only* the borrow we're using in this callback, while SQLite's refcount
in `pApp` is preserved undisturbed. When the temporary goes out of
scope at the end of the callback, its drop decrements refcount back to
1, which is exactly where it was before the call. This is what makes
[`State::clone_from_raw`](src/registry.rs) sound.

Step **(7)** is the other delicate one. `InternalEntry::drop` is
responsible for removing the entry from the registry's hash map, but
only if the entry is *actually* the one currently indexed under its
path. The race to defend against: if a new connection opens the same
database *between* the last `State` being dropped (at step 7 above) and
*before* `InternalEntry::drop` actually runs (say, because of a
multi-threaded scheduler delay), the registry's hash map may already
contain a fresh `Weak` for the new connection's state. Blindly removing
the entry under this path would clobber the new connection's slot. The
identity check — comparing the `Weak`'s stored pointer against
`self as *const InternalEntry<T>` — makes cleanup no-op in exactly
this case and correct in every other case.

---

## 6. Dynamic SQLite API resolution

The piece of `sqlite-ext-core` that makes it work in hosts like Go and
Python is the dynamic FFI routing layer in [`src/api.rs`](src/api.rs).
This section explains what's happening and why it's necessary.

### The problem

Imagine you write a SQLite extension in Rust using `libsqlite3-sys`:

```rust
use libsqlite3_sys::{sqlite3_result_int64, sqlite3_context};

unsafe extern "C" fn my_func(ctx: *mut sqlite3_context, ...) {
    sqlite3_result_int64(ctx, 42);
}
```

When your crate is compiled into a `.so`, the call to `sqlite3_result_int64`
becomes a relocation entry in the ELF's `.rela.plt` section. At load time,
the dynamic linker walks the symbol tables of every loaded shared
library looking for a symbol named `sqlite3_result_int64`. If it finds
one, the relocation is bound to it.

In a C program that dynamically links against `libsqlite3.so.0` — for
example, the sqlite3 CLI — this works, because libsqlite3 is a separate
shared library with all its symbols exported. Your `.so` gets loaded
after libsqlite3, finds `sqlite3_result_int64` in libsqlite3's symbol
table, and everything resolves.

In a Go program using `mattn/go-sqlite3`, it doesn't. go-sqlite3 uses
cgo to **statically compile libsqlite3 into the Go binary itself**. The
binary contains all the libsqlite3 symbols internally, but by default
cgo does not mark them as globally exported for `dlopen`-loaded libraries
to see. When your Rust `.so` is loaded into the Go process via
`sqlite3_load_extension`, the dynamic linker walks the symbol tables
of loaded libraries, cannot find `sqlite3_result_int64` anywhere it's
allowed to look, and the load fails with an undefined-symbol error.

Python's `sqlite3` module, Redis's module system, most embedded SQLite
integrations — they all have the same shape. SQLite is inside them, but
its symbols are not handed out.

### The solution

SQLite was designed with exactly this problem in mind. When the engine
invokes your `sqlite3_<name>_init` function, it passes three arguments:
the database handle, an error-message output pointer, and **a
`sqlite3_api_routines*`** — a struct of function pointers. That struct
is the set of SQLite routines you are allowed to call, provided by the
host. It has been carefully maintained as append-only for decades:
every version of SQLite that has ever shipped has the same prefix of
the same function pointers at the same offsets.

If your extension only ever calls SQLite through pointers pulled out of
that struct, it doesn't matter whether the host exports symbols or not.
The pointers the host gives you *are* the routines you need.

`sqlite-ext-core` implements this strategy in a single layer:

1. [`src/ffi.rs`](src/ffi.rs) defines slot-index constants
   (`SLOT_GET_AUXDATA = 61`, `SLOT_RESULT_INT64 = 83`, and so on) that
   match the layout of the `sqlite3_api_routines` struct in SQLite's
   `sqlite3ext.h`.
2. [`src/api.rs`](src/api.rs) defines two structs — `GlobalApi` (the
   handful of routines the registry itself needs) and `ExtensionApi`
   (the larger surface area most scalar-function code needs) — and
   exposes `sqlite3_extension_init2(p_api)`, which walks the slot table
   by offset and transmutes each entry into a typed function pointer.
   The pointers are stored in two process-wide statics behind a
   `std::sync::Once`.
3. [`src/wrappers.rs`](src/wrappers.rs) provides inline shims —
   `sqlite3_result_int64`, `sqlite3_value_text`, and so on — that look
   exactly like the corresponding `libsqlite3-sys` calls but dispatch
   through `EXTENSION_API.unwrap()` instead of through a static link.

After `sqlite3_extension_init2(p_api)` runs once at init time, every
subsequent FFI call from the extension walks a single extra indirection
(one load from a static, one function-pointer call) compared to the
statically-linked version. The extra load from a read-only static is
free in practice; the CPU's branch predictor and cache handle it
perfectly. The call site is inline so the optimizer can see through
the full chain.

### Why it's in a library and not in your init function

The slot-walking logic and the function-pointer type aliases are
annoying to write correctly, boring to maintain, and easy to get subtly
wrong (pick the wrong slot offset and you have a function-pointer UB
landmine that only goes off on specific input). It is exactly the kind
of thing that should be written once, tested, and never rewritten. The
scalar-function implementations in user code only need to call
`sqlite3_extension_init2(p_api)` at the top of their init function and
then everything just works.

---

## 7. Thread-safety and concurrency

SQLite extensions run on whatever thread the host happens to be using
to execute the current query. This could be:

- The main thread, in a single-threaded CLI tool.
- A dedicated database thread, in a connection-pooled server.
- A different thread for every connection, in Go's goroutine-per-request
  model.
- Potentially different threads for different rows of the same query,
  in exotic parallel-query engines.

`sqlite-ext-core` assumes any of these could be the case and makes
everything safe to touch from any thread at any time. There is no
"call this only from the init thread" rule. The details:

**`GLOBAL_API` and `EXTENSION_API`** are written exactly once, under a
`std::sync::Once` guard, by whichever thread first calls
`sqlite3_extension_init2`. After that they are never mutated again.
Every reader observes a consistent snapshot. This is technically a
`static mut` read with an `unsafe` block, and in stricter modern Rust
(`OnceLock`) you'd model this as an explicitly write-once cell — doing
that is on the 1.0 punchlist — but the current shape is sound under
the write-once invariant.

**`DbRegistry<T>`** is internally an `Arc<Mutex<HashMap<...>>>`. The
mutex is held for a handful of nanoseconds during the cold-path hash
map operations (cache miss, init, drop). The hot path — auxdata
retrieval — does not touch the mutex at all. Real-world contention is
effectively zero even under heavy multi-threaded load; the Go
integration test hits the registry with 75 concurrent connections
across 3 databases running 100 queries each, and the mutex is a
non-factor.

**`State<T>`** is `Arc<InternalEntry<T>>` underneath, which is `Send`
and `Sync` iff `T: Send + Sync`. The `SharedState` marker trait enforces
`T: Send + Sync + 'static` at the type level, so the bound is visible
at the struct declaration site and the compiler catches non-thread-safe
`T` (`Rc`, `Cell`, `RefCell`) at the type instantiation site, not at
method call time.

**Interior mutability on `T`** is the user's responsibility. The
registry doesn't know what kind of writes you want to do, so `T` has to
provide its own synchronization: `AtomicU64`, `Mutex<Inner>`,
`RwLock<Inner>`, `ArcSwap<Inner>`, `DashMap`, whatever matches your
access pattern. See the "Choosing `T`" section in the README.

---

## 8. What makes this useful

Taken together, the design above gives you the following properties
from one small library:

**You write extensions like you would in C, but in Rust.** The wrapper
functions (`sqlite3_result_int64`, `sqlite3_value_text`, …) have the
exact same names and signatures as their `libsqlite3-sys` counterparts,
so porting an existing C extension is mostly search-and-replace on the
import line plus switching from `NULL` to `None`. There is no special
Rust framework to learn.

**Your extension works in every host that can run SQLite extensions.**
A single `.so` built against `sqlite-ext-core` loads correctly under
the sqlite3 CLI, under rusqlite, under go-sqlite3, under Python's
`sqlite3`, and under any other host that passes a valid
`sqlite3_api_routines*` to `sqlite3_<name>_init`. You don't need to
ship multiple builds for different host ABIs.

**Per-database state works the way humans expect.** Open two databases,
get two independent states. Open five connections to the same database,
share one state. Close the last connection, state is freed automatically
via RAII. There is no manual tracking of open handles, no "when do I
free this" question, and no way to leak state short of deliberately
calling `State::into_raw` more times than you call `destructor_bridge`.

**The hot path is fast.** The auxdata cache turns steady-state scalar
function calls into a single pointer dereference plus an `Arc` clone —
~2ns on a modern x86 — regardless of how much state the registry is
holding. The `Mutex<HashMap>` exists to make the first row per statement
correct, not to serve every row.

**The slow path is also correct.** The first call on a new connection,
or the first call after a connection has closed and the cache is stale,
walks the full lookup chain with full locking and full identity checks,
and the state it returns is guaranteed to be the one currently
registered for that database path.

**Zero dependencies in production.** The entire crate compiles to pure
`std` + your own code. No `libsqlite3-sys`, no `dashmap`, no `ahash`,
no `once_cell`, nothing. This matters enormously for extensions, where
every transitive dependency is a future symbol-clash waiting to happen
once you load the `.so` into an unfamiliar host.

**Type-level safety for `T`.** The `SharedState` marker trait is
enforced at the struct declaration site. `DbRegistry<Rc<usize>>` fails
to compile before you even call a method, with an error that cites
`SharedState` and points to exactly which sub-bound (`Send`, `Sync`,
`'static`) is unmet. This catches the "I used a non-thread-safe cell
in a shared-across-connections registry" mistake at compile time
instead of shipping a runtime race.

---

## 9. What this library deliberately does not do

To keep the scope honest, a list of things `sqlite-ext-core` is not and
probably should not become:

- **Not a SQLite binding for application code.** If you're writing a
  web service that talks to a SQLite database, you want `rusqlite`, not
  this.
- **Not a replacement for `libsqlite3-sys` in consumer code.** This
  crate's wrappers only work *inside* an extension, after
  `sqlite3_extension_init2` has populated the dynamic function tables.
  Outside of that context they panic.
- **Not a framework for virtual tables, hooks, or the aggregate/window
  function machinery.** The underlying `sqlite3_create_function_v2` call
  supports all of those (via `xStep`/`xFinal`), and
  `sqlite-ext-core` exposes the wrapper, but there is no high-level
  abstraction. If you want to write a vtable module, you're still
  writing most of the `sqlite3_module` boilerplate by hand.
- **Not a `no_std` crate.** The design deliberately targets hosted
  environments (anything that can run a SQLite host process already
  has `std`). See the `no_std` discussion in the README.
- **Not an async runtime or executor.** Scalar functions are
  synchronous, row-by-row, on whatever thread the host picked.

---

## 10. Where to go next

- **For a working code example** — see the README, or the full
  integration-test extension in
  [`tests/integration/rust_extension/src/lib.rs`](tests/integration/rust_extension/src/lib.rs).
  It's ~90 lines and demonstrates every piece of the API end-to-end.
- **For the lookup path in detail** — read [`src/registry.rs`](src/registry.rs)
  top to bottom. Start with the module-level doc, then follow
  `DbRegistry::get` and `DbRegistry::init`. The whole file is ~420
  lines including generous comments.
- **For the FFI layer** — read [`src/api.rs`](src/api.rs) and
  [`src/wrappers.rs`](src/wrappers.rs). Together they are about 250
  lines and the core mechanism (`sqlite3_extension_init2`) is under 40.
- **For how leak-checking works** — see [`leaks.md`](leaks.md).
- **For what's on the road to 1.0** — see the "honest rough edges" and
  the punchlist sections of the last few design conversations archived
  in the repo history.
