//! Inline C-mirror wrapper functions for the dynamically resolved SQLite API.
//!
//! Every function in this module has the same shape and name as its
//! `libsqlite3-sys` counterpart — `sqlite3_result_int64`, `sqlite3_value_text`,
//! etc. — so code ported from a C extension usually needs no changes beyond
//! swapping the import. The difference is that these wrappers dispatch
//! through the process-wide [`crate::api::ExtensionApi`] table populated by
//! [`crate::api::sqlite3_extension_init2`] instead of through a static link.
//!
//! Each wrapper is `#[inline(always)]` over a single pointer-to-function call,
//! so the generated code after inlining is indistinguishable from a direct
//! call into libsqlite3.
//!
//! ## Ordering contract
//!
//! **All wrappers in this module panic if called before
//! [`crate::api::sqlite3_extension_init2`] has populated `EXTENSION_API`.**
//! They dispatch through a private helper that unwraps the `OnceLock` with a
//! clear diagnostic, so the first call from a mis-ordered extension
//! produces a named panic at the call site instead of a segfault. The
//! individual function docs are deliberately short — this contract applies
//! uniformly.
//!
//! ## Safety
//!
//! Every wrapper is `unsafe` because the underlying SQLite routine takes raw
//! `*mut sqlite3_context` / `*mut sqlite3_value` / `*mut sqlite3` pointers
//! whose validity can't be checked at the Rust boundary. Callers must pass
//! pointers that originated from SQLite (typically via a scalar-function
//! callback) and that are still live.

use std::ffi::c_void;
use std::os::raw::{c_char, c_int};

use crate::api::{ExtensionApi, EXTENSION_API};
use crate::ffi::*;

/// Borrows the resolved extension API table, panicking with a clear
/// diagnostic if [`crate::api::sqlite3_extension_init2`] has not been
/// called yet. Every wrapper in this module routes through here.
#[inline(always)]
fn api() -> &'static ExtensionApi {
    EXTENSION_API.get().expect(
        "sqlite-ext-core: SQLite API wrapper called before \
         sqlite3_extension_init2(p_api) — make sure your extension's \
         init function invokes sqlite3_extension_init2 before doing \
         anything else",
    )
}

/// Inline wrapper for `sqlite3_user_data` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_user_data(ctx: *mut sqlite3_context) -> *mut c_void {
    (api().user_data)(ctx)
}

/// Inline wrapper for `sqlite3_result_blob` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_result_blob(
    ctx: *mut sqlite3_context,
    val: *const c_void,
    len: c_int,
    destructor: Option<unsafe extern "C" fn(*mut c_void)>,
) {
    (api().result_blob)(ctx, val, len, destructor)
}

/// Inline wrapper for `sqlite3_result_double` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_result_double(ctx: *mut sqlite3_context, val: f64) {
    (api().result_double)(ctx, val)
}

/// Inline wrapper for `sqlite3_result_error` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_result_error(ctx: *mut sqlite3_context, val: *const c_char, len: c_int) {
    (api().result_error)(ctx, val, len)
}

/// Inline wrapper for `sqlite3_result_int` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_result_int(ctx: *mut sqlite3_context, val: c_int) {
    (api().result_int)(ctx, val)
}

/// Inline wrapper for `sqlite3_result_int64` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_result_int64(ctx: *mut sqlite3_context, val: i64) {
    (api().result_int64)(ctx, val)
}

/// Inline wrapper for `sqlite3_result_null` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_result_null(ctx: *mut sqlite3_context) {
    (api().result_null)(ctx)
}

/// Inline wrapper for `sqlite3_result_text` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_result_text(
    ctx: *mut sqlite3_context,
    val: *const c_char,
    len: c_int,
    destructor: Option<unsafe extern "C" fn(*mut c_void)>,
) {
    (api().result_text)(ctx, val, len, destructor)
}

/// Inline wrapper for `sqlite3_value_blob` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_value_blob(val: *mut sqlite3_value) -> *const c_void {
    (api().value_blob)(val)
}

/// Inline wrapper for `sqlite3_value_bytes` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_value_bytes(val: *mut sqlite3_value) -> c_int {
    (api().value_bytes)(val)
}

/// Inline wrapper for `sqlite3_value_double` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_value_double(val: *mut sqlite3_value) -> f64 {
    (api().value_double)(val)
}

/// Inline wrapper for `sqlite3_value_int` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_value_int(val: *mut sqlite3_value) -> c_int {
    (api().value_int)(val)
}

/// Inline wrapper for `sqlite3_value_int64` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_value_int64(val: *mut sqlite3_value) -> i64 {
    (api().value_int64)(val)
}

/// Inline wrapper for `sqlite3_value_numeric_type` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_value_numeric_type(val: *mut sqlite3_value) -> c_int {
    (api().value_numeric_type)(val)
}

/// Inline wrapper for `sqlite3_value_text` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_value_text(val: *mut sqlite3_value) -> *const c_char {
    (api().value_text)(val)
}

/// Inline wrapper for `sqlite3_value_type` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_value_type(val: *mut sqlite3_value) -> c_int {
    (api().value_type)(val)
}

/// Inline wrapper for `sqlite3_context_db_handle` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_context_db_handle(ctx: *mut sqlite3_context) -> *mut sqlite3 {
    (api().context_db_handle)(ctx)
}

/// Inline wrapper for `sqlite3_create_function_v2` that utilizes the dynamically resolved API.
#[inline(always)]
pub unsafe fn sqlite3_create_function_v2(
    db: *mut sqlite3,
    z_func_name: *const c_char,
    n_arg: c_int,
    e_text_rep: c_int,
    p_app: *mut c_void,
    x_func: XFunc,
    x_step: XFunc,
    x_final: Option<unsafe extern "C" fn(*mut sqlite3_context)>,
    x_destroy: Option<unsafe extern "C" fn(*mut c_void)>,
) -> c_int {
    (api().create_function_v2)(
        db,
        z_func_name,
        n_arg,
        e_text_rep,
        p_app,
        x_func,
        x_step,
        x_final,
        x_destroy,
    )
}
