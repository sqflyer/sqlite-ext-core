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
segfault), and `DbRegistry::get` / `DbRegistry::init` panic only when they
need to resolve a non-null database path.

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
cargo test                          # 16 unit tests
make test-integration               # Go concurrency + lazy-load suite
make leak-check-integration         # Valgrind run (requires valgrind)
```

## Project status

`sqlite-ext-core` is **pre-1.0** and the API may change between minor
versions. The current `0.2.0` release is usable in production for
loadable extensions and has been stress-tested against the sqlite3 CLI,
rusqlite, and Go's `mattn/go-sqlite3`, but a handful of known rough
edges (listed in the roadmap below) are being worked through before a
stable `1.0` is cut. See [docs.rs](https://docs.rs/sqlite-ext-core) for
the current API surface.

## Roadmap to 1.0

The path from `0.2.0` to `1.0.0` is deliberately short: three
correctness items, two quality items, and five nice-to-haves. The hard
engineering is done; what's left is soundness polish and confidence
building.

### Must-fix (correctness blockers)

These are real soundness or correctness issues that must land before a
stable `1.0` tag.

- **Replace `static mut GLOBAL_API` / `static mut EXTENSION_API` with
  `std::sync::OnceLock`.** The current statics are written exactly once
  under a `Once` guard, which makes them sound in practice, but reading
  a `static mut` from multiple threads is technically UB under Rust's
  aliasing rules. `OnceLock<GlobalApi>` expresses the same write-once
  pattern explicitly, removes every `unsafe { GLOBAL_API }` site in the
  crate, and has zero runtime cost.
- **Fix the in-memory database collision.** Every `:memory:` connection
  resolves to the literal string `":memory:"` today, which means two
  independent in-memory databases in the same process share registry
  state. When `sqlite3_db_filename` returns null/empty, key the
  registry on the raw `db` pointer address instead (e.g. via a
  dedicated `MemKey(usize)` entry type).
- **Validate slot pointers before `transmute`.** If SQLite ever hands
  back a `sqlite3_api_routines` table where one of the slots we read is
  zero (stripped build, missing routine), the `std::mem::transmute` in
  `sqlite3_extension_init2` produces a null function pointer that
  segfaults the moment anything calls it. Wrap each slot read in a
  non-null check and panic with a clear message instead.

### Should-fix (quality polish)

- **Configurable auxdata slot.** Slot `0` is hardcoded in the
  registry's hot path. Any other extension using auxdata slot `0` for
  its own caching will clobber ours and vice versa. Expose a
  `DbRegistry::with_auxdata_slot(slot: i32)` constructor so users can
  pick a higher-numbered slot that's unlikely to conflict.
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
