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

use std::borrow::Cow;
use std::collections::HashMap;
use std::ffi::{c_void, CStr};
use std::fmt::Debug;
use std::ops::Deref;
use std::os::raw::{c_char, c_int};
use std::sync::{Arc, Mutex, Weak};

use crate::api::GLOBAL_API;
use crate::ffi::*;

/// Compile-time FNV-1a hash of a byte string.
///
/// Used to derive a deterministic, crate-identity-derived auxdata slot
/// for [`DEFAULT_AUXDATA_SLOT`]. Any library following the same "hash
/// your crate name" convention gets a distinct slot by construction,
/// without any global coordination.
const fn fnv1a_hash(s: &[u8]) -> u32 {
    // FNV-1a parameters (offset basis + prime), unchanged since 1994.
    let mut hash: u32 = 0x811c_9dc5;
    let mut i = 0;
    while i < s.len() {
        hash ^= s[i] as u32;
        hash = hash.wrapping_mul(0x0100_0193);
        i += 1;
    }
    hash
}

/// Default slot index used by [`DbRegistry`] for its per-statement
/// auxdata hot-path cache.
///
/// ## Why not slot `0`, and why not `i32::MAX`
///
/// SQLite's `sqlite3_set_auxdata(ctx, N, ptr, destructor)` uses `N` as a
/// key into a per-statement slot table. The **convention** in the wild
/// is to use slot `N` to cache the parsed form of scalar-function
/// argument `N` — a regex compiled from `argv[0]` goes into slot `0`, a
/// JSON path compiled from `argv[1]` goes into slot `1`, and so on.
/// That makes slots `0..=argc-1` effectively "taken" for any function
/// that wants to do argument caching, which is the single most common
/// auxdata use case in existing C extensions.
///
/// Our registry uses auxdata for a completely different purpose — it
/// caches the `Arc<InternalEntry<T>>` pointer so per-row state
/// retrieval becomes a single load — so it must pick a slot that cannot
/// conflict with the argument-caching convention. A naive fix would be
/// to use `i32::MAX`, but that choice is arbitrary: two libraries that
/// both pick `i32::MAX` would collide with each other silently.
///
/// ## How this value is derived
///
/// `DEFAULT_AUXDATA_SLOT` is the **FNV-1a hash of the literal string
/// `"sqlite-ext-core"`**, masked to 31 bits so the result is always a
/// non-negative `i32`. Any other library that follows the same "hash
/// your crate name" convention for picking its default auxdata slot
/// gets a completely different value, by construction — no global slot
/// registry needed, no hand-coordination between library authors.
///
/// The resulting value is comfortably above SQLite's
/// `SQLITE_LIMIT_FUNCTION_ARG` ceiling (default 1000, absolute max
/// 32767), so it cannot collide with the argument-caching convention
/// either. A compile-time assertion below enforces this invariant.
///
/// Override via [`DbRegistry::with_auxdata_slot`] if you need a
/// different slot for any reason (e.g. you want two `DbRegistry`
/// instances in the same process that share state policies but use
/// different slots for testing isolation).
pub const DEFAULT_AUXDATA_SLOT: c_int =
    (fnv1a_hash(b"sqlite-ext-core") & 0x7FFF_FFFF) as c_int;

// Sanity check: the computed slot must be above the argument-index
// range (SQLITE_LIMIT_FUNCTION_ARG_HI = 32767) so it cannot collide
// with the conventional "slot N caches argument N" pattern. In the
// vanishingly unlikely event that the FNV hash of a future crate name
// lands in [0, 32767], this assertion will fail at compile time and
// we'll need to apply an explicit high-bit offset.
const _: () = assert!(
    DEFAULT_AUXDATA_SLOT > 32767,
    "DEFAULT_AUXDATA_SLOT hashed below the argument-index ceiling; \
     OR with a high-bit mask to push it above SQLITE_LIMIT_FUNCTION_ARG_HI"
);

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

    /// SQLite `sqlite3_auxdata` slot used by the hot-path cache. See
    /// [`DEFAULT_AUXDATA_SLOT`] for the rationale behind the default.
    pub(crate) auxdata_slot: c_int,
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
    /// Creates a new, empty `DbRegistry` using [`DEFAULT_AUXDATA_SLOT`]
    /// for the hot-path auxdata cache.
    pub fn new() -> Self {
        Self::with_auxdata_slot(DEFAULT_AUXDATA_SLOT)
    }

    /// Creates a new, empty `DbRegistry` using an explicit auxdata slot
    /// for the hot-path cache.
    ///
    /// Use this if you know another library or another `DbRegistry`
    /// instance in your process already uses [`DEFAULT_AUXDATA_SLOT`],
    /// or if you want to pick a slot that plays nicely with some other
    /// convention you have for auxdata in this process.
    ///
    /// Note that picking a small slot (especially `0..=argc-1` for any
    /// scalar function you register) is likely to collide with the
    /// standard auxdata-per-argument caching idiom, so prefer
    /// out-of-band values — `i32::MAX`, `i32::MAX - 1`, etc.
    pub fn with_auxdata_slot(slot: c_int) -> Self {
        Self {
            map: Arc::new(Mutex::new(HashMap::new())),
            auxdata_slot: slot,
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
    /// # Panics
    /// Panics if `db` is null. A null `db` has no meaningful per-database
    /// identity and never corresponds to anything a real SQLite extension
    /// would see — this check fails fast on accidental misuse rather than
    /// silently routing the call into a shared "no database" slot.
    ///
    /// # Safety
    /// `db` must be a valid, non-null pointer to an open `sqlite3*` handle
    /// produced by SQLite. `ctx` must be null or a valid `sqlite3_context*`
    /// pointer produced by SQLite (typically from inside a scalar-function
    /// callback). Any other value is undefined behavior.
    pub fn get(&self, ctx: Option<*mut sqlite3_context>, db: *mut sqlite3) -> Option<State<T>> {
        assert!(
            !db.is_null(),
            "sqlite-ext-core: DbRegistry::get called with a null db pointer — \
             pass the `sqlite3*` handle from your extension init function or \
             from sqlite3_context_db_handle(ctx)"
        );
        // 1. Layer 1: SQLite AuxData (Logical O(1) bypass)
        if let (Some(ctx_ptr), Some(api)) = (ctx, GLOBAL_API.get()) {
            let raw_cached_ptr = unsafe { (api.get_auxdata)(ctx_ptr, self.auxdata_slot) };
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
            guard
                .get(raw_path.as_ref())
                .and_then(|w| w.upgrade())
                .map(State)
        };

        // 3. Layer 3: Cache result back in SQLite AuxData for the next row if found
        if let (Some(state), Some(ctx_ptr), Some(api)) = (&state, ctx, GLOBAL_API.get()) {
            let ptr_to_store = Arc::into_raw(state.0.clone()) as *mut c_void;
            unsafe {
                (api.set_auxdata)(
                    ctx_ptr,
                    self.auxdata_slot,
                    ptr_to_store,
                    Some(destructor_bridge::<T>),
                );
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
    /// # Panics
    /// Panics if `db` is null. Same rationale as [`DbRegistry::get`].
    ///
    /// # Safety
    /// `db` must be a valid, non-null pointer to an open `sqlite3*` handle.
    /// `ctx` must be null or a valid `sqlite3_context*` pointer produced by
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
        assert!(
            !db.is_null(),
            "sqlite-ext-core: DbRegistry::init called with a null db pointer — \
             pass the `sqlite3*` handle from your extension init function or \
             from sqlite3_context_db_handle(ctx)"
        );

        // 1. Try to get existing state first (Hot/Warm path)
        if let Some(state) = self.get(ctx, db) {
            return state;
        }

        // 2. Slow path: Initialize and insert
        let raw_path = unsafe { get_raw_db_path(db) };
        let shared_path: Arc<str> = Arc::from(raw_path.as_ref());

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
        if let (Some(ctx_ptr), Some(api)) = (ctx, GLOBAL_API.get()) {
            let ptr_to_store = Arc::into_raw(state.0.clone()) as *mut c_void;
            unsafe {
                (api.set_auxdata)(
                    ctx_ptr,
                    self.auxdata_slot,
                    ptr_to_store,
                    Some(destructor_bridge::<T>),
                );
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

/// Resolves a `sqlite3*` handle to a `Cow<str>` registry key.
///
/// Two return shapes, mutually exclusive by the presence of a real
/// filesystem path:
///
/// - **Borrowed file path.** The common case. If `sqlite3_db_filename`
///   returns a non-null, non-empty pointer and the path is valid UTF-8,
///   we return it as a `&str` borrowed from SQLite's internal immutable
///   storage, valid for as long as the handle stays open. Zero
///   allocations on the hot path.
/// - **Owned in-memory key with NUL sentinel.** An in-memory database, a
///   temp DB with no backing file, or a non-UTF-8 path produces an owned
///   `":memory:\0<ptr>"` string keyed on the raw `db` address.
///
///   The embedded NUL byte is the collision-proof part: SQLite's
///   `sqlite3_db_filename` returns a C string, which by definition
///   cannot contain interior NULs, so a real filesystem path can never
///   produce the same bytes as an in-memory key. Two independent
///   in-memory databases open simultaneously get distinct registry
///   entries because their `db` pointers differ.
///
/// # Panics
/// - Panics (via `debug_assert!`) if `db` is null. This is an internal
///   invariant: [`DbRegistry::get`] and [`DbRegistry::init`] both reject
///   null `db` pointers at the public API boundary, so by the time we
///   reach this helper, `db` is guaranteed non-null in debug builds and
///   cannot sensibly be null in release either.
/// - Panics if [`crate::api::sqlite3_extension_init2`] has not yet been
///   called — we need `GlobalApi::db_filename` to do the resolution.
///
/// # Safety
/// `db` must be a valid, non-null, open `sqlite3*` handle.
pub(crate) unsafe fn get_raw_db_path<'a>(db: *mut sqlite3) -> Cow<'a, str> {
    debug_assert!(
        !db.is_null(),
        "get_raw_db_path called with null db — public API must reject this at the boundary"
    );

    let api = GLOBAL_API
        .get()
        .expect("sqlite-ext-core: GLOBAL_API not initialized — call sqlite3_extension_init2 first");
    let z_name = b"main\0".as_ptr() as *const c_char;
    let path_ptr = (api.db_filename)(db, z_name);

    if !path_ptr.is_null() && *path_ptr != 0 {
        // Real filesystem path — borrow from SQLite's internal storage.
        // to_str checks UTF-8 validity without allocating; on invalid
        // UTF-8 we fall through to the owned NUL-keyed branch so we
        // still get a unique key for this handle.
        if let Ok(s) = CStr::from_ptr(path_ptr).to_str() {
            return Cow::Borrowed(s);
        }
    }

    // In-memory database, temp DB, or non-UTF-8 path. Key by the raw
    // handle address with a NUL-byte prefix that guarantees no collision
    // with any real filesystem path.
    Cow::Owned(format!(":memory:\0{:p}", db))
}
