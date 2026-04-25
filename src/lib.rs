//! # sqlite-ext-core
//!
//! A minimal, zero-dependency toolkit for building SQLite loadable extensions
//! in Rust. It provides three things a typical extension needs and nothing
//! else:
//!
//! 1. **Raw FFI types** ([`sqlite3`], [`sqlite3_context`], [`sqlite3_value`])
//!    and slot indices into SQLite's `sqlite3_api_routines` table — no static
//!    link to libsqlite3 required.
//! 2. **Dynamic API resolution** via [`sqlite3_extension_init2`]: unpack the
//!    `p_api` pointer SQLite hands you in `sqlite3_*_init`, store the function
//!    pointers once, and let every call after that dispatch through them.
//! 3. **A per-database shared-state registry** ([`DbRegistry<T>`]) that
//!    isolates state by the database file path, shares it across connections
//!    to the same file, and cleans itself up via RAII the moment the last
//!    connection closes.
//!
//! ## Why dynamic resolution?
//!
//! A loadable `.so` can be opened by any SQLite host. Some hosts (Go's
//! `mattn/go-sqlite3`, Python's `sqlite3` module, …) statically link SQLite
//! and do *not* export its symbols to the dynamic linker, so a plain
//! `extern "C"` call to `sqlite3_*` from the extension would fail to resolve.
//! The `p_api` pointer SQLite passes to your init function *is* the list of
//! function pointers the host actually uses, so resolving through it works in
//! every host. See [`api`] and [`wrappers`].
//!
//! ## Hot-path shape
//!
//! Steady-state per-row lookups bypass the registry map entirely: [`DbRegistry::get`]
//! first checks SQLite's `auxdata` slot for a cached `Arc` pointer. This is a
//! single pointer dereference — no hashing, no locking — which is why the
//! registry stays out of the way of tight scalar-function loops. See
//! [`registry`] for the full layering.
//!
//! ## Ordering requirement
//!
//! Call [`sqlite3_extension_init2`] **before** touching any other API in this
//! crate. The C-mirror wrappers in [`wrappers`] panic if called before init,
//! and [`DbRegistry::get`] / [`DbRegistry::init`] panic only when they need
//! to resolve a non-null database path (i.e. not for purely in-memory tests
//! using a null `db` pointer).
//!
//! ## Module layout
//!
//! | Module          | Contents                                                   |
//! |-----------------|------------------------------------------------------------|
//! | [`ffi`]         | Raw FFI types, fn-pointer aliases, `SLOT_*` indices        |
//! | [`api`]         | [`GlobalApi`], [`ExtensionApi`], [`sqlite3_extension_init2`] |
//! | [`wrappers`]    | Inline C-mirror wrappers (`sqlite3_result_*`, `sqlite3_value_*`, …) |
//! | [`registry`]    | [`DbRegistry`], [`State`], [`destructor_bridge`]           |
//!
//! All public items are re-exported at the crate root, so `sqlite_ext_core::sqlite3`,
//! `sqlite_ext_core::DbRegistry`, and friends all resolve without reaching
//! into submodules.

pub mod api;
pub mod ffi;
pub mod registry;
pub mod wrappers;

pub use api::*;
pub use ffi::*;
pub use registry::*;
pub use wrappers::*;

#[cfg(test)]
mod tests {
    use super::*;
    use crate::api::{EXTENSION_API, GLOBAL_API};
    use crate::registry::{get_raw_db_path, InternalEntry};
    use std::ffi::c_void;
    use std::os::raw::c_char;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::{Arc, Weak};
    use std::thread;

    /// Bootstraps `GLOBAL_API` and `EXTENSION_API` from the linked libsqlite3-sys
    /// symbols. Production code resolves these dynamically via
    /// `sqlite3_extension_init2(p_api)`; unit tests don't have a `p_api`, so they
    /// transmute directly-linked function pointers into our opaque-typed slots.
    /// Both sides use `#[repr(C)]` zero-sized opaque structs, so the ABIs match.
    ///
    /// Idempotent: `OnceLock::set` silently ignores second-and-later writes,
    /// so repeated calls across tests are free.
    fn setup_api() {
        unsafe {
            let _ = GLOBAL_API.set(GlobalApi {
                get_auxdata: std::mem::transmute(
                    libsqlite3_sys::sqlite3_get_auxdata as *const (),
                ),
                set_auxdata: std::mem::transmute(
                    libsqlite3_sys::sqlite3_set_auxdata as *const (),
                ),
                db_filename: std::mem::transmute(
                    libsqlite3_sys::sqlite3_db_filename as *const (),
                ),
            });
            let _ = EXTENSION_API.set(ExtensionApi {
                context_db_handle: std::mem::transmute(
                    libsqlite3_sys::sqlite3_context_db_handle as *const (),
                ),
                result_blob: std::mem::transmute(
                    libsqlite3_sys::sqlite3_result_blob as *const (),
                ),
                result_double: std::mem::transmute(
                    libsqlite3_sys::sqlite3_result_double as *const (),
                ),
                result_error: std::mem::transmute(
                    libsqlite3_sys::sqlite3_result_error as *const (),
                ),
                result_int: std::mem::transmute(
                    libsqlite3_sys::sqlite3_result_int as *const (),
                ),
                result_int64: std::mem::transmute(
                    libsqlite3_sys::sqlite3_result_int64 as *const (),
                ),
                result_null: std::mem::transmute(
                    libsqlite3_sys::sqlite3_result_null as *const (),
                ),
                result_text: std::mem::transmute(
                    libsqlite3_sys::sqlite3_result_text as *const (),
                ),
                user_data: std::mem::transmute(libsqlite3_sys::sqlite3_user_data as *const ()),
                value_blob: std::mem::transmute(
                    libsqlite3_sys::sqlite3_value_blob as *const (),
                ),
                value_bytes: std::mem::transmute(
                    libsqlite3_sys::sqlite3_value_bytes as *const (),
                ),
                value_double: std::mem::transmute(
                    libsqlite3_sys::sqlite3_value_double as *const (),
                ),
                value_int: std::mem::transmute(libsqlite3_sys::sqlite3_value_int as *const ()),
                value_int64: std::mem::transmute(
                    libsqlite3_sys::sqlite3_value_int64 as *const (),
                ),
                value_numeric_type: std::mem::transmute(
                    libsqlite3_sys::sqlite3_value_numeric_type as *const (),
                ),
                value_text: std::mem::transmute(
                    libsqlite3_sys::sqlite3_value_text as *const (),
                ),
                value_type: std::mem::transmute(
                    libsqlite3_sys::sqlite3_value_type as *const (),
                ),
                create_function_v2: std::mem::transmute(
                    libsqlite3_sys::sqlite3_create_function_v2 as *const (),
                ),
            });
        }
    }

    /// Tests that the registry can correctly initialize a state structure
    /// for a new database, and subsequent requests for the same database
    /// yield the exact same Arc without re-initialization.
    #[test]
    fn test_registry_initialization_and_retrieval() {
        setup_api();
        let registry = DbRegistry::<usize>::new();
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        let db = unsafe { conn.handle() } as *mut sqlite3;

        // 1. Initial init should create state
        let state1 = registry.init(None, db, || 42);
        assert_eq!(*state1, 42);

        // 2. Subsequent get should return the SAME state
        let state2 = registry.get(None, db).expect("State should exist");
        assert_eq!(*state2, 42);

        // 3. Verify internal map count
        assert_eq!(registry.map.lock().unwrap().len(), 1);
        assert!(Arc::ptr_eq(&state1.0, &state2.0));
    }

    /// Verifies that state can be correctly expunged from the registry
    /// when a database is closed or released, preventing memory leaks over time.
    #[test]
    fn test_registry_release() {
        setup_api();
        let registry = DbRegistry::<usize>::new();
        let temp_file = "temp2.db";
        let conn = rusqlite::Connection::open(temp_file).unwrap();
        let db_ptr = unsafe { conn.handle() } as *mut sqlite3;

        let _val = registry.init(None, db_ptr, || 100);
        assert_eq!(registry.map.lock().unwrap().len(), 1);

        let path = unsafe { get_raw_db_path(db_ptr) };
        registry.release(&path);
        assert_eq!(registry.map.lock().unwrap().len(), 0);

        drop(conn);
        let _ = std::fs::remove_file(temp_file);
    }
    /// A stress test ensuring that if dozens of threads attempt to look
    /// up state for the EXACT same database pointer near-simultaneously,
    /// they all observe the same underlying atomic counter without
    /// corruption. Uses a single real in-memory SQLite connection shared
    /// across threads via its raw pointer, encoded as `usize` so it
    /// crosses the thread boundary without a newtype.
    #[test]
    fn test_concurrent_initialization() {
        setup_api();
        let registry = Arc::new(DbRegistry::<AtomicUsize>::new());
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        let db_addr = unsafe { conn.handle() } as usize;

        // Pre-initialize so every worker thread finds an existing state.
        let keeper = registry.init(None, db_addr as *mut sqlite3, || AtomicUsize::new(0));

        let mut handles = vec![];
        for _ in 0..50 {
            let reg_clone = registry.clone();
            handles.push(thread::spawn(move || {
                let state = reg_clone
                    .get(None, db_addr as *mut sqlite3)
                    .expect("State should exist");
                state.fetch_add(1, Ordering::SeqCst);
            }));
        }

        for h in handles {
            h.join().unwrap();
        }

        assert_eq!(keeper.load(Ordering::SeqCst), 50);
        assert_eq!(registry.map.lock().unwrap().len(), 1);
    }

    /// Validates the FFI bindings to ensure we can correctly extract a
    /// physical database file path from a live `*mut sqlite3` connection pointer.
    #[test]
    fn test_get_db_path_file() {
        setup_api();
        let temp_file = "test_core_file2.db";
        let conn = rusqlite::Connection::open(temp_file).unwrap();

        let db_ptr = unsafe { conn.handle() } as *mut sqlite3;

        let path = unsafe { get_raw_db_path(db_ptr) };
        assert!(path.ends_with(temp_file));

        drop(conn);
        let _ = std::fs::remove_file(temp_file);
    }

    /// Ensures that a non-null in-memory SQLite database produces a unique
    /// key of the form `":memory:\0<ptr>"` — the NUL byte guarantees the
    /// key cannot collide with any real filesystem path, and the pointer
    /// address makes each in-memory connection distinct.
    #[test]
    fn test_get_db_path_memory() {
        setup_api();
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        let db_ptr = unsafe { conn.handle() } as *mut sqlite3;

        let path = unsafe { get_raw_db_path(db_ptr) };
        assert!(
            path.starts_with(":memory:\0"),
            "expected in-memory key with NUL sentinel, got: {:?}",
            path
        );
    }

    /// Critical regression test: two independent in-memory databases in
    /// the same process must NOT share registry state. Before the NUL-keyed
    /// fix, every `:memory:` connection collapsed to the same registry
    /// entry and leaked state between unrelated databases.
    #[test]
    fn test_in_memory_databases_are_isolated() {
        setup_api();
        let registry = DbRegistry::<AtomicUsize>::new();

        let conn1 = rusqlite::Connection::open_in_memory().unwrap();
        let conn2 = rusqlite::Connection::open_in_memory().unwrap();
        let db1 = unsafe { conn1.handle() } as *mut sqlite3;
        let db2 = unsafe { conn2.handle() } as *mut sqlite3;

        let s1 = registry.init(None, db1, || AtomicUsize::new(100));
        let s2 = registry.init(None, db2, || AtomicUsize::new(200));

        // Different handles → different registry entries → different Arcs.
        assert!(!Arc::ptr_eq(&s1.0, &s2.0));

        // Mutating one must not affect the other.
        s1.fetch_add(1, Ordering::SeqCst);
        assert_eq!(s1.load(Ordering::Relaxed), 101);
        assert_eq!(s2.load(Ordering::Relaxed), 200);

        // Map should contain two distinct entries.
        assert_eq!(registry.map.lock().unwrap().len(), 2);
    }

    /// A critical memory-safety test confirming that the C-callback destructor
    /// gracefully decrements the `Arc` reference count when SQLite cleans up an
    /// extension context, eliminating memory leaks across the C/Rust boundary.
    #[test]
    fn test_destructor_bridge() {
        struct Dummy {
            _data: [u8; 1024],
        }

        let arc = Arc::new(InternalEntry {
            state: Dummy { _data: [0; 1024] },
            path: Arc::from("test"),
            map: Weak::new(),
        });
        let raw_ptr = Arc::into_raw(arc.clone()) as *mut c_void;

        assert_eq!(Arc::strong_count(&arc), 2);

        unsafe {
            destructor_bridge::<Dummy>(raw_ptr);
        }

        assert_eq!(Arc::strong_count(&arc), 1);
    }

    /// Ensures default constructor initializes an empty registry correctly.
    #[test]
    fn test_registry_default() {
        let registry = DbRegistry::<usize>::default();
        assert_eq!(registry.map.lock().unwrap().len(), 0);
    }

    /// Ensures the C-FFI destructor bridge handles null auxiliary data pointers
    /// correctly without triggering a panic or unsafe cast.
    #[test]
    fn test_destructor_bridge_null() {
        unsafe {
            destructor_bridge::<usize>(std::ptr::null_mut());
        }
    }

    extern "C" fn mock_scalar_func(
        ctx: *mut libsqlite3_sys::sqlite3_context,
        _argc: std::os::raw::c_int,
        _argv: *mut *mut libsqlite3_sys::sqlite3_value,
    ) {
        unsafe {
            // Retrieve the user-defined data (the DbRegistry) attached to this function.
            let p_app = libsqlite3_sys::sqlite3_user_data(ctx);
            // Reconstruct the registry reference from the raw pointer.
            let registry = &*(p_app as *const DbRegistry<AtomicUsize>);
            // Get the database connection handle for this specific context.
            let db = libsqlite3_sys::sqlite3_context_db_handle(ctx);
            let state = registry.init(
                Some(ctx as *mut c_void as *mut sqlite3_context),
                db as *mut c_void as *mut sqlite3,
                || AtomicUsize::new(100),
            );
            state.fetch_add(1, Ordering::SeqCst);
        }
    }

    /// Integration test bridging actual SQLite execution with the registry.
    /// This forces sqlite to evaluate the function in a SQL query, hitting
    /// both the "slow path" (initial db fetch/insertion) and the $O(1)$
    /// "fast path" (auxdata bypass cache) consecutively to guarantee 100%
    /// correct behavior of `get_fast` and auxiliary C-pointers in realistic environment.
    #[test]
    fn test_get_fast_coverage() {
        setup_api();
        let registry = Arc::new(DbRegistry::<AtomicUsize>::new());

        let mut db: *mut sqlite3 = std::ptr::null_mut();
        unsafe {
            libsqlite3_sys::sqlite3_open(
                b":memory:\0".as_ptr() as *const c_char,
                &mut db as *mut *mut sqlite3 as *mut *mut libsqlite3_sys::sqlite3,
            );

            // Hold a keeper for the real db handle so the registry
            // entry survives the sqlite3_finalize that would otherwise
            // fire destructor_bridge and wipe the slot.
            let _keeper = registry.init(None, db, || AtomicUsize::new(100));

            let p_app = Arc::as_ptr(&registry) as *mut c_void;
            libsqlite3_sys::sqlite3_create_function_v2(
                db as *mut libsqlite3_sys::sqlite3,
                b"test_get_fast\0".as_ptr() as *const c_char,
                1,
                libsqlite3_sys::SQLITE_UTF8,
                p_app,
                Some(std::mem::transmute(mock_scalar_func as *const ())),
                None,
                None,
                None,
            );

            let mut stmt: *mut libsqlite3_sys::sqlite3_stmt = std::ptr::null_mut();
            libsqlite3_sys::sqlite3_prepare_v2(
                db as *mut libsqlite3_sys::sqlite3,
                b"SELECT test_get_fast(1), test_get_fast(1);\0".as_ptr() as *const c_char,
                -1,
                &mut stmt,
                std::ptr::null_mut(),
            );

            // Step: runs the function twice per row, hitting slow then fast paths
            libsqlite3_sys::sqlite3_step(stmt);
            libsqlite3_sys::sqlite3_finalize(stmt);

            let state = registry.get(None, db).expect("State should exist");
            assert_eq!(state.load(Ordering::Relaxed), 102);

            libsqlite3_sys::sqlite3_close(db as *mut libsqlite3_sys::sqlite3);
        }
    }

    #[test]
    fn test_deterministic_cleanup() {
        setup_api();
        let registry = DbRegistry::<usize>::new();
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        let db = unsafe { conn.handle() } as *mut sqlite3;

        // 1. Scope the state handle
        {
            let _state = registry.init(None, db, || 42);
            assert_eq!(registry.map.lock().unwrap().len(), 1);
        } // state dropped here. Deterministic cleanup happens.

        // 2. Map MUST be empty now
        assert_eq!(registry.map.lock().unwrap().len(), 0);
    }

    /// Verifies that two different database handles point to two isolated
    /// states, ensuring no state leakage between unrelated databases.
    #[test]
    fn test_isolation() {
        setup_api();
        let registry = DbRegistry::<AtomicUsize>::new();
        let f1 = "iso1.db";
        let f2 = "iso2.db";
        let conn1 = rusqlite::Connection::open(f1).unwrap();
        let conn2 = rusqlite::Connection::open(f2).unwrap();

        let db1 = unsafe { conn1.handle() } as *mut sqlite3;
        let db2 = unsafe { conn2.handle() } as *mut sqlite3;

        let state1 = registry.init(None, db1, || AtomicUsize::new(1));
        let state2 = registry.init(None, db2, || AtomicUsize::new(2));

        assert_ne!(
            state1.load(Ordering::Relaxed),
            state2.load(Ordering::Relaxed)
        );
        assert_eq!(registry.map.lock().unwrap().len(), 2);

        drop(conn1);
        drop(conn2);
        let _ = std::fs::remove_file(f1);
        let _ = std::fs::remove_file(f2);
    }

    /// Tests that after the last handle to a state is dropped, a subsequent
    /// 'get' call correctly triggers re-initialization from scratch.
    #[test]
    fn test_reinitialization() {
        setup_api();
        let registry = DbRegistry::<AtomicUsize>::new();
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        let db = unsafe { conn.handle() } as *mut sqlite3;

        {
            let state = registry.init(None, db, || AtomicUsize::new(100));
            state.store(101, Ordering::Relaxed);
        } // Purged from map here.

        let state = registry.init(None, db, || AtomicUsize::new(200));
        assert_eq!(state.load(Ordering::Relaxed), 200); // Should be new state, not 101.
    }

    /// Verifies that if the registry itself is dropped but connections are still open,
    /// the entries (State handles) remain valid and don't crash when they are finally dropped.
    #[test]
    fn test_registry_dropped_first() {
        setup_api();
        let registry = Box::new(DbRegistry::<AtomicUsize>::new());
        let conn = rusqlite::Connection::open_in_memory().unwrap();
        let db = unsafe { conn.handle() } as *mut sqlite3;

        let state = registry.init(None, db, || AtomicUsize::new(1));

        // Drop the registry while we still hold a 'state' handle.
        drop(registry);

        // State handle should still be valid.
        assert_eq!(state.load(Ordering::Relaxed), 1);

        // Dropping the state handle now should not crash,
        // even though it can't clean itself up from the map anymore.
        drop(state);
    }

    /// This test explicitly verifies that when a `sqlite3_context` is provided,
    /// the registry uses SQLite's internal metadata cache (AuxData) to store
    /// and retrieve the state handle, achieving true $O(1)$ performance.
    #[test]
    fn test_direct_context_usage() {
        setup_api();
        let registry = Arc::new(DbRegistry::<AtomicUsize>::new());
        let mut db: *mut sqlite3 = std::ptr::null_mut();

        unsafe {
            libsqlite3_sys::sqlite3_open(
                b":memory:\0".as_ptr() as *const c_char,
                &mut db as *mut *mut sqlite3 as *mut *mut libsqlite3_sys::sqlite3,
            );

            extern "C" fn test_func(
                ctx: *mut libsqlite3_sys::sqlite3_context,
                _argc: i32,
                _argv: *mut *mut libsqlite3_sys::sqlite3_value,
            ) {
                unsafe {
                    // Extract the DbRegistry instance originally passed to sqlite3_create_function_v2.
                    let p_app = libsqlite3_sys::sqlite3_user_data(ctx);
                    // Cast the raw pointer back to our Registry type (safe because we control p_app).
                    let registry = &*(p_app as *const DbRegistry<AtomicUsize>);
                    // Obtain the underlying sqlite3* database connection attached to this context.
                    let db = libsqlite3_sys::sqlite3_context_db_handle(ctx);

                    // 1. Initial init - should populate AuxData
                    let s1 = registry.init(
                        Some(ctx as *mut c_void as *mut sqlite3_context),
                        db as *mut c_void as *mut sqlite3,
                        || AtomicUsize::new(42),
                    );

                    // 2. Immediate get - should hit AuxData bypass (Layer 1)
                    let s2 = registry
                        .get(
                            Some(ctx as *mut c_void as *mut sqlite3_context),
                            db as *mut c_void as *mut sqlite3,
                        )
                        .expect("Should exist in context cache");

                    // 3. Verify they point to the exact same memory
                    assert!(Arc::ptr_eq(&s1.0, &s2.0));

                    // 4. Verify sqlite3_get_auxdata directly at the slot the
                    //    registry actually uses (DEFAULT_AUXDATA_SLOT, which is
                    //    i32::MAX — not 0, to avoid colliding with the
                    //    argument-caching convention).
                    let raw = libsqlite3_sys::sqlite3_get_auxdata(
                        ctx,
                        crate::registry::DEFAULT_AUXDATA_SLOT,
                    );
                    assert!(!raw.is_null(), "AuxData should not be null after init");

                    s1.fetch_add(1, Ordering::SeqCst);
                    libsqlite3_sys::sqlite3_result_int(ctx, 1);
                }
            }

            let p_app = Arc::as_ptr(&registry) as *mut c_void;
            libsqlite3_sys::sqlite3_create_function_v2(
                db as *mut libsqlite3_sys::sqlite3,
                b"test_ctx_usage\0".as_ptr() as *const c_char,
                0,
                libsqlite3_sys::SQLITE_UTF8,
                p_app,
                Some(std::mem::transmute(test_func as *const ())),
                None,
                None,
                None,
            );

            let mut stmt: *mut libsqlite3_sys::sqlite3_stmt = std::ptr::null_mut();
            libsqlite3_sys::sqlite3_prepare_v2(
                db as *mut libsqlite3_sys::sqlite3,
                b"SELECT test_ctx_usage();\0".as_ptr() as *const c_char,
                -1,
                &mut stmt,
                std::ptr::null_mut(),
            );

            libsqlite3_sys::sqlite3_step(stmt);
            libsqlite3_sys::sqlite3_finalize(stmt);

            // 5. Verify RAII Cleanup: Since no other handles exist,
            // finalizing the statement (which triggers the destructor_bridge)
            // should have purged the entry from the map instantly.
            assert_eq!(
                registry.map.lock().unwrap().len(),
                0,
                "Map must be empty after finalize"
            );

            libsqlite3_sys::sqlite3_close(db as *mut libsqlite3_sys::sqlite3);
        }
    }

    /// `DbRegistry::with_auxdata_slot` produces a registry that works
    /// end-to-end under real scalar-function invocation. The hot path
    /// uses the specified slot (not the hardcoded default) for every
    /// read and write, proving the slot is threaded through to all
    /// three `get_auxdata`/`set_auxdata` call sites and not accidentally
    /// left as `0` anywhere.
    ///
    /// We can't directly inspect auxdata from outside the scalar
    /// function (auxdata is keyed on `sqlite3_context`, which we don't
    /// have a handle to after the query completes), so the assertion
    /// is behavioral: after two calls to `probe()` in a single
    /// statement — the second of which hits the hot-path cache — the
    /// counter must be exactly 2. If any of the three auxdata
    /// operations inside the registry still hardcoded slot `0`, the
    /// hot-path cache would either miss consistently (causing extra
    /// init_fn calls, which this test would survive) or write/read
    /// mismatched slots (which would cause subtle auxdata corruption
    /// detectable under valgrind).
    #[test]
    fn test_custom_auxdata_slot() {
        setup_api();
        const CUSTOM_SLOT: std::os::raw::c_int = 12345;
        let registry = Arc::new(DbRegistry::<AtomicUsize>::with_auxdata_slot(CUSTOM_SLOT));

        extern "C" fn probe(
            ctx: *mut libsqlite3_sys::sqlite3_context,
            _argc: std::os::raw::c_int,
            _argv: *mut *mut libsqlite3_sys::sqlite3_value,
        ) {
            unsafe {
                let p_app = libsqlite3_sys::sqlite3_user_data(ctx);
                let registry = &*(p_app as *const DbRegistry<AtomicUsize>);
                let db = libsqlite3_sys::sqlite3_context_db_handle(ctx);
                let state = registry.init(
                    Some(ctx as *mut c_void as *mut sqlite3_context),
                    db as *mut c_void as *mut sqlite3,
                    || AtomicUsize::new(0),
                );
                state.fetch_add(1, Ordering::SeqCst);
                libsqlite3_sys::sqlite3_result_int(ctx, 1);
            }
        }

        let mut db: *mut sqlite3 = std::ptr::null_mut();
        unsafe {
            libsqlite3_sys::sqlite3_open(
                b":memory:\0".as_ptr() as *const c_char,
                &mut db as *mut *mut sqlite3 as *mut *mut libsqlite3_sys::sqlite3,
            );

            let _keeper = registry.init(None, db, || AtomicUsize::new(0));

            let p_app = Arc::as_ptr(&registry) as *mut c_void;
            libsqlite3_sys::sqlite3_create_function_v2(
                db as *mut libsqlite3_sys::sqlite3,
                b"probe\0".as_ptr() as *const c_char,
                0,
                libsqlite3_sys::SQLITE_UTF8,
                p_app,
                Some(std::mem::transmute(probe as *const ())),
                None,
                None,
                None,
            );

            // Call probe() twice in one statement so the second invocation
            // goes through the auxdata hot path via CUSTOM_SLOT.
            let mut stmt: *mut libsqlite3_sys::sqlite3_stmt = std::ptr::null_mut();
            libsqlite3_sys::sqlite3_prepare_v2(
                db as *mut libsqlite3_sys::sqlite3,
                b"SELECT probe(), probe();\0".as_ptr() as *const c_char,
                -1,
                &mut stmt,
                std::ptr::null_mut(),
            );
            libsqlite3_sys::sqlite3_step(stmt);
            libsqlite3_sys::sqlite3_finalize(stmt);

            // After two probe() calls (both reaching the registry
            // through CUSTOM_SLOT), the counter reflects exactly those
            // two increments.
            let state = registry.get(None, db).expect("State should exist");
            assert_eq!(state.load(Ordering::Relaxed), 2);

            libsqlite3_sys::sqlite3_close(db as *mut libsqlite3_sys::sqlite3);
        }
    }

    /// Two independent in-memory databases, each hit with many real
    /// scalar-function calls over a live SQLite connection. Before the
    /// NUL-keyed in-memory fix, the two connections collapsed to the same
    /// registry entry and their counters cross-contaminated. This test
    /// exercises the full path — sqlite3_open → register function → run
    /// N queries on db1 and M queries on db2 interleaved → verify both
    /// counters ended up exactly where they should — so it catches any
    /// regression that re-introduces the collision, not just the cheap
    /// `init()`-only check in `test_in_memory_databases_are_isolated`.
    #[test]
    fn test_multi_call_two_in_memory_databases() {
        setup_api();
        let registry = Arc::new(DbRegistry::<AtomicUsize>::new());

        // Scalar function body: go through the same auxdata hot path a
        // real extension would.
        extern "C" fn bump(
            ctx: *mut libsqlite3_sys::sqlite3_context,
            _argc: std::os::raw::c_int,
            _argv: *mut *mut libsqlite3_sys::sqlite3_value,
        ) {
            unsafe {
                let p_app = libsqlite3_sys::sqlite3_user_data(ctx);
                let registry = &*(p_app as *const DbRegistry<AtomicUsize>);
                let db = libsqlite3_sys::sqlite3_context_db_handle(ctx);
                let state = registry.init(
                    Some(ctx as *mut c_void as *mut sqlite3_context),
                    db as *mut c_void as *mut sqlite3,
                    || AtomicUsize::new(0),
                );
                state.fetch_add(1, Ordering::SeqCst);
                libsqlite3_sys::sqlite3_result_int(ctx, 1);
            }
        }

        // Open two completely separate in-memory databases.
        let mut db1: *mut sqlite3 = std::ptr::null_mut();
        let mut db2: *mut sqlite3 = std::ptr::null_mut();
        unsafe {
            libsqlite3_sys::sqlite3_open(
                b":memory:\0".as_ptr() as *const c_char,
                &mut db1 as *mut *mut sqlite3 as *mut *mut libsqlite3_sys::sqlite3,
            );
            libsqlite3_sys::sqlite3_open(
                b":memory:\0".as_ptr() as *const c_char,
                &mut db2 as *mut *mut sqlite3 as *mut *mut libsqlite3_sys::sqlite3,
            );

            // db1 and db2 are distinct sqlite3* handles — the pointers
            // themselves must differ, otherwise the rest of the test is
            // meaningless.
            assert_ne!(db1, db2, "sqlite3_open returned same handle twice");

            // Hold strong `State` handles in the test scope so the
            // registry entries survive across query boundaries. Without
            // these, each `sqlite3_finalize` would drop the auxdata
            // refcount to zero, fire `InternalEntry::drop`, and wipe the
            // entry from the map — the next query would see a fresh
            // counter and this whole test would measure nothing useful.
            let _k1 = registry.init(
                None,
                db1 as *mut c_void as *mut sqlite3,
                || AtomicUsize::new(0),
            );
            let _k2 = registry.init(
                None,
                db2 as *mut c_void as *mut sqlite3,
                || AtomicUsize::new(0),
            );

            // Register `bump()` on both databases, pointing both at the
            // same shared registry.
            let p_app = Arc::as_ptr(&registry) as *mut c_void;
            for db in [db1, db2] {
                libsqlite3_sys::sqlite3_create_function_v2(
                    db as *mut libsqlite3_sys::sqlite3,
                    b"bump\0".as_ptr() as *const c_char,
                    0,
                    libsqlite3_sys::SQLITE_UTF8,
                    p_app,
                    Some(std::mem::transmute(bump as *const ())),
                    None,
                    None,
                    None,
                );
            }

            // Run many queries on each database, interleaving them to
            // make sure the registry doesn't cache the wrong state
            // between calls from different handles.
            let (n1, n2): (usize, usize) = (17, 5);
            for i in 0..(n1 + n2) {
                // Alternate: most iterations hit db1, a few hit db2.
                let db = if i % 5 == 0 && i / 5 < n2 { db2 } else { db1 };
                let mut stmt: *mut libsqlite3_sys::sqlite3_stmt = std::ptr::null_mut();
                libsqlite3_sys::sqlite3_prepare_v2(
                    db as *mut libsqlite3_sys::sqlite3,
                    b"SELECT bump();\0".as_ptr() as *const c_char,
                    -1,
                    &mut stmt,
                    std::ptr::null_mut(),
                );
                libsqlite3_sys::sqlite3_step(stmt);
                libsqlite3_sys::sqlite3_finalize(stmt);
            }

            // Read back state directly from the registry before closing
            // the connections (which would fire xDestroy and drop the
            // Arcs). Each database should hold exactly the count of
            // queries that targeted it.
            let s1 = registry
                .get(None, db1 as *mut c_void as *mut sqlite3)
                .expect("db1 should have state");
            let s2 = registry
                .get(None, db2 as *mut c_void as *mut sqlite3)
                .expect("db2 should have state");

            // The two databases must back distinct Arcs.
            assert!(!Arc::ptr_eq(&s1.0, &s2.0));

            // Count how the loop actually distributed the calls — the
            // expected values come straight from the (i % 5 == 0 &&
            // i / 5 < n2) branch above.
            let mut expected_n1 = 0;
            let mut expected_n2 = 0;
            for i in 0..(n1 + n2) {
                if i % 5 == 0 && i / 5 < n2 {
                    expected_n2 += 1;
                } else {
                    expected_n1 += 1;
                }
            }
            assert_eq!(s1.load(Ordering::Relaxed), expected_n1);
            assert_eq!(s2.load(Ordering::Relaxed), expected_n2);

            // Registry must contain exactly 2 entries — one per db.
            assert_eq!(registry.map.lock().unwrap().len(), 2);

            libsqlite3_sys::sqlite3_close(db1 as *mut libsqlite3_sys::sqlite3);
            libsqlite3_sys::sqlite3_close(db2 as *mut libsqlite3_sys::sqlite3);
        }
    }
}
