//! Per-database shared-state registry.
//!
//! [`DbRegistry<T>`] maps each SQLite database file path to a reference-counted
//! block of user state. Typical usage is a single `static` registry per
//! extension; the registry itself is cheap to clone (`Arc` internally) and
//! freely shareable across threads.
//!
//! ## Lookup layering
//!
//! [`DbRegistry::get`] and [`DbRegistry::init`] walk three layers in order:
//!
//! 1. **Hot path — SQLite auxdata cache.** If the caller passes a
//!    `sqlite3_context`, the registry checks slot 0 of its auxdata. A hit is
//!    a raw pointer dereference plus an `Arc` clone — nanoseconds. This is
//!    what makes the registry cheap enough to query from inside a tight
//!    scalar-function loop.
//! 2. **Warm path — hash map lookup.** On auxdata miss, lock the map and
//!    look up by database file path. Still fast (cache-friendly, no
//!    allocations for the key lookup thanks to `Arc<str>: Borrow<str>`), but
//!    takes a mutex.
//! 3. **Slow path — initialize.** Only [`DbRegistry::init`] hits this, and
//!    only on genuine first use for a database. Runs `init_fn()` under the
//!    map lock, inserts a `Weak` into the map, and caches the fresh `Arc`
//!    into auxdata so every subsequent call for the same statement hits
//!    layer 1.
//!
//! Warm/slow hits also write back to the auxdata cache so the next row
//! executes through the hot path.
//!
//! ## Ownership model
//!
//! - [`State<T>`] is the public handle. It derefs to `T`, is cheap to clone
//!    (`Arc` internally), and automatically triggers registry cleanup via
//!    `InternalEntry`'s `Drop` when the last strong reference goes away.
//! - [`State::into_raw`] / [`State::clone_from_raw`] / [`destructor_bridge`]
//!    form the C-interop trio: hand a `State` to SQLite as a `pApp` pointer,
//!    recover a temporary `State` from the pointer inside your scalar
//!    function without affecting the refcount, and drop the C-owned refcount
//!    via `destructor_bridge` when SQLite fires `xDestroy`.
//! - The registry map stores `Weak<InternalEntry<T>>`, not strong refs, so
//!    it never keeps state alive on its own. State lives exactly as long as
//!    some connection (or your code) holds a strong `State<T>` to it.
//!
//! ## Cleanup races
//!
//! Between the last `State` drop and a new connection opening for the same
//! file, there's a window where `InternalEntry::drop` might try to remove an
//! entry that has already been replaced by the new connection. The `Drop`
//! impl guards against this by comparing the live pointer in the map
//! against `self` before removing — see the comment in the impl below.

use std::collections::HashMap;
use std::ffi::{c_void, CStr};
use std::fmt::Debug;
use std::ops::Deref;
use std::os::raw::c_char;
use std::sync::{Arc, Mutex, Weak};

use crate::api::GLOBAL_API;
use crate::ffi::*;

/// Marker trait for types that can live inside a [`DbRegistry`].
///
/// A `DbRegistry<T>` is meant to be shared across every SQLite connection
/// in a process, so `T` has to satisfy the standard "shareable across
/// threads for the life of the program" bounds: `Send + Sync + 'static`.
/// This trait is a self-documenting alias for exactly that bound.
///
/// You **never need to implement this trait manually** — there is a
/// blanket impl that covers any `T: Send + Sync + 'static`. It exists only
/// so that bounds on public items read as `T: SharedState` instead of
/// `T: Send + Sync + 'static`, and so that compiler error messages point
/// at a single meaningful trait name.
///
/// ```
/// # use sqlite_ext_core::SharedState;
/// use std::sync::atomic::AtomicUsize;
/// use std::sync::Mutex;
///
/// fn takes_shared<T: SharedState>() {}
///
/// takes_shared::<AtomicUsize>();           // ok — atomic is Send + Sync + 'static
/// takes_shared::<Mutex<Vec<u8>>>();        // ok — Mutex<T> is Send + Sync if T: Send
/// ```
///
/// See [`DbRegistry`] for a table of common shapes that satisfy this bound.
pub trait SharedState: Send + Sync + 'static {}

impl<T: Send + Sync + 'static> SharedState for T {}

/// Process-wide registry of per-database shared state.
///
/// Typical usage is a single `static` registry per extension, initialized
/// lazily via [`std::sync::LazyLock`] (or `once_cell::sync::Lazy`). The
/// registry itself is just an `Arc` wrapper around a mutex-guarded map, so
/// cloning and sharing it across threads is cheap.
///
/// ## Bounds on `T`
///
/// All operations that touch `T` require `T: `[`SharedState`], which is a
/// self-documenting alias for `Send + Sync + 'static` with a blanket impl
/// (so you never implement it manually). This bound is non-negotiable: a
/// single `DbRegistry` is meant to be shared across every SQLite connection
/// in the process, which means `T` will be read and mutated from whatever
/// threads the host (rusqlite, Go's `mattn/go-sqlite3`, a thread pool, …)
/// uses to execute queries. In particular:
///
/// - **`Send`** — the `Arc<T>` inside every `State<T>` can be moved between
///   threads as connections migrate across workers.
/// - **`Sync`** — `State<T>` derefs to `&T`, and multiple threads can hold
///   live `State<T>` handles simultaneously, so concurrent `&T` access must
///   be sound.
/// - **`'static`** — the registry outlives any given connection (it is
///   process-wide), so `T` must not borrow from anything with a shorter
///   lifetime.
///
/// Since `T` is shared and needs interior mutability for almost any real
/// use case, users typically pick one of:
///
/// | Shape of your state                  | Good choice for `T`                           |
/// |--------------------------------------|-----------------------------------------------|
/// | A single counter or flag             | `AtomicUsize`, `AtomicBool`, …                |
/// | A small struct with atomic fields    | `struct { a: AtomicU64, b: AtomicBool }`      |
/// | Arbitrary mutable state              | `Mutex<Inner>` or `RwLock<Inner>`             |
/// | Read-mostly config loaded once       | `ArcSwap<Inner>` (from the `arc-swap` crate)  |
/// | Lock-free maps/queues                | `DashMap`, `crossbeam` channels, …            |
///
/// A plain `Cell<T>` / `RefCell<T>` is **not** enough — neither is `Sync`,
/// and the compiler will reject them at the bound. If you find yourself
/// wanting `RefCell`, reach for `Mutex` instead.
///
/// See the [module-level docs](crate::registry) for the lookup layering and
/// ownership model.
pub struct DbRegistry<T: SharedState> {
    /// The map uses `Arc<str>` keys so the database path is stored once and
    /// shared between the map key and `InternalEntry::path` (zero
    /// duplication). Values are `Weak<InternalEntry<T>>` so the registry
    /// never keeps state alive on its own — state lives exactly as long as
    /// some connection holds a strong `State<T>` handle.
    pub(crate) map: Arc<RegistryMap<T>>,
}

/// Type alias for the internal registry map.
///
/// Backed by a plain `std::collections::HashMap` behind a `Mutex`. The hot
/// path for extension calls bypasses this map entirely via SQLite's auxdata
/// cache (one pointer dereference), so the map is only touched on cache
/// miss, init, and cleanup. Real workloads touch it a handful of times per
/// query at most, so a single global mutex is plenty — sharded or
/// rw-locking would be over-engineering.
pub(crate) type RegistryMap<T> = Mutex<HashMap<Arc<str>, Weak<InternalEntry<T>>>>;

/// Reference-counted handle to the shared state for one database.
///
/// `State<T>` derefs to `T` directly, so you can treat it like any other
/// `Arc`-style smart pointer. It is cheap to clone and `Send + Sync` as long
/// as `T` is. When the last `State<T>` for a given database is dropped, the
/// corresponding registry entry is removed automatically via
/// `InternalEntry`'s `Drop`.
///
/// The three things most extensions do with a `State<T>`:
///
/// - **Hold it.** `DbRegistry::init(...)` returns one; keep a strong
///   reference in your extension-init closure if you need RAII cleanup to
///   wait until your function's destructor fires.
/// - **Hand it to C.** [`State::into_raw`] converts it to a raw
///   `*mut c_void` suitable for passing as `pApp` to
///   [`sqlite3_create_function_v2`](crate::wrappers::sqlite3_create_function_v2).
/// - **Recover it from C.** [`State::clone_from_raw`] gives you a temporary
///   `State<T>` from a `pApp` pointer inside a scalar function, without
///   touching the C-owned refcount.
#[derive(Debug, Clone)]
pub struct State<T: SharedState>(pub(crate) Arc<InternalEntry<T>>);

impl<T: SharedState> Deref for State<T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        &self.0.state
    }
}

impl<T: SharedState> State<T> {
    /// Consumes the handle and returns a raw pointer that owns one `Arc`
    /// refcount, suitable for passing to SQLite as a `pApp` value.
    ///
    /// Pair this with [`destructor_bridge`] as the `xDestroy` callback when
    /// registering a function, so SQLite drops the refcount when the
    /// connection closes. Inside scalar-function callbacks, use
    /// [`State::clone_from_raw`] to recover a temporary `State` without
    /// consuming SQLite's refcount.
    pub fn into_raw(self) -> *mut c_void {
        Arc::into_raw(self.0) as *mut c_void
    }

    /// Recovers a temporary `State<T>` handle from a raw `pApp` pointer
    /// produced by [`State::into_raw`], *without* affecting the C-side refcount.
    ///
    /// Use this inside scalar-function callbacks. The returned `State` adds
    /// one strong reference for the duration of the callback and drops it
    /// normally on scope exit; SQLite's own refcount is left untouched, so
    /// subsequent callbacks still find the same live handle.
    ///
    /// # Safety
    /// `raw` must be a pointer previously returned by [`State::into_raw`]
    /// for the same `T`, and must still be live (i.e. SQLite hasn't fired
    /// `xDestroy` yet).
    pub unsafe fn clone_from_raw(raw: *mut c_void) -> Self {
        // Reconstruct the Arc from the raw pointer so we can clone it,
        // but we must `forget` the original so we don't decrement the
        // refcount that belongs to SQLite.
        let arc = Arc::from_raw(raw as *const InternalEntry<T>);
        let cloned = arc.clone();
        std::mem::forget(arc);
        State(cloned)
    }
}

/// C-compatible `xDestroy` callback that drops the `Arc` refcount previously
/// leaked by [`State::into_raw`].
///
/// Pass `Some(destructor_bridge::<T>)` as the `xDestroy` argument to
/// [`sqlite3_create_function_v2`](crate::wrappers::sqlite3_create_function_v2)
/// whenever you pass a `State::into_raw()` pointer as `pApp`. SQLite will
/// call this when the connection closes, releasing the refcount and
/// triggering the registry cleanup chain (last `State` dropped →
/// `InternalEntry::drop` → map entry removed).
///
/// # Safety
/// `ptr` must be null or a pointer previously returned by
/// [`State::into_raw`] for the same `T`.
pub unsafe extern "C" fn destructor_bridge<T: SharedState>(ptr: *mut c_void) {
    if !ptr.is_null() {
        drop(Arc::from_raw(ptr as *const InternalEntry<T>));
    }
}

/// Internal wrapper holding the user state plus the bookkeeping needed for
/// RAII self-cleanup from the registry map.
///
/// Not exposed to users directly; they interact with it through [`State<T>`].
#[derive(Debug)]
pub(crate) struct InternalEntry<T: SharedState> {
    /// The user-defined shared state. Exposed to users via `Deref` on
    /// [`State<T>`].
    pub(crate) state: T,

    /// Shared pointer to the database file path. The same `Arc<str>` is used
    /// both here and as the key in the registry map, so the string bytes are
    /// stored exactly once.
    pub(crate) path: Arc<str>,

    /// Weak back-reference to the registry map. Weak (not `Arc`) to avoid a
    /// reference cycle: the map holds a `Weak<InternalEntry>`, and the entry
    /// holds a `Weak<RegistryMap>`, so neither side keeps the other alive.
    /// On `Drop`, the entry tries to upgrade this to remove itself from the
    /// map; if the upgrade fails the registry is already gone and there's
    /// nothing to clean up.
    pub(crate) map: Weak<RegistryMap<T>>,
}

impl<T: SharedState> Drop for InternalEntry<T> {
    /// Removes the entry from the registry map the moment the last `State<T>`
    /// handle for this database is dropped.
    ///
    /// Only touches the map if (a) the registry is still alive and (b) the
    /// `Weak` currently stored under this entry's path still points at
    /// `self`. The identity check in (b) prevents a classic race: if another
    /// connection re-opened the same database *after* our last strong ref
    /// was dropped but *before* this destructor ran, the map now holds a
    /// fresh `Weak` for a new entry — we must not evict it.
    fn drop(&mut self) {
        if let Some(map) = self.map.upgrade() {
            if let Ok(mut guard) = map.lock() {
                let self_ptr = self as *const InternalEntry<T>;
                if let Some(weak) = guard.get(&self.path) {
                    if weak.as_ptr() == self_ptr {
                        guard.remove(&self.path);
                    }
                }
            }
        }
    }
}

impl<T: SharedState> DbRegistry<T> {
    /// Creates a new, empty `DbRegistry`.
    pub fn new() -> Self {
        Self {
            map: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// Returns the shared state for the given database connection, if any.
    ///
    /// Walks the hot and warm lookup layers described in the
    /// [module-level docs](crate::registry):
    ///
    /// 1. If `ctx` is `Some` and the auxdata slot is populated, return the
    ///    cached handle with a single pointer dereference.
    /// 2. Otherwise lock the map and look up by database file path. On hit,
    ///    also write the handle back to the auxdata cache so the next row
    ///    hits layer 1.
    ///
    /// Returns `None` if no state has been initialized for this database.
    ///
    /// # Safety
    /// Both `ctx` and `db` must be either null or valid pointers produced by
    /// SQLite (typically retrieved from a scalar-function callback). Any
    /// other value is undefined behavior.
    pub fn get(&self, ctx: Option<*mut sqlite3_context>, db: *mut sqlite3) -> Option<State<T>> {
        // 1. Layer 1: SQLite AuxData (Logical O(1) bypass)
        if let (Some(ctx_ptr), Some(api)) = (ctx, unsafe { GLOBAL_API }) {
            let raw_cached_ptr = unsafe { (api.get_auxdata)(ctx_ptr, 0) };
            if !raw_cached_ptr.is_null() {
                // Return existing handle from SQLite's internal context memory.
                let temp_arc = unsafe { Arc::from_raw(raw_cached_ptr as *const InternalEntry<T>) };
                let state_to_return = State(temp_arc.clone());
                let _ = Arc::into_raw(temp_arc); // Maintain C-side ownership.
                return Some(state_to_return);
            }
        }

        // 2. Layer 2: Registry Hash Map lookup
        let raw_path = unsafe { get_raw_db_path(db) };

        let state = {
            let guard = self.map.lock().expect("registry map poisoned");
            guard.get(raw_path).and_then(|w| w.upgrade()).map(State)
        };

        // 3. Layer 3: Cache result back in SQLite AuxData for the next row if found
        if let (Some(state), Some(ctx_ptr), Some(api)) = (&state, ctx, unsafe { GLOBAL_API }) {
            let ptr_to_store = Arc::into_raw(state.0.clone()) as *mut c_void;
            unsafe {
                (api.set_auxdata)(ctx_ptr, 0, ptr_to_store, Some(destructor_bridge::<T>));
            }
        }

        state
    }

    /// Returns the existing state for this database, or initializes a new
    /// one by calling `init_fn` if nothing is registered yet.
    ///
    /// On a cache or map hit, `init_fn` is *not* called — it is guaranteed
    /// to run at most once per database per process lifetime. On a miss,
    /// the new state is inserted into the map and (if `ctx` is present)
    /// cached into auxdata for subsequent rows.
    ///
    /// Note that `init_fn` runs while the map mutex is held, so it should
    /// be short and must not recursively call other `DbRegistry` methods —
    /// std's `Mutex` is not reentrant.
    ///
    /// # Safety
    /// Both `ctx` and `db` must be either null or valid pointers produced by
    /// SQLite. Any other value is undefined behavior.
    pub fn init<F>(
        &self,
        ctx: Option<*mut sqlite3_context>,
        db: *mut sqlite3,
        init_fn: F,
    ) -> State<T>
    where
        F: FnOnce() -> T,
    {
        // 1. Try to get existing state first (Hot/Warm path)
        if let Some(state) = self.get(ctx, db) {
            return state;
        }

        // 2. Slow path: Initialize and insert
        let raw_path = unsafe { get_raw_db_path(db) };
        let shared_path: Arc<str> = Arc::from(raw_path);

        let state = {
            use std::collections::hash_map::Entry;
            let mut guard = self.map.lock().expect("registry map poisoned");
            match guard.entry(shared_path.clone()) {
                Entry::Occupied(mut occupied) => {
                    if let Some(existing_state) = occupied.get().upgrade() {
                        State(existing_state)
                    } else {
                        let entry = Arc::new(InternalEntry {
                            state: init_fn(),
                            path: shared_path,
                            map: Arc::downgrade(&self.map),
                        });
                        occupied.insert(Arc::downgrade(&entry));
                        State(entry)
                    }
                }
                Entry::Vacant(vacant) => {
                    let entry = Arc::new(InternalEntry {
                        state: init_fn(),
                        path: shared_path,
                        map: Arc::downgrade(&self.map),
                    });
                    vacant.insert(Arc::downgrade(&entry));
                    State(entry)
                }
            }
        };

        // 3. Cache in SQLite AuxData
        if let (Some(ctx_ptr), Some(api)) = (ctx, unsafe { GLOBAL_API }) {
            let ptr_to_store = Arc::into_raw(state.0.clone()) as *mut c_void;
            unsafe {
                (api.set_auxdata)(ctx_ptr, 0, ptr_to_store, Some(destructor_bridge::<T>));
            }
        }

        state
    }

    /// Removes the map entry for the given database path.
    ///
    /// This is rarely needed — the registry cleans itself up via RAII when
    /// the last `State<T>` handle is dropped. Use `release` only if you
    /// explicitly want to sever the map's `Weak` reference while connections
    /// are still alive (e.g. for testing, or to force re-initialization on
    /// the next lookup). Live `State<T>` handles remain usable; the shared
    /// state itself is only freed when the last strong reference goes away.
    pub fn release(&self, db_path: &str) {
        if let Ok(mut guard) = self.map.lock() {
            guard.remove(db_path);
        }
    }
}

impl<T: SharedState> Default for DbRegistry<T> {
    /// Creates a new, empty `DbRegistry`.
    fn default() -> Self {
        Self::new()
    }
}

/// Resolves a `sqlite3*` handle to a borrowed `&str` view of its file path
/// via `sqlite3_db_filename`, with no allocation.
///
/// Returns `":memory:"` in three cases: a null `db` pointer, a null or
/// empty path from SQLite (in-memory or temp databases), or a non-UTF-8
/// path. The returned slice borrows from SQLite's internal immutable path
/// storage and is valid for as long as the `db` handle stays open.
///
/// # Panics
/// Panics if `db` is non-null but [`crate::api::sqlite3_extension_init2`]
/// has not yet been called — we need `GlobalApi::db_filename` to do the
/// resolution.
///
/// # Safety
/// `db` must be either null or a valid, open `sqlite3*` handle.
pub(crate) unsafe fn get_raw_db_path<'a>(db: *mut sqlite3) -> &'a str {
    if db.is_null() {
        return ":memory:";
    }
    let api = GLOBAL_API
        .expect("sqlite-ext-core: GLOBAL_API not initialized — call sqlite3_extension_init2 first");
    let z_name = b"main\0".as_ptr() as *const c_char;
    let path_ptr = (api.db_filename)(db, z_name);

    if path_ptr.is_null() || *path_ptr == 0 {
        return ":memory:";
    }

    // Convert the raw C-String pointer to a Rust string slice.
    // to_str().unwrap_or checks for UTF-8 validity without allocating.
    CStr::from_ptr(path_ptr).to_str().unwrap_or(":memory:")
}
