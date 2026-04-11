# Leak-check guide

This document explains how `sqlite-ext-core` is validated for memory leaks,
what the tooling actually checks, and why the non-zero numbers that valgrind
still reports after a clean run are **not** bugs. If you change the leak-check
harness, the Makefile, or the internals of `DbRegistry`, read this first.

## TL;DR

```bash
make leak-check-integration
```

Expected tail:

```
LEAK SUMMARY:
   definitely lost: 0 bytes in 0 blocks
   indirectly lost: 0 bytes in 0 blocks
     possibly lost: 116 bytes in 1 blocks
    still reachable: 3,939 bytes in 10 blocks
         suppressed: 104 bytes in 1 blocks

ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

The only line that matters is **`definitely lost: 0`** and
**`ERROR SUMMARY: 0 errors`**. Everything else is expected and explained
below.

## What the leak check actually does

The check lives in two files:

- [`tests/integration/rust_extension/leak_check.c`](tests/integration/rust_extension/leak_check.c)
  — a minimal C program that loads the Rust extension into an in-memory
  SQLite database, runs the extension's scalar function twice, and exits
  cleanly.
- [`Makefile`](Makefile#L11) — the `leak-check-integration` target that
  builds the extension, compiles the C harness, and runs it under valgrind.

The harness exercises the complete lifecycle that `sqlite-ext-core` cares
about:

1. `sqlite3_open(":memory:")` — create a database connection.
2. `sqlite3_load_extension(...)` — triggers `sqlite3_myext_init` inside our
   `.so`, which calls `sqlite3_extension_init2(p_api)` and
   `REGISTRY.init(...)` to populate per-database state, then registers a
   scalar function with `State::into_raw()` as `pApp` and
   `destructor_bridge::<T>` as `xDestroy`.
3. `sqlite3_prepare_v2 / sqlite3_step / sqlite3_finalize` — executes the
   scalar function, which goes through the full hot-path / warm-path
   auxdata caching machinery in `DbRegistry::get`.
4. `sqlite3_close(db)` — tells SQLite to tear down. SQLite fires `xDestroy`
   (`destructor_bridge`), which drops the last `Arc<InternalEntry<T>>`,
   which in turn triggers `InternalEntry::drop`, which removes the entry
   from the registry's `HashMap`.
5. `sqlite3_shutdown()` — releases libsqlite3's process-wide state
   (page cache, mutex subsystem, pcache1). Required — see below.

This covers every interesting allocation path in the crate: extension
init, registry insert, auxdata caching, scalar-function dispatch, and RAII
cleanup via `destructor_bridge`.

## Why valgrind specifically

Three reasons:

- **It sees everything.** Sanitizers (ASan/LSan) only work inside a
  single ELF object with a shared runtime, which is awkward when your
  extension is a `.so` loaded by a C harness. Valgrind instruments the
  entire process regardless of who allocated what.
- **It reports by category.** "Definitely lost" vs "indirectly lost" vs
  "possibly lost" vs "still reachable" is exactly the granularity you
  want for this problem, because some allocations *are* intentionally
  retained for the process lifetime and should not be flagged.
- **We already have a valgrind suppressions file.** See
  [`valgrind.supp`](valgrind.supp).

The tradeoff: valgrind is slow and doesn't love Rust's allocator. On CI
this check takes a few seconds to run, which is fine.

## Three non-obvious tricks in the harness

The C harness is ~50 lines, but three of them are load-bearing for getting
a clean report. Miss any of these and valgrind will scream about hundreds
of kilobytes of "leaks" that aren't actually leaks.

### 1. `sqlite3_shutdown()` after `sqlite3_close()`

Without this, valgrind reports ~62 KB of "definitely lost" blocks whose
stack traces go entirely through `libsqlite3.so`. These are SQLite's own
page cache, mutex subsystem, and pcache1 allocator pools, which the
library retains on exit as a documented performance optimization. They
are not released until you explicitly call `sqlite3_shutdown()`.

From the SQLite docs:

> The sqlite3_shutdown() routine is not designed to be called by
> application code. If sqlite3_shutdown() is called while there are
> unused connections, or a threads that may still be in the SQLite
> library, the results are undefined.

We call it only *after* closing the single connection we opened, which
is safe.

### 2. `dlopen(ext_path, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE)` before
`sqlite3_load_extension`

This one is the reason an earlier version of this guide talked about
"???" frames in valgrind output. Here's what happens without it:

1. `sqlite3_load_extension` internally calls `dlopen(RTLD_NOW)` on our
   `.so`. The extension is loaded, `sqlite3_myext_init` runs, state gets
   allocated.
2. `sqlite3_close(db)` triggers SQLite's internal cleanup, which calls
   `dlclose` on every extension that was loaded with this connection.
3. Our `.so` is unmapped from memory. Its DWARF debug info, which
   valgrind needs to symbolize any stack frame that points into the
   extension's address range, is gone too.
4. At process exit, valgrind runs its final leak-check pass. It finds
   allocations whose "allocated from" pointers are in the address range
   where our `.so` used to live, but since the library is no longer
   mapped it cannot symbolize them. They show up as `???` in every stack
   trace frame that came from our code.

The fix is to pin the library in memory with `RTLD_NODELETE` *before*
handing it to SQLite. `RTLD_NODELETE` tells the dynamic linker "never
actually unload this library even if dlclose is called on it". SQLite's
later `dlclose` becomes a no-op — the reference count drops but the
memory mapping stays. When valgrind does its final pass, our DWARF info
is still loaded and every frame symbolizes cleanly.

Side effect: see the "still reachable" category below.

### 3. Taking the extension path as `argv[1]`

The harness used to hardcode `./libintegration_ext.so`. The Makefile
doesn't `cd` to any particular directory before running valgrind, so
the relative path resolved against whatever `cwd` happened to be —
which worked occasionally by accident when stale artifacts were sitting
around, and silently failed the rest of the time. Taking the path as an
argument and having the Makefile pass `$(BIN_DIR)/libmyext.so`
explicitly fixes this class of "works on my machine" bug.

## Interpreting each category of the leak summary

### `definitely lost: 0 bytes in 0 blocks`

This is the only category that matters for the `--errors-for-leak-kinds=definite`
flag that the Makefile target uses. Zero bytes = the leak check passes.
"Definitely lost" means valgrind can't find *any* live pointer pointing
at the allocation, i.e. the allocation has been truly orphaned. Any
non-zero value here is a real leak in our code and must be fixed.

### `indirectly lost: 0 bytes in 0 blocks`

Allocations that are only reachable through a `definitely lost` parent.
If "definitely lost" is zero, "indirectly lost" is necessarily zero too.

### `possibly lost: 116 bytes in 1 blocks`

This is the `HashMap<Arc<str>, Weak<InternalEntry<SharedState>>>` bucket
table inside the `REGISTRY` static. Valgrind's exact stack trace:

```
hashbrown::raw::RawTable::reserve_rehash
→ HashMap::rustc_entry
→ DbRegistry::init (src/registry.rs)
→ sqlite3_myext_init (tests/integration/rust_extension/src/lib.rs)
→ sqlite3_load_extension (libsqlite3.so)
→ main (leak_check)
```

When `REGISTRY.init(None, db, || SharedState { ... })` inserts its first
entry, hashbrown allocates a 116-byte bucket table to back the HashMap.
When the connection closes and `InternalEntry::drop` removes that entry,
the HashMap becomes empty — but `HashMap` does **not** shrink on remove,
so the bucket allocation persists. And because `REGISTRY` is a
`LazyLock<DbRegistry<SharedState>>` static, it is never dropped at
process exit (Rust statics don't run destructors), so the bucket lives
until the process dies.

This is **by design**. The registry is explicitly a process-wide static
that outlives individual connections. The bucket remaining allocated is
a feature, not a bug: the next time the extension is loaded for the same
database, the insert path doesn't have to re-allocate.

Valgrind classifies this as "possibly lost" rather than "still reachable"
because its pointer-chasing heuristic doesn't follow the full chain
`LazyLock → Once state → Arc → Mutex → HashMap → RawTable`. That's a
valgrind classification quirk, not a correctness issue.

The Makefile target uses `--errors-for-leak-kinds=definite` specifically
so that "possibly lost" allocations do not fail the check. If you want
to be pedantic, you can add a suppression to [`valgrind.supp`](valgrind.supp)
that matches the hashbrown stack, but it isn't necessary.

### `still reachable: 3,939 bytes in 10 blocks`

These are all allocations inside glibc's dynamic linker (`_dl_new_object`,
`_dl_map_object_from_fd`, `_dl_map_new_object`, `dl_open_worker`, …)
that get created when we `dlopen` the extension with `RTLD_NODELETE`.

This is the unavoidable cost of pinning a shared library. `RTLD_NODELETE`
tells ld.so "keep this library mapped for the rest of the process's
life", which means all of ld.so's bookkeeping state for that library —
the DT_NEEDED chain, the TLS template, the symbol hash tables — also
has to stay alive. Valgrind correctly classifies these as "still
reachable" because ld.so's global linked list still points at them.

There is nothing you can do about these and nothing you should try to
do. They are the price we pay for getting symbolized stack traces on
any allocation that comes from our `.so`.

### `suppressed: 104 bytes in 1 blocks`

Matched by a suppression rule in [`valgrind.supp`](valgrind.supp). The
suppressions file currently contains rules for `std::thread::Thread::new`
and `std::sync::mpmc::context::Context::new` — these are allocated lazily
by Rust's runtime in some code paths and are not freed because they live
inside thread-local storage. The suppressions are inherited from the
earlier `cargo valgrind test` integration and are not specific to this
leak check.

## What to do if the check starts failing

If `definitely lost` ever becomes non-zero, walk the stack traces and
identify where the leak originates. The common failure modes, roughly in
order of likelihood:

1. **Forgot `destructor_bridge::<T>` on `sqlite3_create_function_v2`.**
   Without it, SQLite never drops the `Arc` refcount that was leaked into
   `pApp` by `State::into_raw`, and the `InternalEntry<T>` lives forever.
   Fix: always pair `State::into_raw()` as `pApp` with
   `Some(destructor_bridge::<T>)` as `xDestroy`.
2. **Called `State::into_raw()` more times than you called
   `destructor_bridge`.** Each `into_raw` leaks one refcount; each
   `destructor_bridge` drops one refcount. They must balance. Use
   `clone_from_raw` inside scalar functions, not `into_raw`.
3. **A race in `InternalEntry::drop`.** The identity check against
   `self as *const _` is what makes map cleanup safe when a new
   connection opens the same database between the last `State` drop and
   the destructor firing. If you change that logic, re-read the comment
   in [`src/registry.rs`](src/registry.rs) carefully.
4. **An allocation that happens outside the connection lifecycle.** Any
   state you stash in a `static` via `LazyLock` / `OnceLock` will show up
   as "possibly lost" or "still reachable", not "definitely lost", so it
   won't break the check. But if you start heap-allocating from inside
   `sqlite3_extension_init2` itself without hooking it into RAII, you
   can produce real "definitely lost" leaks.

If `definitely lost` is zero but the harness fails for a different
reason (e.g. `Extension load failed`), check:

- The path passed as `argv[1]` actually points at a built `.so`. The
  Makefile sets it to `$(BIN_DIR)/libmyext.so` but that file has to
  exist — make sure `make build` or the cargo step in the target ran
  successfully.
- The `.so` has debug info (`file bin/libmyext.so` should say "with
  debug_info, not stripped"). If not, check the `[profile.release]
  debug = true` line in
  [`tests/integration/rust_extension/Cargo.toml`](tests/integration/rust_extension/Cargo.toml).
- `libdl` is linked into the C harness. The Makefile gcc line includes
  `-ldl`; if you changed it, put that back.

## Related targets

- **`make test`** — Rust unit tests. 16 tests exercise the registry,
  the FFI glue, and `destructor_bridge` directly. No valgrind.
- **`make test-integration`** — Go concurrency + lazy-load stress
  tests. Exercises the full extension under multi-threaded load but
  doesn't run under valgrind (valgrind + CGo + go-sqlite3 is a rabbit
  hole).
- **`make leak-check-valgrind`** — runs `cargo valgrind test` over the
  Rust unit tests using the suppressions file. Slower, exercises
  different code paths, complementary to `leak-check-integration`.
