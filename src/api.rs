//! Dynamic SQLite API resolution.
//!
//! Loadable extensions receive a `sqlite3_api_routines*` (aka `p_api`) at
//! init time. This module unpacks that pointer into two typed structs of
//! function pointers — [`GlobalApi`] and [`ExtensionApi`] — and stores them
//! in process-wide [`OnceLock`]s. Every FFI call from `sqlite-ext-core`
//! after init dispatches through these resolved pointers, so the crate
//! never needs a static link against libsqlite3 and works in hosts (Go,
//! Python, …) that don't export SQLite symbols to the dynamic linker.
//!
//! ## Ordering contract
//!
//! [`sqlite3_extension_init2`] must be called **exactly once** per process,
//! from inside your `sqlite3_<name>_init` entry point, before anything else
//! in this crate is used. After it runs, the wrappers in
//! [`crate::wrappers`] and the FFI-dependent code paths in
//! [`crate::registry`] are safe to call from any thread.
//!
//! ## Thread safety
//!
//! The two tables are stored in [`OnceLock`]s, which provide write-once
//! semantics and race-free reads without any `unsafe` on the read path.
//! Concurrent calls to `sqlite3_extension_init2` from multiple extensions
//! in the same process are safe: the first caller wins the write, every
//! subsequent caller's `set` is silently dropped.
//!
//! ## Null-slot guard
//!
//! Before [`std::mem::transmute`]ing each slot into a typed function
//! pointer, `sqlite3_extension_init2` checks that the raw `usize` is
//! non-zero and panics with a clear message if it isn't. Without this
//! check, a stripped or incompatible SQLite build that returned a zero
//! slot would silently produce a null function pointer, and the first
//! wrapper call to touch it would be undefined behavior at best and a
//! segfault at worst. The check converts that class of bug into a
//! panic-at-init with the exact slot name and offset instead.

use std::ffi::c_void;
use std::os::raw::{c_char, c_int};
use std::sync::OnceLock;

use crate::ffi::*;

/// Small set of SQLite function pointers that `sqlite-ext-core` itself
/// depends on internally (auxdata cache + db filename lookup).
///
/// Kept deliberately separate from the larger [`ExtensionApi`] so the core
/// registry path does not need to know about every routine an extension
/// might call.
#[derive(Clone, Copy)]
pub struct GlobalApi {
    /// Pointer to `sqlite3_get_auxdata`.
    pub get_auxdata: unsafe extern "C" fn(*mut sqlite3_context, c_int) -> *mut c_void,
    /// Pointer to `sqlite3_set_auxdata`.
    pub set_auxdata: unsafe extern "C" fn(
        *mut sqlite3_context,
        c_int,
        *mut c_void,
        Option<unsafe extern "C" fn(*mut c_void)>,
    ),
    /// Pointer to `sqlite3_db_filename`.
    pub db_filename: unsafe extern "C" fn(*mut sqlite3, *const c_char) -> *const c_char,
}

/// Generic SQLite API function pointers re-exported via the inline wrappers
/// in [`crate::wrappers`]. This is the surface area most scalar-function
/// implementations need: result/value accessors plus
/// `sqlite3_create_function_v2` for registering functions from within your
/// extension entry point.
#[derive(Clone, Copy)]
pub struct ExtensionApi {
    /// Pointer to `sqlite3_context_db_handle`.
    pub context_db_handle: ContextDbHandleFn,
    /// Pointer to `sqlite3_result_blob`.
    pub result_blob: ResultBlobFn,
    /// Pointer to `sqlite3_result_double`.
    pub result_double: ResultDoubleFn,
    /// Pointer to `sqlite3_result_error`.
    pub result_error: ResultErrorFn,
    /// Pointer to `sqlite3_result_int`.
    pub result_int: ResultIntFn,
    /// Pointer to `sqlite3_result_int64`.
    pub result_int64: ResultInt64Fn,
    /// Pointer to `sqlite3_result_null`.
    pub result_null: ResultNullFn,
    /// Pointer to `sqlite3_result_text`.
    pub result_text: ResultTextFn,
    /// Pointer to `sqlite3_user_data`.
    pub user_data: UserDataFn,
    /// Pointer to `sqlite3_value_blob`.
    pub value_blob: ValueBlobFn,
    /// Pointer to `sqlite3_value_bytes`.
    pub value_bytes: ValueBytesFn,
    /// Pointer to `sqlite3_value_double`.
    pub value_double: ValueDoubleFn,
    /// Pointer to `sqlite3_value_int`.
    pub value_int: ValueIntFn,
    /// Pointer to `sqlite3_value_int64`.
    pub value_int64: ValueInt64Fn,
    /// Pointer to `sqlite3_value_numeric_type`.
    pub value_numeric_type: ValueNumericTypeFn,
    /// Pointer to `sqlite3_value_text`.
    pub value_text: ValueTextFn,
    /// Pointer to `sqlite3_value_type`.
    pub value_type: ValueTypeFn,
    /// Pointer to `sqlite3_create_function_v2`.
    pub create_function_v2: CreateFunctionV2Fn,
}

/// Small API used internally by the registry; written once by
/// [`sqlite3_extension_init2`] and read by every FFI-touching path afterwards.
pub(crate) static GLOBAL_API: OnceLock<GlobalApi> = OnceLock::new();
/// Extension-facing API used by the inline wrappers in [`crate::wrappers`];
/// written once by [`sqlite3_extension_init2`].
pub(crate) static EXTENSION_API: OnceLock<ExtensionApi> = OnceLock::new();

/// Reads one slot from the `sqlite3_api_routines` table, asserts it is
/// non-zero, and transmutes it into a typed function pointer.
///
/// Called only from `sqlite3_extension_init2` below. Wrapped in an
/// `#[inline]` helper instead of a macro so the panic message carries a
/// precise slot name and offset and so the transmute site is narrow
/// enough to audit.
///
/// # Safety
/// `slots` must be a valid `*const usize` pointing at the start of the
/// `sqlite3_api_routines` table, and `offset` must be within its length.
/// `T` must be a function-pointer type with the same size as `usize`.
#[inline]
unsafe fn resolve_slot<T: Copy>(slots: *const usize, offset: usize, name: &'static str) -> T {
    // Compile-time assertion: every caller must pass a `T` that is
    // pointer-sized. On every platform sqlite-ext-core targets (64-bit Unix,
    // 64-bit Windows, 32-bit Unix), function pointers are exactly one usize.
    const { assert!(std::mem::size_of::<T>() == std::mem::size_of::<usize>()) };

    let raw = *slots.add(offset);
    assert!(
        raw != 0,
        "sqlite-ext-core: required sqlite3_api_routines slot `{name}` \
         (offset {offset}) is null — the host's libsqlite3 is stripped or \
         otherwise missing this routine. Cannot continue."
    );
    std::mem::transmute_copy::<usize, T>(&raw)
}

/// Unpacks SQLite's `sqlite3_api_routines` table into the process-wide API
/// [`OnceLock`]s used by the rest of this crate.
///
/// This is the *one* function you must call from your extension's entry
/// point (`sqlite3_<name>_init`) before doing anything else.
///
/// ## Idempotency
///
/// Backed by [`OnceLock`], so it is safe to call from every extension entry
/// point in a process: only the first call actually writes the slot values.
/// Subsequent calls attempt to `set` and silently drop the result. A null
/// `p_api` is also a no-op (useful for tests).
///
/// ## Panics
///
/// Panics if any of the required slots in the provided `sqlite3_api_routines`
/// table is null, with a message naming the specific routine. In practice
/// this only happens against stripped or custom SQLite builds that omit a
/// routine we depend on; every supported upstream SQLite release has every
/// slot populated.
///
/// ## Safety
///
/// - `p_api` must be either null or a valid pointer to a `sqlite3_api_routines`
///   structure provided by the SQLite runtime. Any other value is undefined
///   behavior.
/// - The slot indices in [`crate::ffi`] must match the layout of the
///   `sqlite3_api_routines` struct compiled into the host's SQLite. This is
///   true for every supported SQLite release, but if you ever compile
///   against a drastically cut-down or custom SQLite, double-check.
#[no_mangle]
pub unsafe extern "C" fn sqlite3_extension_init2(p_api: *const c_void) {
    if p_api.is_null() {
        return;
    }

    let slots = p_api as *const usize;

    let _ = GLOBAL_API.set(GlobalApi {
        get_auxdata: resolve_slot(slots, SLOT_GET_AUXDATA, "sqlite3_get_auxdata"),
        set_auxdata: resolve_slot(slots, SLOT_SET_AUXDATA, "sqlite3_set_auxdata"),
        db_filename: resolve_slot(slots, SLOT_DB_FILENAME, "sqlite3_db_filename"),
    });

    let _ = EXTENSION_API.set(ExtensionApi {
        context_db_handle: resolve_slot(slots, SLOT_CONTEXT_DB_HANDLE, "sqlite3_context_db_handle"),
        result_blob: resolve_slot(slots, SLOT_RESULT_BLOB, "sqlite3_result_blob"),
        result_double: resolve_slot(slots, SLOT_RESULT_DOUBLE, "sqlite3_result_double"),
        result_error: resolve_slot(slots, SLOT_RESULT_ERROR, "sqlite3_result_error"),
        result_int: resolve_slot(slots, SLOT_RESULT_INT, "sqlite3_result_int"),
        result_int64: resolve_slot(slots, SLOT_RESULT_INT64, "sqlite3_result_int64"),
        result_null: resolve_slot(slots, SLOT_RESULT_NULL, "sqlite3_result_null"),
        result_text: resolve_slot(slots, SLOT_RESULT_TEXT, "sqlite3_result_text"),
        user_data: resolve_slot(slots, SLOT_USER_DATA, "sqlite3_user_data"),
        value_blob: resolve_slot(slots, SLOT_VALUE_BLOB, "sqlite3_value_blob"),
        value_bytes: resolve_slot(slots, SLOT_VALUE_BYTES, "sqlite3_value_bytes"),
        value_double: resolve_slot(slots, SLOT_VALUE_DOUBLE, "sqlite3_value_double"),
        value_int: resolve_slot(slots, SLOT_VALUE_INT, "sqlite3_value_int"),
        value_int64: resolve_slot(slots, SLOT_VALUE_INT64, "sqlite3_value_int64"),
        value_numeric_type: resolve_slot(slots, SLOT_VALUE_NUMERIC_TYPE, "sqlite3_value_numeric_type"),
        value_text: resolve_slot(slots, SLOT_VALUE_TEXT, "sqlite3_value_text"),
        value_type: resolve_slot(slots, SLOT_VALUE_TYPE, "sqlite3_value_type"),
        create_function_v2: resolve_slot(slots, SLOT_CREATE_FUNCTION_V2, "sqlite3_create_function_v2"),
    });
}

/// Returns the resolved [`GlobalApi`] snapshot if
/// [`sqlite3_extension_init2`] has already run, otherwise `None`.
///
/// Exposed primarily so internal paths in this crate can conditionally skip
/// auxdata caching when init has not happened yet (e.g. in tests that use a
/// null `db` pointer).
#[doc(hidden)]
pub fn get_global_api() -> Option<GlobalApi> {
    GLOBAL_API.get().copied()
}
