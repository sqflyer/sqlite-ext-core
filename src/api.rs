//! Dynamic SQLite API resolution.
//!
//! Loadable extensions receive a `sqlite3_api_routines*` (aka `p_api`) at
//! init time. This module unpacks that pointer into two typed structs of
//! function pointers — [`GlobalApi`] and [`ExtensionApi`] — and stores them
//! in process-wide statics. Every FFI call from `sqlite-ext-core` after init
//! dispatches through these resolved pointers, so the crate never needs a
//! static link against libsqlite3 and works in hosts (Go, Python, …) that
//! don't export SQLite symbols to the dynamic linker.
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
//! The statics are written exactly once, under a [`std::sync::Once`] guard,
//! from the thread that calls `sqlite3_extension_init2`. After that they are
//! never mutated again, so subsequent concurrent reads observe a consistent
//! snapshot. Reads use `unsafe { STATIC }` which requires an `unsafe` block
//! but is sound given the write-once invariant.
//!
//! ## Null-slot footgun
//!
//! If SQLite hands back a `p_api` table where one of the slots is zero
//! (because the host was built without that routine), [`std::mem::transmute`]
//! will happily produce a null function pointer. Calling it later is
//! undefined behavior. In practice, all slots this crate reads are core
//! routines that have been in SQLite since the dawn of time, so this is not
//! a real-world concern — but if you resolve newer optional routines via
//! [`ExtensionApi`], validate the raw slot values before transmuting.

use std::ffi::c_void;
use std::os::raw::{c_char, c_int};
use std::sync::Once;

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
pub(crate) static mut GLOBAL_API: Option<GlobalApi> = None;
/// Extension-facing API used by the inline wrappers in [`crate::wrappers`];
/// written once by [`sqlite3_extension_init2`].
pub(crate) static mut EXTENSION_API: Option<ExtensionApi> = None;
/// One-shot latch that guarantees the two static tables above are populated
/// at most once, even if `sqlite3_extension_init2` is called from multiple
/// threads (e.g. lazy-loaded extensions on concurrent connections).
pub(crate) static API_INIT: Once = Once::new();

/// Unpacks SQLite's `sqlite3_api_routines` table into the process-wide API
/// statics used by the rest of this crate.
///
/// This is the *one* function you must call from your extension's entry
/// point (`sqlite3_<name>_init`) before doing anything else.
///
/// ## Idempotency
///
/// Protected by a [`std::sync::Once`], so it is safe to call from every
/// extension entry point in a process: only the first call actually walks
/// the slot table. Subsequent calls are no-ops. A null `p_api` is also a
/// no-op (useful for tests).
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

    API_INIT.call_once(|| {
        GLOBAL_API = Some(GlobalApi {
            get_auxdata: std::mem::transmute(*slots.add(SLOT_GET_AUXDATA)),
            set_auxdata: std::mem::transmute(*slots.add(SLOT_SET_AUXDATA)),
            db_filename: std::mem::transmute(*slots.add(SLOT_DB_FILENAME)),
        });

        EXTENSION_API = Some(ExtensionApi {
            context_db_handle: std::mem::transmute(*slots.add(SLOT_CONTEXT_DB_HANDLE)),
            result_blob: std::mem::transmute(*slots.add(SLOT_RESULT_BLOB)),
            result_double: std::mem::transmute(*slots.add(SLOT_RESULT_DOUBLE)),
            result_error: std::mem::transmute(*slots.add(SLOT_RESULT_ERROR)),
            result_int: std::mem::transmute(*slots.add(SLOT_RESULT_INT)),
            result_int64: std::mem::transmute(*slots.add(SLOT_RESULT_INT64)),
            result_null: std::mem::transmute(*slots.add(SLOT_RESULT_NULL)),
            result_text: std::mem::transmute(*slots.add(SLOT_RESULT_TEXT)),
            user_data: std::mem::transmute(*slots.add(SLOT_USER_DATA)),
            value_blob: std::mem::transmute(*slots.add(SLOT_VALUE_BLOB)),
            value_bytes: std::mem::transmute(*slots.add(SLOT_VALUE_BYTES)),
            value_double: std::mem::transmute(*slots.add(SLOT_VALUE_DOUBLE)),
            value_int: std::mem::transmute(*slots.add(SLOT_VALUE_INT)),
            value_int64: std::mem::transmute(*slots.add(SLOT_VALUE_INT64)),
            value_numeric_type: std::mem::transmute(*slots.add(SLOT_VALUE_NUMERIC_TYPE)),
            value_text: std::mem::transmute(*slots.add(SLOT_VALUE_TEXT)),
            value_type: std::mem::transmute(*slots.add(SLOT_VALUE_TYPE)),
            create_function_v2: std::mem::transmute(*slots.add(SLOT_CREATE_FUNCTION_V2)),
        });
    });
}

/// Returns the resolved [`GlobalApi`] snapshot if
/// [`sqlite3_extension_init2`] has already run, otherwise `None`.
///
/// Exposed primarily so internal paths in this crate can conditionally skip
/// auxdata caching when init has not happened yet (e.g. in tests that use a
/// null `db` pointer).
///
/// # Safety
/// Reads a `static mut`. The write-once invariant makes this sound after init,
/// but callers must not observe a torn value before init completes.
#[doc(hidden)]
pub unsafe fn get_global_api() -> Option<GlobalApi> {
    GLOBAL_API
}
