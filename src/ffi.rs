//! Raw SQLite FFI primitives.
//!
//! This module is pure: it has no dependencies beyond `std`, it does not link
//! against libsqlite3, and it never calls SQLite itself. It just declares the
//! opaque handle types, the function-pointer type aliases that mirror
//! `sqlite3ext.h`, and the slot indices used to fish function pointers out of
//! SQLite's `sqlite3_api_routines` table.
//!
//! ## Opaque handle ABI
//!
//! [`sqlite3`], [`sqlite3_context`], and [`sqlite3_value`] are declared as
//! zero-sized `#[repr(C)]` structs. Extension code only ever holds `*mut` /
//! `*const` pointers to them, and those pointers originate from SQLite
//! itself — we never dereference the struct bodies. Because both this crate
//! and `libsqlite3-sys` use the same "opaque ZST" pattern, the pointer
//! representations are ABI-compatible and pointers can be cast between them.
//!
//! ## `SLOT_*` indices
//!
//! The integer constants at the bottom of this file are offsets into the
//! `sqlite3_api_routines` struct that SQLite passes to loadable extensions as
//! `p_api`. The layout is defined in SQLite's `sqlite3ext.h` and has been
//! append-only since the table was introduced, so these indices are stable
//! for any SQLite release that includes the corresponding routine. The
//! values here have been verified against the upstream `sqlite3ext.h` shipped
//! with SQLite 3.x.

use std::ffi::c_void;
use std::os::raw::{c_char, c_int};

// ─── Raw SQLite FFI Types ───────────────────────────────────────────────────

/// Opaque handle to a SQLite database connection (`sqlite3*` in C).
#[repr(C)]
pub struct sqlite3 {
    _unused: [u8; 0],
}
/// Opaque handle to a SQLite function execution context
/// (`sqlite3_context*` in C — the first argument to scalar functions).
#[repr(C)]
pub struct sqlite3_context {
    _unused: [u8; 0],
}
/// Opaque handle to a SQLite dynamic value (`sqlite3_value*` in C — the
/// elements of the `argv` array passed to scalar functions).
#[repr(C)]
pub struct sqlite3_value {
    _unused: [u8; 0],
}

/// Generic SQLite application callback function pointer.
pub type XFunc = Option<unsafe extern "C" fn(*mut sqlite3_context, c_int, *mut *mut sqlite3_value)>;
/// Generic SQLite application data destructor function pointer.
pub type XDestroy = Option<unsafe extern "C" fn(*mut c_void)>;

/// Function pointer type for `sqlite3_create_function_v2`.
pub type CreateFunctionV2Fn = unsafe extern "C" fn(
    db: *mut sqlite3,
    name: *const c_char,
    n_arg: c_int,
    e_text_rep: c_int,
    p_app: *mut c_void,
    x_func: XFunc,
    x_step: XFunc,
    x_final: Option<unsafe extern "C" fn(*mut sqlite3_context)>,
    x_destroy: Option<unsafe extern "C" fn(*mut c_void)>,
) -> c_int;

/// Function pointer type for `sqlite3_context_db_handle`.
pub type ContextDbHandleFn = unsafe extern "C" fn(*mut sqlite3_context) -> *mut sqlite3;
/// Function pointer type for `sqlite3_result_int64`.
pub type ResultInt64Fn = unsafe extern "C" fn(*mut sqlite3_context, i64);
/// Function pointer type for `sqlite3_result_blob`.
pub type ResultBlobFn = unsafe extern "C" fn(
    *mut sqlite3_context,
    *const c_void,
    c_int,
    Option<unsafe extern "C" fn(*mut c_void)>,
);
/// Function pointer type for `sqlite3_result_double`.
pub type ResultDoubleFn = unsafe extern "C" fn(*mut sqlite3_context, f64);
/// Function pointer type for `sqlite3_result_error`.
pub type ResultErrorFn = unsafe extern "C" fn(*mut sqlite3_context, *const c_char, c_int);
/// Function pointer type for `sqlite3_result_int`.
pub type ResultIntFn = unsafe extern "C" fn(*mut sqlite3_context, c_int);
/// Function pointer type for `sqlite3_result_null`.
pub type ResultNullFn = unsafe extern "C" fn(*mut sqlite3_context);
/// Function pointer type for `sqlite3_result_text`.
pub type ResultTextFn = unsafe extern "C" fn(
    *mut sqlite3_context,
    *const c_char,
    c_int,
    Option<unsafe extern "C" fn(*mut c_void)>,
);
/// Function pointer type for `sqlite3_user_data`.
pub type UserDataFn = unsafe extern "C" fn(*mut sqlite3_context) -> *mut c_void;

/// Function pointer type for `sqlite3_value_blob`.
pub type ValueBlobFn = unsafe extern "C" fn(*mut sqlite3_value) -> *const c_void;
/// Function pointer type for `sqlite3_value_bytes`.
pub type ValueBytesFn = unsafe extern "C" fn(*mut sqlite3_value) -> c_int;
/// Function pointer type for `sqlite3_value_double`.
pub type ValueDoubleFn = unsafe extern "C" fn(*mut sqlite3_value) -> f64;
/// Function pointer type for `sqlite3_value_int`.
pub type ValueIntFn = unsafe extern "C" fn(*mut sqlite3_value) -> c_int;
/// Function pointer type for `sqlite3_value_int64`.
pub type ValueInt64Fn = unsafe extern "C" fn(*mut sqlite3_value) -> i64;
/// Function pointer type for `sqlite3_value_numeric_type`.
pub type ValueNumericTypeFn = unsafe extern "C" fn(*mut sqlite3_value) -> c_int;
/// Function pointer type for `sqlite3_value_text`.
pub type ValueTextFn = unsafe extern "C" fn(*mut sqlite3_value) -> *const c_char;
/// Function pointer type for `sqlite3_value_type`.
pub type ValueTypeFn = unsafe extern "C" fn(*mut sqlite3_value) -> c_int;

/// SQLite success return code. Equivalent to `SQLITE_OK` in C.
pub const SQLITE_OK: c_int = 0;
/// UTF-8 text encoding flag. Pass as `eTextRep` to
/// [`sqlite3_create_function_v2`](crate::wrappers::sqlite3_create_function_v2).
pub const SQLITE_UTF8: c_int = 1;

// ─── `sqlite3_api_routines` slot indices ────────────────────────────────────
//
// Offsets (in units of `*const usize`) into the routine table SQLite hands to
// `sqlite3_extension_init2` as `p_api`. Only the routines this crate actually
// resolves at startup are listed; extend the set if you add a new wrapper.
pub const SLOT_GET_AUXDATA: usize = 61;
pub const SLOT_SET_AUXDATA: usize = 92;
pub const SLOT_DB_FILENAME: usize = 180;
pub const SLOT_CONTEXT_DB_HANDLE: usize = 149;
pub const SLOT_RESULT_BLOB: usize = 78;
pub const SLOT_RESULT_DOUBLE: usize = 79;
pub const SLOT_RESULT_ERROR: usize = 80;
pub const SLOT_RESULT_INT: usize = 82;
pub const SLOT_RESULT_INT64: usize = 83;
pub const SLOT_RESULT_NULL: usize = 84;
pub const SLOT_RESULT_TEXT: usize = 85;
pub const SLOT_VALUE_BLOB: usize = 102;
pub const SLOT_VALUE_BYTES: usize = 103;
pub const SLOT_VALUE_DOUBLE: usize = 105;
pub const SLOT_VALUE_INT: usize = 106;
pub const SLOT_VALUE_INT64: usize = 107;
pub const SLOT_VALUE_NUMERIC_TYPE: usize = 108;
pub const SLOT_VALUE_TEXT: usize = 109;
pub const SLOT_VALUE_TYPE: usize = 113;
pub const SLOT_CREATE_FUNCTION_V2: usize = 162;
pub const SLOT_USER_DATA: usize = 101;
