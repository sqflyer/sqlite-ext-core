# sqlite-ext-core

[![crates.io](https://img.shields.io/crates/v/sqlite-ext-core.svg)](https://crates.io/crates/sqlite-ext-core)
[![docs.rs](https://img.shields.io/docsrs/sqlite-ext-core)](https://docs.rs/sqlite-ext-core)
[![license](https://img.shields.io/crates/l/sqlite-ext-core.svg)](https://github.com/sqflyer/sqlite-ext-core/blob/main/LICENSE)

A minimal, zero-dependency Rust toolkit for building SQLite loadable
extensions. It eliminates FFI boilerplate by giving you raw SQLite types, a
dynamic API resolver, inline C-mirror wrappers, and a per-database
shared-state registry — all with no transitive crate dependencies, no
static link to libsqlite3, and a ~2ns hot path for per-row state retrieval.

## Installation

```bash
cargo add sqlite-ext-core
```

or in `Cargo.toml`:

```toml
[dependencies]
sqlite-ext-core = "0.2"

[lib]
crate-type = ["cdylib"]   # required for a loadable extension
```

## Documentation

- **[README](README.md)** — you are here; API overview and quickstart.
- **[architecture.md](architecture.md)** — design document: layered
  architecture, lookup layering, ownership model, dynamic API resolution,
  and thread-safety story.
- **[leaks.md](leaks.md)** — how valgrind leak-checking is set up, what
  the non-zero categories in a passing run mean, and how to debug leaks
  if the check ever fails.
- **[docs.rs](https://docs.rs/sqlite-ext-core)** — auto-generated API
  reference for the latest published version.

## Key features

- **Pure-std, zero dependencies.** Production builds pull in nothing beyond
  `sqlite-ext-core` itself. No `libsqlite3-sys`, no `dashmap`, no `ahash`.
- **Dynamic API resolution.** One call to `sqlite3_extension_init2(p_api)`
  in your extension entry point unpacks the routine table SQLite hands you
  and enables every wrapper in the crate. Works in hosts like Go's
  `mattn/go-sqlite3` and Python's `sqlite3` module, where SQLite symbols
  are not exported to the dynamic linker.
- **C-mirror wrappers.** 20+ inline functions (`sqlite3_result_int64`,
  `sqlite3_value_text`, `sqlite3_create_function_v2`, …) that mirror the
  libsqlite3-sys API name-for-name, so porting from C is a search-and-replace.
- **Per-database state registry.** `DbRegistry<T>` isolates state by
  database file path, shares it across connections to the same file, and
  cleans itself up via RAII the moment the last connection closes.
- **Nanosecond-scale hot path.** Steady-state lookups bypass the registry
  map entirely via SQLite's `auxdata` slot: one pointer dereference, no
  hashing, no locking. Achieves ~2ns per retrieval inside tight scalar
  function loops.

## Module layout

| Module     | Contents                                                         |
|------------|------------------------------------------------------------------|
| `ffi`      | Raw FFI types, fn-pointer aliases, `SLOT_*` indices              |
| `api`      | `GlobalApi`, `ExtensionApi`, `sqlite3_extension_init2`           |
| `wrappers` | Inline C-mirror wrappers (`sqlite3_result_*`, `sqlite3_value_*`) |
| `registry` | `DbRegistry`, `State`, `destructor_bridge`                       |

All public items are re-exported at the crate root, so
`sqlite_ext_core::sqlite3`, `sqlite_ext_core::DbRegistry`, and friends
resolve directly without reaching into submodules.

## Example: a per-database shared counter

### 1. Define your state and registry

```rust
use sqlite_ext_core::{
    sqlite3, sqlite3_context, sqlite3_value, sqlite3_result_int64,
    sqlite3_user_data, sqlite3_extension_init2, sqlite3_create_function_v2,
    DbRegistry, State, SQLITE_OK, SQLITE_UTF8, destructor_bridge,
};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::LazyLock;

pub struct SharedState {
    pub counter: AtomicUsize,
}

/// Process-wide registry: maps each db file path to a `SharedState`.
static REGISTRY: LazyLock<DbRegistry<SharedState>> = LazyLock::new(DbRegistry::new);
```

### 2. Implement the scalar function

```rust
unsafe extern "C" fn test_counter_func(
    ctx: *mut sqlite3_context,
    _argc: std::os::raw::c_int,
    _argv: *mut *mut sqlite3_value,
) {
    // Recover the state handle from `pApp` with zero lookup overhead.
    let p_app = sqlite3_user_data(ctx);
    let state = State::<SharedState>::clone_from_raw(p_app);

    let val = state.counter.fetch_add(1, Ordering::SeqCst);
    sqlite3_result_int64(ctx, (val + 1) as i64);
}
```

### 3. Consolidated entry point

```rust
#[no_mangle]
pub unsafe extern "C" fn sqlite3_myext_init(
    db: *mut sqlite3,
    _pz_err_msg: *mut *mut std::os::raw::c_char,
    p_api: *const std::os::raw::c_void,
) -> std::os::raw::c_int {
    // 1. Resolve all SQLite function pointers process-wide.
    sqlite3_extension_init2(p_api);

    // 2. Initialize or retrieve shared state for this database.
    let state = REGISTRY.init(None, db, || SharedState {
        counter: AtomicUsize::new(0),
    });

    // 3. Anchor the state to this connection via pApp + xDestroy.
    sqlite3_create_function_v2(
        db,
        b"test_counter\0".as_ptr() as *const _,
        0,
        SQLITE_UTF8,
        state.into_raw(),                     // pApp = leaked Arc refcount
        Some(test_counter_func),
        None, None,
        Some(destructor_bridge::<SharedState>), // dropped when connection closes
    );

    SQLITE_OK
}
```

The flow that makes this fast: `state.into_raw()` leaks one `Arc` refcount
into SQLite's `pApp` slot. Inside `test_counter_func`, `clone_from_raw`
recovers a temporary `State` without touching that refcount. When the
connection closes, SQLite fires `destructor_bridge`, which takes ownership
back and drops the refcount. If it was the last one, `InternalEntry::drop`
removes the registry entry automatically — no manual cleanup, no leaks.

## Ordering requirement

Call `sqlite3_extension_init2` **before** any other API in this crate. The
inline wrappers panic if they're used before init (clear panic, not a
segfault), and `DbRegistry::get` / `DbRegistry::init` panic on a null `db`
pointer.

## Auxdata slot used by the hot path

The registry's nanosecond-scale per-row lookup works by caching an `Arc`
pointer into SQLite's `sqlite3_auxdata` table under a single integer
slot. This is the mechanism that makes `DbRegistry::get` essentially
free inside a tight scalar-function loop: one pointer dereference, no
hashing, no locking.

Picking the right slot matters. SQLite's `sqlite3_set_auxdata(ctx, N, …)`
uses `N` as a key into a per-statement slot table, and by convention
scalar functions use slot `N` to cache the parsed form of `argv[N]` —
a compiled regex from `argv[0]` goes into slot `0`, a parsed JSON path
from `argv[1]` goes into slot `1`, and so on. Any library that picks a
low-numbered slot for its own purposes collides with this convention.

### `DEFAULT_AUXDATA_SLOT`

`sqlite-ext-core` exposes a compile-time constant:

```rust
pub const DEFAULT_AUXDATA_SLOT: c_int =
    (fnv1a_hash(b"sqlite-ext-core") & 0x7FFF_FFFF) as c_int;
```

This resolves at compile time to **`816_545_397`** (`0x30AB7E75`) — the
[FNV-1a hash](https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function)
of the literal string `"sqlite-ext-core"` with the sign bit masked off
so the value is a non-negative `i32`. Three things fall out of this
choice:

- **Far above the argument-index range.** `SQLITE_LIMIT_FUNCTION_ARG`
  caps at 32767 absolute maximum; `816_545_397` is four orders of
  magnitude higher, so it cannot collide with any scalar function's
  conventional argument-caching slot. A `const { assert!(…) }` in the
  source pins this invariant at compile time — if the crate ever gets
  renamed to something whose FNV hash lands below the ceiling, the
  build fails with a clear message rather than silently shipping a
  collision-prone default.
- **Unique by construction.** Any other library that follows the same
  "hash your crate name" convention gets a completely different
  default slot, without coordination. Two libraries picking
  `i32::MAX` would collide silently; two libraries hashing their
  distinct crate names will not.
- **Reproducible and auditable.** There's no magic number. Anyone can
  re-derive the value from the string `"sqlite-ext-core"` and verify
  it matches what the crate ships.

Under the hood, SQLite 3.30+ stores auxdata as a linked list of
`AuxData` structs (not a dense array indexed by `N`), so picking a
large slot number costs nothing in memory — it's a single list entry.

### Overriding the slot

If you know another library in your process already uses
`DEFAULT_AUXDATA_SLOT`, or you want two `DbRegistry` instances in the
same process to avoid sharing auxdata state, construct with an
explicit slot:

```rust
use sqlite_ext_core::DbRegistry;

// Pick any non-negative i32 that doesn't collide with other slots
// in your process. Staying above 32767 keeps you out of the
// argument-caching convention range.
let registry = DbRegistry::<MyState>::with_auxdata_slot(0x4000_0000);
```

`DbRegistry::new()` is equivalent to
`DbRegistry::with_auxdata_slot(DEFAULT_AUXDATA_SLOT)`, so most users
never need to touch this.

## Choosing `T` for your `DbRegistry<T>`

`DbRegistry<T>`, `State<T>`, and `destructor_bridge<T>` all carry a
**type-level** bound `T: SharedState`, which is a self-documenting marker
trait for `Send + Sync + 'static` with a blanket impl:

```rust
pub trait SharedState: Send + Sync + 'static {}
impl<T: Send + Sync + 'static> SharedState for T {}
```

**You never implement `SharedState` manually** — any type that is already
`Send + Sync + 'static` satisfies it automatically. The trait exists so
that bounds read as `T: SharedState` instead of `T: Send + Sync + 'static`
and so that compiler errors point at a single meaningful trait name.

The bound is enforced at the *struct* level, not the impl level, so
writing `DbRegistry<Rc<usize>>` fails at the type declaration — before
you even call a method:

```text
error[E0277]: `Rc<usize>` cannot be sent between threads safely
 --> src/main.rs:6:12
  |
6 |     let _: DbRegistry<Rc<usize>>;
  |            ^^^^^^^^^^^^^^^^^^^^^ `Rc<usize>` cannot be sent between threads safely
  |
  = note: required for `Rc<usize>` to implement `SharedState`
note: required by a bound in `DbRegistry`
```

### Why each piece of the bound exists

A single registry is shared across every SQLite connection in the process,
so `T` is read and mutated from whichever threads the host (rusqlite, Go's
`mattn/go-sqlite3`, a thread pool, …) uses to execute queries. Each
sub-bound buys something concrete:

- **`Send`** — the `Arc<T>` inside every `State<T>` can move between
  threads as connections migrate across workers.
- **`Sync`** — `State<T>` derefs to `&T`, and multiple threads can hold
  live `State<T>` handles simultaneously, so concurrent `&T` access must
  be sound.
- **`'static`** — the registry outlives any given connection (it is
  process-wide), so `T` must not borrow from anything with a shorter
  lifetime.

### Common shapes for `T`

Since `T` is shared and needs interior mutability for almost any real use
case, pick the shape that matches your data:

| Shape of your state                  | Good choice for `T`                           |
|--------------------------------------|-----------------------------------------------|
| A single counter or flag             | `AtomicUsize`, `AtomicBool`, …                |
| A small struct with atomic fields    | `struct { a: AtomicU64, b: AtomicBool }`      |
| Arbitrary mutable state              | `Mutex<Inner>` or `RwLock<Inner>`             |
| Read-mostly config loaded once       | `ArcSwap<Inner>` (from the `arc-swap` crate)  |
| Lock-free maps / queues              | `DashMap`, `crossbeam` channels, …            |

A plain `Cell<T>` / `RefCell<T>` is **not** enough — neither is `Sync`,
and the compiler will reject them at the bound. Reach for `Mutex` instead.

## Build system

| Command                            | What it does                                      |
|------------------------------------|---------------------------------------------------|
| `make test`                        | Rust unit tests (`cargo test`)                    |
| `make test-integration`            | Go concurrency + lazy-load stress tests           |
| `make leak-check-integration`      | Valgrind run against a C harness (zero leaks)     |
| `make leak-check-valgrind`         | `cargo valgrind test`                             |
| `make coverage`                    | Coverage report via `cargo tarpaulin`             |
| `make clean`                       | Prune all `target/` and `bin/` artifacts          |

## Verification

The integration suite exercises the registry under realistic loads:

- **Concurrency.** 75+ concurrent connections across 3 databases, 100
  iterations each — Go stress test validates strict per-database state
  isolation and counter consistency.
- **Lazy loading.** Dynamic `LoadExtension` on already-open connections,
  same concurrency shape.
- **RAII safety.** Valgrind-confirmed zero `definitely lost` bytes via
  `destructor_bridge` against a C-driven harness. See
  [leaks.md](leaks.md) for the full breakdown of what the leak check
  actually measures and why.

```bash
cargo test                          # 17 unit tests + 1 doctest
make test-integration               # Go concurrency + lazy-load suite
make leak-check-integration         # Valgrind run (requires valgrind)
```

## Project status

`sqlite-ext-core` is **pre-1.0** and the API may change between minor
versions. The current `0.2.x` line has landed every correctness
blocker that was originally on the path to 1.0 — it is sound under
Rust's strict aliasing rules, isolates in-memory databases correctly,
and rejects null-pointer misuse at the public API boundary. It has
been stress-tested against the sqlite3 CLI, rusqlite, and Go's
`mattn/go-sqlite3`. What remains before cutting `1.0.0` is one quality
item (a direct Rust-side test for `sqlite3_extension_init2`) plus a
small set of nice-to-haves (benchmarks, CI, docs.rs polish). See the
roadmap below for the full status. Current API surface is on
[docs.rs](https://docs.rs/sqlite-ext-core).

## Roadmap to 1.0

The path from `0.2.x` to `1.0.0` is deliberately short. All correctness
blockers have landed; what remains is one quality item plus five
nice-to-haves.

### Must-fix (correctness blockers) — all landed

- **✅ Replace `static mut GLOBAL_API` / `static mut EXTENSION_API`
  with `std::sync::OnceLock`.** *Landed in 0.2.x.* Both API tables are
  now `OnceLock<GlobalApi>` / `OnceLock<ExtensionApi>` with race-free
  reads and no `unsafe { STATIC }` sites in the crate. The wrappers
  module routes through a single private `api()` helper that unwraps
  the `OnceLock` with a clear panic if init was skipped.
- **✅ Fix the in-memory database collision.** *Landed in 0.2.x.*
  `get_raw_db_path` now returns a `Cow<'a, str>` keyed on
  `":memory:\0<ptr>"` for any handle whose `sqlite3_db_filename`
  resolves to null/empty. The embedded NUL byte is the collision-proof
  part — SQLite's `sqlite3_db_filename` returns a C string, which by
  definition cannot contain interior NULs, so a real filesystem path
  can never collide with an in-memory key. As a side benefit, null
  `db` pointers are now rejected at the `DbRegistry::get`/`init`
  public API boundary with a named panic, so accidental misuse fails
  fast instead of routing into a shared "no database" slot.
- **✅ Validate slot pointers before `transmute`.** *Landed in 0.2.x.*
  Each slot read in `sqlite3_extension_init2` now goes through a
  private `resolve_slot<T>(slots, offset, name)` helper that asserts
  the raw `usize` is non-zero and panics with the routine name and
  offset if not. A `const { assert!(size_of::<T>() ==
  size_of::<usize>()) }` enforces at compile time that every caller
  resolves a pointer-sized type. Stripped or incompatible SQLite
  builds now panic at init with a clear diagnostic instead of
  segfaulting on first FFI call.

### Should-fix (quality polish)

- **✅ Configurable auxdata slot.** *Landed in 0.2.x.* The hot-path
  cache now uses [`DEFAULT_AUXDATA_SLOT`](#auxdata-slot-used-by-the-hot-path)
  (the FNV-1a hash of the crate name, resolving to `816_545_397` —
  well above the argument-caching convention range), and
  `DbRegistry::with_auxdata_slot(slot)` lets callers override it.
  See the "Auxdata slot used by the hot path" section above for the
  full rationale.
- **Rust-side test for `sqlite3_extension_init2`.** The slot-walking
  code is currently only exercised by the Go integration tests; the
  Rust unit tests bootstrap `GLOBAL_API` / `EXTENSION_API` via a
  `libsqlite3-sys` shim in `setup_api()` that bypasses the real init
  path. Add a unit test that constructs a fake `sqlite3_api_routines`
  struct in memory and feeds it to `sqlite3_extension_init2` directly.

### Nice-to-have (1.0 quality bar, not correctness)

- **Benchmarks.** The README claims "~2ns hot-path latency" but there
  is no `criterion` bench to back it up. Add a benchmark group that
  measures warm-auxdata `get()`, cold-hash `get()`, and first-call
  `init()`. Either validate the 2ns claim or update it.
- **Aggregate / window function example.** The underlying
  `sqlite3_create_function_v2` supports `xStep` / `xFinal`, so
  aggregate and window functions work with the current API — but the
  README only shows a scalar function. Add an example (e.g. a
  per-database `total()` aggregate) so users have a copy-paste
  starting point.
- **CI with miri + valgrind.** Wire `make leak-check-integration` into
  GitHub Actions alongside `cargo test` and a miri run over the pure
  `destructor_bridge` / `InternalEntry::drop` refcount math. Catches
  future regressions automatically instead of relying on a manual
  `make leak-check-integration` before release.
- **`cargo semver-checks` gate.** Once `1.0.0` is cut, run
  `cargo semver-checks` in CI on every PR so breaking API changes can't
  land by accident.
- **docs.rs landing page polish.** Convert the crate-level doc block in
  `lib.rs` so it includes the full scalar-function example from the
  README as a compiling doctest. Guarantees the example never rots and
  gives docs.rs a proper landing page.

### Deliberately out of scope for 1.0

- **Virtual tables, hooks, aggregate-function abstractions.** The
  underlying C calls are all accessible via the current wrappers, but
  `sqlite-ext-core` will not ship a high-level framework for them
  before 1.0. They are a substantial new surface area and should not
  block the scalar-function + per-database-state story that 1.0 is
  focused on.
- **`Result`-based error handling.** The panic-on-misuse design is
  internally consistent and matches how C extensions already work.
  There is no plan to add a `Result<(), CoreError>` layer unless a
  concrete user asks for it.
- **`no_std` support.** SQLite itself requires libc, a filesystem, and
  a host process — every environment that can load a SQLite extension
  has `std` available. `no_std` buys nothing for this library's
  specific niche; see the `no_std` discussion in
  [architecture.md](architecture.md).

## License

Licensed under the [MIT License](LICENSE).
