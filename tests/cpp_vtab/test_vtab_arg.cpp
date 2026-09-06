#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "sqlite3_vtab_arg.hpp"
#include "sqlite3_vtab.hpp"
#include "sqlite3_statement.hpp"

// ============================================================================
// 0. sqlite_vtab_arg_internal Utilities Boundary Tests
// ============================================================================
static void test_vtab_arg_internal_utilities() {
    printf("0. Testing sqlite_vtab_arg_internal utilities (boundary cases)...\n");

    // trim_slice
    SqliteStringView s1 = sqlite_vtab_arg_internal::trim_slice("  hello  ", 0, 9);
    assert(s1 == SqliteStringView("hello"));
    SqliteStringView s2 = sqlite_vtab_arg_internal::trim_slice("    ", 0, 4);
    assert(s2 == SqliteStringView(""));
    SqliteStringView s3 = sqlite_vtab_arg_internal::trim_slice("", 0, 0);
    assert(s3 == SqliteStringView(""));

    // strip_quotes
    assert(sqlite_vtab_arg_internal::strip_quotes(SqliteStringView("\"test\"")) == SqliteStringView("test"));
    assert(sqlite_vtab_arg_internal::strip_quotes(SqliteStringView("'test'")) == SqliteStringView("test"));
    assert(sqlite_vtab_arg_internal::strip_quotes(SqliteStringView("`test`")) == SqliteStringView("test"));
    assert(sqlite_vtab_arg_internal::strip_quotes(SqliteStringView("[test]")) == SqliteStringView("test"));
    assert(sqlite_vtab_arg_internal::strip_quotes(SqliteStringView("\"\"")) == SqliteStringView(""));
    assert(sqlite_vtab_arg_internal::strip_quotes(SqliteStringView("\"")) == SqliteStringView("\""));
    assert(sqlite_vtab_arg_internal::strip_quotes(SqliteStringView("no_quotes")) == SqliteStringView("no_quotes"));
    assert(sqlite_vtab_arg_internal::strip_quotes(SqliteStringView("\"mismatched'")) == SqliteStringView("\"mismatched'"));

    // ci_starts_with_kw
    assert(sqlite_vtab_arg_internal::ci_starts_with_kw("PRIMARY KEY", 11, "PRIMARY KEY", 11) == true);
    assert(sqlite_vtab_arg_internal::ci_starts_with_kw("PRIMARY KEY (a)", 15, "PRIMARY KEY", 11) == true);
    assert(sqlite_vtab_arg_internal::ci_starts_with_kw("PRIMARY_KEY", 11, "PRIMARY KEY", 11) == false);
    assert(sqlite_vtab_arg_internal::ci_starts_with_kw("PRIMARY KEYING", 14, "PRIMARY KEY", 11) == false);

    printf("   [PASS] Internal utilities verified.\n");
}

// ============================================================================
// 1. SqliteVTabParam Unit Tests (Accessors, Parsing, Edge Cases)
// ============================================================================
static void test_vtab_param_accessors_and_conversions() {
    printf("1. Testing SqliteVTabParam typed accessors and edge cases...\n");

    // Default constructor
    SqliteVTabParam p_def;
    assert(p_def.key().empty());
    assert(p_def.value().empty());
    assert(p_def.as_str().empty());

    // Basic key-value
    SqliteVTabParam p1(SqliteStringView("capacity"), SqliteStringView("1024"));
    assert(p1.key() == SqliteStringView("capacity"));
    assert(p1.value() == SqliteStringView("1024"));
    assert(p1.as_str() == SqliteStringView("1024"));

    int v_int = 0;
    assert(p1.as_int(v_int) && v_int == 1024);

    long long v_long = 0;
    assert(p1.as_long(v_long) && v_long == 1024LL);

    sqlite3_int64 v_i64 = 0;
    assert(p1.as_int64(v_i64) && v_i64 == 1024LL);

    size_t v_size = 0;
    assert(p1.as_size(v_size) && v_size == 1024);

    double v_double = 0.0;
    assert(p1.as_double(v_double) && v_double == 1024.0);

    SqliteVTabParam p_ratio(SqliteStringView("ratio"), SqliteStringView("0.875"));
    assert(p_ratio.as_double(v_double) && v_double == 0.875);

    // Booleans - True representations
    bool v_bool = false;
    SqliteVTabParam p_b1(SqliteStringView("b"), SqliteStringView("true"));
    assert(p_b1.as_bool(v_bool) && v_bool == true);

    SqliteVTabParam p_b1_upper(SqliteStringView("b"), SqliteStringView("TRUE"));
    assert(p_b1_upper.as_bool(v_bool) && v_bool == true);

    SqliteVTabParam p_b2(SqliteStringView("b"), SqliteStringView("yes"));
    assert(p_b2.as_bool(v_bool) && v_bool == true);

    SqliteVTabParam p_b3(SqliteStringView("b"), SqliteStringView("on"));
    assert(p_b3.as_bool(v_bool) && v_bool == true);

    SqliteVTabParam p_b4(SqliteStringView("b"), SqliteStringView("1"));
    assert(p_b4.as_bool(v_bool) && v_bool == true);

    SqliteVTabParam p_b_num(SqliteStringView("b"), SqliteStringView("42"));
    assert(p_b_num.as_bool(v_bool) && v_bool == true);

    // Booleans - False representations
    SqliteVTabParam p_b5(SqliteStringView("b"), SqliteStringView("false"));
    assert(p_b5.as_bool(v_bool) && v_bool == false);

    SqliteVTabParam p_b5_upper(SqliteStringView("b"), SqliteStringView("FALSE"));
    assert(p_b5_upper.as_bool(v_bool) && v_bool == false);

    SqliteVTabParam p_b6(SqliteStringView("b"), SqliteStringView("no"));
    assert(p_b6.as_bool(v_bool) && v_bool == false);

    SqliteVTabParam p_b7(SqliteStringView("b"), SqliteStringView("off"));
    assert(p_b7.as_bool(v_bool) && v_bool == false);

    SqliteVTabParam p_b8(SqliteStringView("b"), SqliteStringView("0"));
    assert(p_b8.as_bool(v_bool) && v_bool == false);

    // Invalid boolean strings
    SqliteVTabParam p_b_inv(SqliteStringView("b"), SqliteStringView("invalid_bool"));
    assert(!p_b_inv.as_bool(v_bool));

    // Empty value edge cases
    SqliteVTabParam p_empty(SqliteStringView("k"), SqliteStringView("", 0));
    assert(!p_empty.as_int(v_int));
    assert(!p_empty.as_long(v_long));
    assert(!p_empty.as_double(v_double));
    assert(!p_empty.as_size(v_size));
    assert(!p_empty.as_bool(v_bool));

    // Invalid non-numeric strings
    SqliteVTabParam p_invalid(SqliteStringView("k"), SqliteStringView("abc"));
    assert(!p_invalid.as_int(v_int));
    assert(!p_invalid.as_long(v_long));
    assert(!p_invalid.as_double(v_double));
    assert(!p_invalid.as_size(v_size));

    // SqliteValueOwned dynamic extraction via as_value()
    SqliteVTabParam p_null(SqliteStringView("v"), SqliteStringView("null"));
    SqliteValueOwned val_null = p_null.as_value();
    assert(val_null.type() == SQLITE_NULL);

    SqliteVTabParam p_int_val(SqliteStringView("v"), SqliteStringView("42"));
    SqliteValueOwned val_int = p_int_val.as_value();
    assert(val_int.type() == SQLITE_INTEGER);
    assert(val_int.as_int() == 42);

    SqliteVTabParam p_real_val(SqliteStringView("v"), SqliteStringView("3.14159"));
    SqliteValueOwned val_real = p_real_val.as_value();
    assert(val_real.type() == SQLITE_FLOAT);
    assert(val_real.as_double() > 3.14 && val_real.as_double() < 3.15);

    SqliteVTabParam p_str_quoted(SqliteStringView("v"), SqliteStringView("'hello world'"));
    SqliteValueOwned val_str_q = p_str_quoted.as_value();
    assert(val_str_q.type() == SQLITE_TEXT);
    assert(val_str_q.as_text() == SqliteStringView("hello world"));

    SqliteVTabParam p_str_unquoted(SqliteStringView("v"), SqliteStringView("fast_mode"));
    SqliteValueOwned val_str_u = p_str_unquoted.as_value();
    assert(val_str_u.type() == SQLITE_TEXT);
    assert(val_str_u.as_text() == SqliteStringView("fast_mode"));

    SqliteVTabParam p_bool_val(SqliteStringView("v"), SqliteStringView("true"));
    SqliteValueOwned val_bool = p_bool_val.as_value();
    assert(val_bool.type() == SQLITE_INTEGER);
    assert(val_bool.subtype() == SQLITE_SUBTYPE_BOOL);
    assert(val_bool.as_bool() == true);

    printf("   [PASS] SqliteVTabParam verified.\n");
}

// ============================================================================
// 2. SqliteVTabColumn Unit Tests (Affinities, Constraints, Collation, Defaults)
// ============================================================================
static void test_vtab_column_parsing_and_affinities() {
    printf("2. Testing SqliteVTabColumn affinities, constraints, collation, and defaults...\n");

    // Default constructor
    SqliteVTabColumn col_def;
    assert(col_def.name().empty());
    assert(col_def.definition().empty());
    assert(col_def.full_def().empty());
    assert(col_def.affinity() == SqliteVTabColAffinity::Blob);
    assert(col_def.flags() == ColFlag_None);
    assert(!col_def.not_null());
    assert(!col_def.primary_key());
    assert(!col_def.is_hidden());
    assert(!col_def.has_default());
    assert(col_def.collation().empty());

    // Single token column (name only, no definition)
    SqliteVTabColumn col_name_only(SqliteStringView("payload"));
    assert(col_name_only.name() == SqliteStringView("payload"));
    assert(col_name_only.definition().empty());
    assert(col_name_only.full_def() == SqliteStringView("payload"));
    assert(col_name_only.affinity() == SqliteVTabColAffinity::Blob);

    // Rule 1: INTEGER Affinity ("INT", "INTEGER", "BIGINT", "TINYINT")
    SqliteVTabColumn c1(SqliteStringView("id INTEGER PRIMARY KEY NOT NULL AUTOINCREMENT"));
    assert(c1.name() == SqliteStringView("id"));
    assert(c1.unquoted_name() == SqliteStringView("id"));
    assert(c1.definition() == SqliteStringView("INTEGER PRIMARY KEY NOT NULL AUTOINCREMENT"));
    assert(c1.full_def() == SqliteStringView("id INTEGER PRIMARY KEY NOT NULL AUTOINCREMENT"));
    assert(c1.affinity() == SqliteVTabColAffinity::Integer);
    assert(c1.not_null());
    assert(c1.primary_key());
    assert(c1.is_autoincrement());
    assert(!c1.is_unique());
    assert(c1.flags() & ColFlag_AutoIncr);
    assert(!c1.is_hidden());

    SqliteVTabColumn c1b(SqliteStringView("big_count BIGINT"));
    assert(c1b.affinity() == SqliteVTabColAffinity::Integer);

    // Rule 2: TEXT Affinity ("CHAR", "VARCHAR", "CLOB", "TEXT")
    SqliteVTabColumn c2(SqliteStringView("email VARCHAR(255) UNIQUE NOT NULL"));
    assert(c2.name() == SqliteStringView("email"));
    assert(c2.unquoted_name() == SqliteStringView("email"));
    assert(c2.affinity() == SqliteVTabColAffinity::Text);
    assert(c2.not_null());
    assert(c2.is_unique());
    assert(!c2.is_autoincrement());
    assert(c2.flags() & ColFlag_Unique);
    assert(!c2.primary_key());

    SqliteVTabColumn c2b(SqliteStringView("body CLOB"));
    assert(c2b.affinity() == SqliteVTabColAffinity::Text);

    SqliteVTabColumn c2c(SqliteStringView("description TEXT"));
    assert(c2c.affinity() == SqliteVTabColAffinity::Text);

    // Rule 3: BLOB Affinity ("BLOB" or empty)
    SqliteVTabColumn c3(SqliteStringView("data BLOB"));
    assert(c3.name() == SqliteStringView("data"));
    assert(c3.affinity() == SqliteVTabColAffinity::Blob);

    // Rule 4: REAL Affinity ("REAL", "FLOAT", "DOUBLE")
    SqliteVTabColumn c4a(SqliteStringView("score REAL"));
    assert(c4a.affinity() == SqliteVTabColAffinity::Real);

    SqliteVTabColumn c4b(SqliteStringView("ratio FLOAT"));
    assert(c4b.affinity() == SqliteVTabColAffinity::Real);

    SqliteVTabColumn c4c(SqliteStringView("val DOUBLE PRECISION"));
    assert(c4c.affinity() == SqliteVTabColAffinity::Real);

    // Rule 5: NUMERIC Affinity (Everything else)
    SqliteVTabColumn c5a(SqliteStringView("price NUMERIC"));
    assert(c5a.affinity() == SqliteVTabColAffinity::Numeric);

    SqliteVTabColumn c5b(SqliteStringView("amount DECIMAL(10,2)"));
    assert(c5b.affinity() == SqliteVTabColAffinity::Numeric);

    SqliteVTabColumn c5c(SqliteStringView("is_active BOOLEAN"));
    assert(c5c.affinity() == SqliteVTabColAffinity::Numeric);

    SqliteVTabColumn c5d(SqliteStringView("created_at DATETIME"));
    assert(c5d.affinity() == SqliteVTabColAffinity::Numeric);

    // Virtual Table HIDDEN column
    SqliteVTabColumn c6(SqliteStringView("tag HIDDEN"));
    assert(c6.name() == SqliteStringView("tag"));
    assert(c6.is_hidden());
    assert(c6.flags() & ColFlag_Hidden);

    // Collation sequences
    SqliteVTabColumn c7(SqliteStringView("username TEXT COLLATE NOCASE NOT NULL"));
    assert(c7.name() == SqliteStringView("username"));
    assert(c7.affinity() == SqliteVTabColAffinity::Text);
    assert(c7.not_null());
    assert(c7.collation() == SqliteStringView("NOCASE"));

    SqliteVTabColumn c8(SqliteStringView("code TEXT COLLATE RTRIM"));
    assert(c8.name() == SqliteStringView("code"));
    assert(c8.collation() == SqliteStringView("RTRIM"));

    SqliteVTabColumn c8b(SqliteStringView("hash TEXT COLLATE BINARY"));
    assert(c8b.collation() == SqliteStringView("BINARY"));

    SqliteVTabColumn c8c(SqliteStringView("raw_text TEXT"));
    assert(c8c.collation().empty());

    // Default value clauses
    SqliteVTabColumn c9(SqliteStringView("status TEXT DEFAULT 'active' NOT NULL"));
    assert(c9.name() == SqliteStringView("status"));
    assert(c9.has_default());
    assert(c9.default_value() == SqliteStringView("'active'"));
    assert(c9.not_null());

    SqliteVTabColumn c10(SqliteStringView("retries INT DEFAULT 3"));
    assert(c10.name() == SqliteStringView("retries"));
    assert(c10.has_default());
    assert(c10.default_value() == SqliteStringView("3"));

    SqliteVTabColumn c10b(SqliteStringView("rate REAL DEFAULT 1.25"));
    assert(c10b.has_default());
    assert(c10b.default_value() == SqliteStringView("1.25"));

    SqliteVTabColumn c11(SqliteStringView("created_at REAL DEFAULT (datetime('now'))"));
    assert(c11.name() == SqliteStringView("created_at"));
    assert(c11.has_default());
    assert(c11.default_value() == SqliteStringView("(datetime('now'))"));

    SqliteVTabColumn c12(SqliteStringView("notes TEXT DEFAULT ''"));
    assert(c12.has_default());
    assert(c12.default_value() == SqliteStringView("''"));

    // Declared data_type extraction tests
    assert(c1.data_type() == SqliteStringView("INTEGER"));
    assert(c2.data_type() == SqliteStringView("VARCHAR(255)"));
    assert(c4c.data_type() == SqliteStringView("DOUBLE PRECISION"));
    assert(c5b.data_type() == SqliteStringView("DECIMAL(10,2)"));
    assert(c7.data_type() == SqliteStringView("TEXT"));
    assert(c9.data_type() == SqliteStringView("TEXT"));
    assert(col_name_only.data_type().empty()); // Untyped column has empty data_type()

    SqliteVTabColumn dt_multi(SqliteStringView("val UNSIGNED BIG INT NOT NULL PRIMARY KEY"));
    assert(dt_multi.data_type() == SqliteStringView("UNSIGNED BIG INT"));

    SqliteVTabColumn dt_untyped_constr(SqliteStringView("id PRIMARY KEY NOT NULL"));
    assert(dt_untyped_constr.data_type().empty());

    SqliteVTabColumn dt_check(SqliteStringView("age INT CHECK(age >= 0) NOT NULL"));
    assert(dt_check.data_type() == SqliteStringView("INT"));

    SqliteVTabColumn dt_hidden(SqliteStringView("secret BLOB HIDDEN NOT NULL"));
    assert(dt_hidden.data_type() == SqliteStringView("BLOB"));

    printf("   [PASS] SqliteVTabColumn verified.\n");
}

// ============================================================================
// 3. SqliteVTabConstraint Unit Tests (Composite PK, Unique, Check, FK)
// ============================================================================
static void test_vtab_constraint_parsing_and_types() {
    printf("3. Testing SqliteVTabConstraint (Composite Primary Key, Unique, Check, FK)...\n");

    // Default constructor
    SqliteVTabConstraint c_def;
    assert(c_def.kind() == SqliteVTabConstraintKind::Unknown);
    assert(c_def.name().empty());
    assert(c_def.columns_raw().empty());
    assert(c_def.full_def().empty());
    assert(c_def.column_count() == 0);
    assert(!c_def.is_primary_key());
    assert(!c_def.is_unique());
    assert(!c_def.is_check());
    assert(!c_def.is_foreign_key());

    // Anonymous composite Primary Key
    SqliteVTabConstraint pk1(SqliteStringView("PRIMARY KEY (user_id, device_id, timestamp)"));
    assert(pk1.is_primary_key());
    assert(!pk1.is_unique());
    assert(!pk1.is_check());
    assert(!pk1.is_foreign_key());
    assert(pk1.name().empty());
    assert(!pk1.has_name());
    assert(pk1.columns_raw() == SqliteStringView("user_id, device_id, timestamp"));
    assert(pk1.column_count() == 3);
    assert(pk1.has_column(SqliteStringView("user_id")));
    assert(pk1.has_column(SqliteStringView("USER_ID"))); // Case-insensitive
    assert(pk1.has_column(SqliteStringView("device_id")));
    assert(pk1.has_column(SqliteStringView("timestamp")));
    assert(!pk1.has_column(SqliteStringView("other")));

    // PRIMARY_KEY alternate underscore syntax
    SqliteVTabConstraint pk1_alt(SqliteStringView("PRIMARY_KEY (k1, k2)"));
    assert(pk1_alt.is_primary_key());
    assert(!pk1_alt.has_name());
    assert(pk1_alt.column_count() == 2);

    // Named Primary Key constraint
    SqliteVTabConstraint pk2(SqliteStringView("CONSTRAINT pk_custom PRIMARY KEY (tenant_id, org_id)"));
    assert(pk2.is_primary_key());
    assert(pk2.has_name());
    assert(pk2.name() == SqliteStringView("pk_custom"));
    assert(pk2.column_count() == 2);
    assert(pk2.has_column(SqliteStringView("tenant_id")));
    assert(pk2.has_column(SqliteStringView("org_id")));

    // Unique table constraint
    SqliteVTabConstraint uq(SqliteStringView("CONSTRAINT uq_email UNIQUE (domain, username)"));
    assert(uq.is_unique());
    assert(!uq.is_primary_key());
    assert(uq.has_name());
    assert(uq.name() == SqliteStringView("uq_email"));
    assert(uq.column_count() == 2);
    assert(uq.has_column(SqliteStringView("domain")));
    assert(uq.has_column(SqliteStringView("username")));

    // Check table constraint
    SqliteVTabConstraint ck(SqliteStringView("CHECK (score >= 0 AND score <= 100)"));
    assert(ck.is_check());
    assert(ck.columns_raw() == SqliteStringView("score >= 0 AND score <= 100"));

    // Foreign key table constraint
    SqliteVTabConstraint fk(SqliteStringView("FOREIGN KEY (user_id) REFERENCES users(id)"));
    assert(fk.is_foreign_key());

    // Empty constraint string
    SqliteVTabConstraint c_empty(SqliteStringView("", 0));
    assert(c_empty.kind() == SqliteVTabConstraintKind::Unknown);

    printf("   [PASS] SqliteVTabConstraint verified.\n");
}

// ============================================================================
// 4. SqliteVTabArg Tagged Union & Lifecycle Tests
// ============================================================================
static void test_vtab_arg_tagged_union_lifecycle() {
    printf("4. Testing SqliteVTabArg Tagged Union lifecycle and factory methods...\n");

    // 1. Default constructor
    SqliteVTabArg a_empty;
    assert(a_empty.is_empty());
    assert(a_empty.kind() == SqliteVTabArg::Kind::Empty);
    assert(!a_empty.is_param());
    assert(!a_empty.is_column());
    assert(!a_empty.is_constraint());

    // 2. Factory methods
    SqliteVTabArg a_param = SqliteVTabArg::make_param(SqliteStringView("ttl"), SqliteStringView("60"));
    assert(a_param.is_param());
    assert(a_param.param().key() == SqliteStringView("ttl"));
    assert(a_param.param().value() == SqliteStringView("60"));
    assert(a_param.key_is(SqliteStringView("ttl")));
    assert(!a_param.key_is(SqliteStringView("other")));

    SqliteVTabArg a_col = SqliteVTabArg::make_column(SqliteStringView("id INT PRIMARY KEY"));
    assert(a_col.is_column());
    assert(a_col.column().name() == SqliteStringView("id"));
    assert(a_col.column().primary_key());

    SqliteVTabArg a_cons = SqliteVTabArg::make_constraint(SqliteStringView("PRIMARY KEY(a, b)"));
    assert(a_cons.is_constraint());
    assert(a_cons.constraint().is_primary_key());
    assert(a_cons.constraint().column_count() == 2);

    // 3. Raw C-String Parsing constructor
    SqliteVTabArg a_raw_null(nullptr);
    assert(a_raw_null.is_empty());

    SqliteVTabArg a_raw_empty("");
    assert(a_raw_empty.is_empty());

    SqliteVTabArg a_raw_spaces("    \t\r\n   ");
    assert(a_raw_spaces.is_empty());

    SqliteVTabArg a_raw_p("capacity=1024");
    assert(a_raw_p.is_param());
    assert(a_raw_p.param().key() == SqliteStringView("capacity"));

    SqliteVTabArg a_raw_c("name TEXT NOT NULL");
    assert(a_raw_c.is_column());
    assert(a_raw_c.column().name() == SqliteStringView("name"));

    SqliteVTabArg a_raw_cons("PRIMARY KEY (k1, k2)");
    assert(a_raw_cons.is_constraint());

    // 4. Copy Construction
    SqliteVTabArg a_copy_empty(a_empty);
    assert(a_copy_empty.is_empty());

    SqliteVTabArg a_copy_param(a_param);
    assert(a_copy_param.is_param());
    assert(a_copy_param.param().key() == SqliteStringView("ttl"));

    SqliteVTabArg a_copy_col(a_col);
    assert(a_copy_col.is_column());
    assert(a_copy_col.column().name() == SqliteStringView("id"));

    SqliteVTabArg a_copy_cons(a_cons);
    assert(a_copy_cons.is_constraint());
    assert(a_copy_cons.constraint().column_count() == 2);

    // 5. Copy Assignment & Self-Assignment
    SqliteVTabArg a_assign;
    a_assign = a_param;
    assert(a_assign.is_param());
    assert(a_assign.param().key() == SqliteStringView("ttl"));

    a_assign = a_col;
    assert(a_assign.is_column());
    assert(a_assign.column().name() == SqliteStringView("id"));

    a_assign = a_cons;
    assert(a_assign.is_constraint());
    assert(a_assign.constraint().column_count() == 2);

    a_assign = a_empty;
    assert(a_assign.is_empty());

    // Self assignment check
    a_assign = *&a_assign;
    assert(a_assign.is_empty());

    // Non-const accessors
    SqliteVTabArg a_mut = SqliteVTabArg::make_param(SqliteStringView("k"), SqliteStringView("v"));
    a_mut.param().m_key = SqliteStringView("k2");
    assert(a_mut.param().key() == SqliteStringView("k2"));

    SqliteVTabArg a_mut_col = SqliteVTabArg::make_column(SqliteStringView("score REAL"));
    a_mut_col.column().m_name = SqliteStringView("price");
    assert(a_mut_col.column().name() == SqliteStringView("price"));

    printf("   [PASS] SqliteVTabArg Tagged Union verified.\n");
}

// ============================================================================
// 5. SqliteVTabArgs & Multi Primary Key Integration Tests
// ============================================================================
static void test_vtab_args_batch_extraction_and_lookup() {
    printf("5. Testing SqliteVTabArgs batch parsing & multi-PK aggregation...\n");

    const char* raw_argv[] = {
        "sqlite3",                  // argv[0]: module name
        "main",                     // argv[1]: db name
        "my_vtab",                  // argv[2]: table name
        "user_id INTEGER NOT NULL", // argv[3]: col 0
        "device_id INT NOT NULL",   // argv[4]: col 1
        "payload TEXT",             // argv[5]: col 2
        "PRIMARY KEY (user_id, device_id)", // argv[6]: Table-level Composite PK
        "capacity=5000",            // argv[7]: Param 0
        "ttl=120",                  // argv[8]: Param 1
        "mode=strict",              // argv[9]: Param 2
        "enabled=true"              // argv[10]: Param 3
    };

    SqliteVTabArgs args(11, raw_argv, 3);
    assert(args.argc() == 11);
    assert(args.user_argc() == 8);

    // operator[] indexing
    assert(args[0].is_column()); // argv[3]
    assert(args[3].is_constraint()); // argv[6]
    assert(args[4].is_param()); // argv[7]
    assert(args[100].is_empty()); // Out-of-bounds
    assert(args[-10].is_empty()); // Out-of-bounds

    // 1. Parameter lookups
    assert(args.has(SqliteStringView("capacity")));
    assert(!args.has(SqliteStringView("non_existent")));
    assert(args.get_int(SqliteStringView("capacity"), 0) == 5000);
    assert(args.get_long(SqliteStringView("capacity"), 0LL) == 5000LL);
    assert(args.get_size(SqliteStringView("capacity"), 0) == 5000);
    assert(args.get_double(SqliteStringView("capacity"), 0.0) == 5000.0);
    assert(args.get_int(SqliteStringView("ttl"), 0) == 120);
    assert(args.get_str(SqliteStringView("mode")) == SqliteStringView("strict"));
    assert(args.get_bool(SqliteStringView("enabled")) == true);
    assert(args.get_int(SqliteStringView("non_existent"), 42) == 42);

    // 2. Column count & names
    int col_count = 0;
    args.for_each_column([&](const SqliteVTabColumn& col) {
        if (col_count == 0) assert(col.name() == SqliteStringView("user_id"));
        if (col_count == 1) assert(col.name() == SqliteStringView("device_id"));
        if (col_count == 2) assert(col.name() == SqliteStringView("payload"));
        ++col_count;
    });
    assert(col_count == 3);

    // 3. Composite Primary Key verification
    assert(args.is_composite_primary_key());
    assert(args.primary_key_count() == 2);
    assert(args.is_primary_key_column(SqliteStringView("user_id")));
    assert(args.is_primary_key_column(SqliteStringView("device_id")));
    assert(!args.is_primary_key_column(SqliteStringView("payload")));

    // 4. Schema Binding
    static const char* kModes[] = { "normal", "strict", "fast" };
    int capacity = 0;
    int ttl = 0;
    bool enabled = false;
    int mode_idx = -1;

    SqliteVTabParamSchema schema;
    schema.bind_int(SqliteStringView("capacity"), &capacity)
          .bind_int(SqliteStringView("ttl"), &ttl)
          .bind_bool(SqliteStringView("enabled"), &enabled)
          .bind_enum(SqliteStringView("mode"), kModes, 3, &mode_idx);

    int matched = schema.parse(args);
    assert(matched == 4);
    assert(capacity == 5000);
    assert(ttl == 120);
    assert(enabled == true);
    assert(mode_idx == 1); // "strict" is index 1

    SqliteValueOwned val_capacity;
    SqliteVTabParamSchema schema2;
    schema2.bind_value(SqliteStringView("capacity"), &val_capacity);
    int matched2 = schema2.parse(args);
    assert(matched2 == 1);
    assert(val_capacity.type() == SQLITE_INTEGER);
    assert(val_capacity.as_int() == 5000);

    // Direct get_value lookup:
    SqliteValueOwned val_mode = args.get_value(SqliteStringView("mode"));
    assert(val_mode.type() == SQLITE_TEXT);
    assert(val_mode.as_text() == SqliteStringView("strict"));

    SqliteValueOwned val_enabled = args.get_value(SqliteStringView("enabled"));
    assert(val_enabled.type() == SQLITE_INTEGER);
    assert(val_enabled.subtype() == SQLITE_SUBTYPE_BOOL);
    assert(val_enabled.as_bool() == true);

    SqliteValueOwned val_missing = args.get_value(SqliteStringView("missing"), SqliteValueOwned(999));
    assert(val_missing.type() == SQLITE_INTEGER);
    assert(val_missing.as_int() == 999);

    // 5. Index & Count Queries
    assert(args.column_count() == 3);
    assert(args.param_count() == 4);
    assert(args.constraint_count() == 1);

    assert(args.column_index(SqliteStringView("user_id")) == 0);
    assert(args.column_index(SqliteStringView("device_id")) == 1);
    assert(args.column_index(SqliteStringView("payload")) == 2);
    assert(args.column_index(SqliteStringView("non_existent")) == -1);

    assert(args.column_at(0).name() == SqliteStringView("user_id"));
    assert(args.column_at(1).name() == SqliteStringView("device_id"));
    assert(args.column_at(2).name() == SqliteStringView("payload"));
    assert(args.column_at(99).name().empty()); // Out-of-bounds column

    int indexed_count = 0;
    args.for_each_column_indexed([&](const SqliteVTabColumn& col, int idx) {
        assert(idx == indexed_count);
        if (idx == 0) assert(col.name() == SqliteStringView("user_id"));
        if (idx == 1) assert(col.name() == SqliteStringView("device_id"));
        if (idx == 2) assert(col.name() == SqliteStringView("payload"));
        ++indexed_count;
    });
    assert(indexed_count == 3);

    // for_each all args
    int total_visited = 0;
    args.for_each([&](const SqliteVTabArg& a) {
        assert(!a.is_empty());
        ++total_visited;
    });
    assert(total_visited == 8);

    // for_each_param
    int param_visited = 0;
    args.for_each_param([&](const SqliteVTabParam& p) {
        assert(!p.key().empty());
        ++param_visited;
    });
    assert(param_visited == 4);

    // for_each_constraint
    int constraint_visited = 0;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        assert(c.is_primary_key());
        ++constraint_visited;
    });
    assert(constraint_visited == 1);

    printf("   [PASS] SqliteVTabArgs verified.\n");
}

// ============================================================================
// 6. Mixed Order Arguments (Parameters at Top, Middle, and Bottom)
// ============================================================================
static void test_vtab_args_mixed_order_and_defaults() {
    printf("6. Testing SqliteVTabArgs with parameters at the TOP, middle, and bottom...\n");

    const char* mixed_argv[] = {
        "sqlite3",                         // argv[0]
        "main",                            // argv[1]
        "mixed_vtab",                      // argv[2]
        "capacity=1024",                   // argv[3]: PARAM AT TOP
        "mode=fast",                       // argv[4]: PARAM AT TOP
        "user_id INTEGER NOT NULL",        // argv[5]: Column 0
        "device_id INT NOT NULL",          // argv[6]: Column 1
        "status TEXT DEFAULT 'active'",    // argv[7]: Column 2 (WITH DEFAULT)
        "PRIMARY KEY(user_id, device_id)", // argv[8]: Table constraint
        "ttl=60",                          // argv[9]: PARAM IN MIDDLE
        "strict=true",                     // argv[10]: PARAM IN MIDDLE
        "metadata TEXT",                   // argv[11]: Column 3 (AT BOTTOM)
        "flush_interval=5.5"               // argv[12]: PARAM AT BOTTOM
    };

    SqliteVTabArgs args(13, mixed_argv, 3);
    assert(args.user_argc() == 10);

    // 1. Parameter queries from any position
    assert(args.get_int(SqliteStringView("capacity"), 0) == 1024);
    assert(args.get_str(SqliteStringView("mode")) == SqliteStringView("fast"));
    assert(args.get_int(SqliteStringView("ttl"), 0) == 60);
    assert(args.get_bool(SqliteStringView("strict")) == true);
    assert(args.get_double(SqliteStringView("flush_interval"), 0.0) == 5.5);
    assert(args.param_count() == 5);

    // 1b. Parameter Fallback Defaults (when parameter is missing)
    assert(args.get_int(SqliteStringView("missing_int"), 42) == 42);
    assert(args.get_size(SqliteStringView("missing_size"), 2048) == 2048);
    assert(args.get_bool(SqliteStringView("missing_bool"), true) == true);
    assert(args.get_double(SqliteStringView("missing_double"), 3.14) == 3.14);
    assert(args.get_str(SqliteStringView("missing_str"), SqliteStringView("fallback")) == SqliteStringView("fallback"));

    // 2. Column indexing correctly assigns 0, 1, 2, 3 regardless of surrounding params
    assert(args.column_count() == 4);
    assert(args.column_index(SqliteStringView("user_id")) == 0);
    assert(args.column_index(SqliteStringView("device_id")) == 1);
    assert(args.column_index(SqliteStringView("status")) == 2);
    assert(args.column_index(SqliteStringView("metadata")) == 3);
    assert(args.column_index(SqliteStringView("capacity")) == -1);

    assert(args.column_at(0).name() == SqliteStringView("user_id"));
    assert(args.column_at(1).name() == SqliteStringView("device_id"));
    assert(args.column_at(2).name() == SqliteStringView("status"));
    assert(args.column_at(3).name() == SqliteStringView("metadata"));

    // 2b. Column DEFAULT constraint extraction
    assert(args.column_at(2).has_default() == true);
    assert(args.column_at(2).default_value() == SqliteStringView("'active'"));
    assert(args.column_at(0).has_default() == false);
    assert(args.column_at(3).has_default() == false);

    // 3. Composite Primary Key
    assert(args.is_composite_primary_key());
    assert(args.primary_key_count() == 2);
    assert(args.is_primary_key_column(SqliteStringView("user_id")));
    assert(args.is_primary_key_column(SqliteStringView("device_id")));
    assert(!args.is_primary_key_column(SqliteStringView("status")));
    assert(!args.is_primary_key_column(SqliteStringView("metadata")));

    printf("   [PASS] Mixed order arguments verified.\n");
}

// ============================================================================
// 7. SqliteVTabParamSchema Full Coverage (All Types, Enum Mismatch, Max Limit)
// ============================================================================
static void test_vtab_param_schema_binding_and_parsing() {
    printf("7. Testing SqliteVTabParamSchema comprehensive binding coverage...\n");

    const char* test_argv[] = {
        "mod", "db", "t",
        "i=10",
        "l=20000000000",
        "d=3.1415",
        "s=8192",
        "b=true",
        "str=hello",
        "val=42.5",
        "choice=medium"
    };

    SqliteVTabArgs args(11, test_argv, 3);

    int              val_i = 0;
    long long        val_l = 0;
    double           val_d = 0.0;
    size_t           val_s = 0;
    bool             val_b = false;
    SqliteStringView val_str;
    SqliteValueOwned val_owned;
    int              val_choice = -1;

    static const char* kChoices[] = { "low", "medium", "high" };

    SqliteVTabParamSchema schema;
    schema.bind_int(SqliteStringView("i"), &val_i)
          .bind_long(SqliteStringView("l"), &val_l)
          .bind_double(SqliteStringView("d"), &val_d)
          .bind_size(SqliteStringView("s"), &val_s)
          .bind_bool(SqliteStringView("b"), &val_b)
          .bind_str(SqliteStringView("str"), &val_str)
          .bind_value(SqliteStringView("val"), &val_owned)
          .bind_enum(SqliteStringView("choice"), kChoices, 3, &val_choice);

    assert(schema.binding_count() == 8);

    int parsed_count = schema.parse(args);
    assert(parsed_count == 8);
    assert(val_i == 10);
    assert(val_l == 20000000000LL);
    assert(val_d > 3.14 && val_d < 3.15);
    assert(val_s == 8192);
    assert(val_b == true);
    assert(val_str == SqliteStringView("hello"));
    assert(val_owned.type() == SQLITE_FLOAT);
    assert(val_choice == 1); // "medium" is index 1

    // bind_int64 explicit verification
    sqlite3_int64 val_i64 = 0;
    SqliteVTabParamSchema schema_i64;
    schema_i64.bind_int64(SqliteStringView("l"), &val_i64);
    assert(schema_i64.parse(args) == 1);
    assert(val_i64 == 20000000000LL);

    // Enum not matching returns -1
    const char* bad_enum_argv[] = { "mod", "db", "t", "choice=extreme" };
    SqliteVTabArgs bad_args(4, bad_enum_argv, 3);
    int bad_choice = 99;
    SqliteVTabParamSchema bad_schema;
    bad_schema.bind_enum(SqliteStringView("choice"), kChoices, 3, &bad_choice);
    bad_schema.parse(bad_args);
    assert(bad_choice == -1);

    // Max parameters boundary (16 limits)
    SqliteVTabParamSchema max_schema;
    int dummy = 0;
    for (int i = 0; i < SqliteVTabParamSchema::MAX_PARAMS; ++i) {
        max_schema.bind_int(SqliteStringView("p"), &dummy);
    }
    assert(max_schema.binding_count() == 16);
    // 17th slot is safely rejected
    max_schema.bind_int(SqliteStringView("extra"), &dummy);
    assert(max_schema.binding_count() == 16);

    printf("   [PASS] SqliteVTabParamSchema full coverage verified.\n");
}

// ============================================================================
// 8. Virtual Table Module with Dynamic Argument Parsing Integration
// ============================================================================
class ArgTestVTable;

class ArgTestCursor : public SqliteVTabCursor {
public:
    int m_rowid;
    ArgTestVTable* m_tab;

    ArgTestCursor(ArgTestVTable* tab) : m_rowid(0), m_tab(tab) {}

    int filter(int, const char*, SqliteUdfArgs) override {
        m_rowid = 1;
        return SQLITE_OK;
    }

    int next() override {
        m_rowid++;
        return SQLITE_OK;
    }

    bool eof() override {
        return m_rowid > 1;
    }

    int column(SqliteContext& ctx, int i) override;

    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_rowid;
        return SQLITE_OK;
    }
};

class ArgTestVTable : public SqliteVTable {
public:
    size_t m_capacity;
    int    m_mode_idx;
    bool   m_is_composite_pk;

    ArgTestVTable(sqlite3* db, size_t cap, int mode, bool composite_pk)
        : SqliteVTable(db), m_capacity(cap), m_mode_idx(mode), m_is_composite_pk(composite_pk) {}

    static int connect(SqliteConnectArgs& args) {
        SqliteVTabArgs vargs(args);

        static const char* kModes[] = { "normal", "strict", "fast" };
        size_t capacity = 100;
        int mode_idx = 0;

        SqliteVTabParamSchema schema;
        schema.bind_size(SqliteStringView("capacity"), &capacity)
              .bind_enum(SqliteStringView("mode"), kModes, 3, &mode_idx);
        schema.parse(vargs);

        bool composite_pk = vargs.is_composite_primary_key();

        // Build dynamic schema string
        char schema_buf[256];
        snprintf(schema_buf, sizeof(schema_buf), 
                 "CREATE TABLE x(k1 INT, k2 INT, val TEXT, cap INT, mode INT, is_comp INT)");

        int rc = sqlite3_declare_vtab(args.db(), schema_buf);
        if (rc != SQLITE_OK) return rc;

        args.set_instance(sqlite_new<ArgTestVTable>(args.db(), capacity, mode_idx, composite_pk));
        return SQLITE_OK;
    }

    static int create(SqliteConnectArgs& args) {
        return connect(args);
    }

    int bestIndex(SqliteIndexInfo& info) override {
        info.set_estimated_cost(10.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<ArgTestCursor>(this);
    }
};

inline int ArgTestCursor::column(SqliteContext& ctx, int i) {
    if (i == 0) ctx.result_int(10);
    else if (i == 1) ctx.result_int(20);
    else if (i == 2) ctx.result_text(SqliteStringView("test_val"));
    else if (i == 3) ctx.result_int64(static_cast<sqlite3_int64>(m_tab->m_capacity));
    else if (i == 4) ctx.result_int(m_tab->m_mode_idx);
    else if (i == 5) ctx.result_int(m_tab->m_is_composite_pk ? 1 : 0);
    return SQLITE_OK;
}

static void test_vtab_sql_table_creation_and_querying() {
    printf("8. Testing Virtual Table SQL instantiation with multi-PK args...\n");

    sqlite3* db = nullptr;
    int rc = sqlite3_open(":memory:", &db);
    assert(rc == SQLITE_OK && db != nullptr);

    rc = SqliteVTab::define<ArgTestVTable>(db, "arg_test_vtab");
    assert(rc == SQLITE_OK);

    // Create table with composite PK and custom parameters
    char* err_msg = nullptr;
    rc = sqlite3_exec(
        db,
        "CREATE VIRTUAL TABLE t1 USING arg_test_vtab("
        "  user_id INT, "
        "  device_id INT, "
        "  payload TEXT, "
        "  PRIMARY KEY (user_id, device_id), "
        "  capacity=8192, "
        "  mode=fast"
        ");",
        nullptr, nullptr, &err_msg
    );
    if (rc != SQLITE_OK) {
        printf("Error: %s\n", err_msg ? err_msg : "unknown");
        sqlite3_free(err_msg);
    }
    assert(rc == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT cap, mode, is_comp FROM t1;", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    assert(sqlite3_column_int64(stmt, 0) == 8192);
    assert(sqlite3_column_int(stmt, 1) == 2); // "fast" is index 2
    assert(sqlite3_column_int(stmt, 2) == 1); // composite PK is true

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    printf("   [PASS] Virtual Table SQL execution with multi-PK verified.\n");
}

// ============================================================================
// 9. Edge Cases & Boundary Conditions
// ============================================================================
static void test_vtab_args_syntax_edge_cases_and_boundaries() {
    printf("9. Testing boundary cases (spaces, nested parens, quotes, etc.)...\n");

    const char* edge_argv[] = {
        "mod", "db", "t",
        "capacity = 1024",                    // 3: Param with spaces around '='
        "[my special col] INTEGER",           // 4: Column with brackets and spaces
        "\"complex \"\"name\" TEXT",          // 5: Column with inner escaped quotes
        "created_at TEXT DEFAULT (datetime('now', 'localtime'))", // 6: Nested parens in DEFAULT
        "PRIMARY KEY ( a ,  b , c )",         // 7: Extra whitespaces in PK constraint
        "my_col TEXT DEFAULT 'PRIMARY KEY'",  // 8: Keyword inside a string literal
        "other_col TEXT DEFAULT 'NOT NULL'",  // 9: Keyword inside a string literal
        "id INTEGER PRIMARY   KEY",           // 10: Multiple spaces inside keyword
        "CONSTRAINT \"my const\" UNIQUE (x)", // 11: Quoted constraint name
        "\"a=b\" INT",                        // 12: Column name containing '='
        "val TEXT COLLATE \"my collation\"",  // 13: Quoted collation name
        "tricky TEXT DEFAULT (' ) ')",        // 14: Parens inside quoted DEFAULT expression
        "empty_val = ",                       // 15: Parameter with empty value
        "col VARCHAR(255) PRIMARY KEY",       // 16: Column with parens in type, followed by keyword
        "multiline INT \n DEFAULT 5",         // 17: Multiline definition
        "CHECK (a = 5 AND b = 10)",           // 18: CHECK constraint with parens and AND
        "\"PRIMARY KEY\" INT",                // 19: Column name that is exactly a keyword
        "UNIQUE (\"a,b\", c)",                // 20: Quoted column name with comma in constraint
        "\"par=am\" = \"key=val\"",           // 21: Parameter name and value with equal signs and quotes
        "escaped_col TEXT DEFAULT 'a '' b'",  // 22: Escaped quote inside string literal
        "CHECK(a IN (1, 2, 3))",              // 23: Nested parens in constraint
        "FOREIGN KEY (col1) REFERENCES t(c) ON DELETE CASCADE", // 24: FK with trailing clauses
        "\nCONSTRAINT\n\"newline\"\nUNIQUE\n(\ny\n)\n", // 25: Multiline constraint
        "PRIMARY KEY (tenant_id ASC, user_id DESC)",          // 26: ASC/DESC in constraint
        "UNIQUE (\"first name\" COLLATE NOCASE ASC, [last name] DESC)", // 27: COLLATE + ASC/DESC in constraint
        "negative_val REAL DEFAULT -12.34",                   // 28: Negative floating point default
        "positive_val INT DEFAULT +99",                       // 29: Positive signed int default
        "blob_hex BLOB DEFAULT X'DEADBEEF'",                  // 30: Hex literal default
        "flag_bool BOOLEAN DEFAULT TRUE",                     // 31: Boolean keyword default
        "json_param = '{\"host\": \"localhost\", \"port\": 8080}'", // 32: JSON string param
        "calc INT GENERATED ALWAYS AS (a * 2 + 1) STORED",    // 33: Generated column
        "auto_id INTEGER PRIMARY KEY AUTOINCREMENT",          // 34: AUTOINCREMENT flag
        "[ユーザー_id] INTEGER NOT NULL",                      // 35: Unicode identifier
        "price DECIMAL(10, 2) NOT NULL",                      // 36: Type with comma in precision
        "ts DATETIME DEFAULT CURRENT_TIMESTAMP",              // 37: Timestamp keyword default
        "scale = 1.25e-5",                                    // 38: Scientific notation float param
        "threshold = +3.4E+10",                               // 39: Positive exponent float param
        "debug = yes",                                        // 40: Boolean param 'yes'
        "logging = off",                                      // 41: Boolean param 'off'
        "uuid TEXT NOT NULL UNIQUE PRIMARY KEY",              // 42: Multiple inline constraints
        "FOREIGN KEY (author_id) REFERENCES users(id) ON UPDATE CASCADE ON DELETE SET NULL", // 43: Complex FK
        "PRIMARY KEY (`user_id`, [device_id])",               // 44: Mixed backticks & brackets PK
        "empty_quotes = ''",                                  // 45: Empty quoted string param
        "feature_on = ON",                                    // 46: Boolean 'ON'
        "allow_no = NO",                                      // 47: Boolean 'NO'
        "big_val UNSIGNED BIG INT",                           // 48: Multi-word INT affinity
        "precise DOUBLE PRECISION",                           // 49: Multi-word REAL affinity
        "expr_col INT DEFAULT (1 + (2 * 3))",                 // 50: Nested arithmetic default
        "null_kw TEXT DEFAULT NULL",                          // 51: NULL keyword default
        "null_str TEXT DEFAULT 'NULL'",                       // 52: 'NULL' string default
        "col_coll TEXT COLLATE RTRIM \t ",                    // 53: Collation with trailing whitespace
        "offset = -100",                                      // 54: Negative integer param
        "max_bytes = 4294967296",                             // 55: 64-bit unsigned size param
        "PRIMARY KEY (c1, c2, c3, c4, c5)",                   // 56: 5-column composite PK
        "CONSTRAINT \"pk\"\"custom\" PRIMARY KEY (k_id)",     // 57: Escaped quotes in constraint name
        "secret_col BLOB HIDDEN",                             // 58: Hidden column
        "internal_id INT NOT NULL HIDDEN",                    // 59: Hidden + NOT NULL column
        "typeless_col",                                       // 60: Untyped column
        "data_col NOT NULL",                                  // 61: Untyped with constraint
        "id_col PRIMARY KEY",                                 // 62: Untyped with PK
        "db_path = 'C:\\data\\test.db'",                      // 63: Escaped Windows file path param
        "endpoint = 'http://localhost:8080/api/v1?query=1&page=2'", // 64: URL param with query string
        "\"🚀rocket\" TEXT",                                   // 65: Emoji in quoted identifier
        "[🔥fire_level] INT",                                 // 66: Emoji in bracketed identifier
        "text_sq TEXT COLLATE 'NOCASE'",                      // 67: Single-quoted collation name
        "code_bt TEXT COLLATE `BINARY`",                      // 68: Backtick collation name
        "tag_br TEXT COLLATE [RTRIM]",                        // 69: Bracket collation name
        "c_date DATE DEFAULT CURRENT_DATE",                   // 70: CURRENT_DATE default
        "c_time TIME DEFAULT CURRENT_TIME",                   // 71: CURRENT_TIME default
        "UNIQUE (\"col A\", `col B`, [col C], col_d)",        // 72: Mixed quote styles composite UNIQUE
        "`my``name` TEXT",                                    // 73: Escaped backtick in identifier
        "[my]]name] INTEGER",                                 // 74: Escaped bracket in identifier
        "tag TEXT CHECK (length(tag) > 0 AND tag != ' ')",    // 75: CHECK with string containing spaces
        "pointing_coord POINT",                               // 76: POINT contains INT rule
        "floating_val FLOAT",                                 // 77: FLOAT contains FLOA rule
        "blob_field BLOB_DATA",                               // 78: BLOB_DATA contains BLOB rule
        "flag_int_0 = 0",                                     // 79: Integer boolean 0
        "flag_int_1 = 1",                                     // 80: Integer boolean 1
        "flag_int_neg = -1",                                  // 81: Integer boolean -1
        "query_filter = '  SELECT * FROM test  '",            // 82: String with leading/trailing spaces
        "FOREIGN KEY (fk_a, fk_b) REFERENCES parent(pk_x, pk_y)", // 83: Multi-col FK
        "PRIMARY KEY ([user_code] COLLATE NOCASE ASC, `org_id` DESC)", // 84: Mixed collation & quotes PK
        "commented_col INT /* note */ NOT NULL",              // 85: C-style comment in column
        "lower_col integer primary key autoincrement not null unique" // 86: All lowercase keywords
    };

    SqliteVTabArgs args(87, edge_argv, 3);

    // 1. Spaces around '=' in Parameters
    // 'capacity = 1024' should be correctly parsed as a param.
    assert(args.has(SqliteStringView("capacity")));
    assert(args.get_int(SqliteStringView("capacity"), 0) == 1024);

    // 2. Complex Quoted Column Names
    int idx_special = args.column_index(SqliteStringView("my special col"));
    assert(idx_special >= 0);
    assert(args.column_at(idx_special).name() == SqliteStringView("[my special col]"));

    int idx_complex = args.column_index(SqliteStringView("complex \"\"name"));
    assert(idx_complex >= 0);

    // 3. Parentheses in DEFAULT Clauses
    int idx_created = args.column_index(SqliteStringView("created_at"));
    assert(idx_created >= 0);
    SqliteStringView def_val = args.column_at(idx_created).default_value();
    assert(def_val == SqliteStringView("(datetime('now', 'localtime'))"));

    // 4. Whitespace inside PRIMARY KEY
    bool has_b = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_primary_key() && c.has_column(SqliteStringView("b"))) {
            has_b = true;
        }
    });
    assert(has_b);

    // 5. Keyword inside a string literal
    int idx_my_col = args.column_index(SqliteStringView("my_col"));
    assert(idx_my_col >= 0);
    assert(!args.column_at(idx_my_col).primary_key()); // Should NOT be a primary key

    int idx_other = args.column_index(SqliteStringView("other_col"));
    assert(idx_other >= 0);
    assert(!args.column_at(idx_other).not_null()); // Should NOT be NOT NULL

    // 6. Multiple spaces inside keyword
    int idx_id = args.column_index(SqliteStringView("id"));
    assert(idx_id >= 0);
    assert(args.column_at(idx_id).primary_key()); // 'PRIMARY   KEY' should be recognized

    // 7. Quoted constraint name
    bool has_x = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_unique() && c.has_column(SqliteStringView("x"))) {
            has_x = true;
        }
    });
    if (!has_x) {
        printf("   [!] BUG DETECTED: CONSTRAINT \"my const\" UNIQUE (x) failed to parse constraint name/type.\n");
    }

    // 8. Column name containing '='
    int idx_eq = args.column_index(SqliteStringView("a=b"));
    if (idx_eq < 0) {
        printf("   [!] BUG DETECTED: Column '\"a=b\" INT' was incorrectly parsed as a Parameter!\n");
    }

    // 9. Quoted collation name
    int idx_coll = args.column_index(SqliteStringView("val"));
    if (idx_coll >= 0) {
        SqliteStringView coll = args.column_at(idx_coll).collation();
        if (coll != SqliteStringView("\"my collation\"")) {
            printf("   [!] BUG DETECTED: COLLATE \"my collation\" extracted as '%.*s'\n", coll.length(), coll.data());
        }
    }

    // 10. Parens inside quoted DEFAULT expression
    int idx_tricky = args.column_index(SqliteStringView("tricky"));
    if (idx_tricky >= 0) {
        SqliteStringView def_val_tricky = args.column_at(idx_tricky).default_value();
        if (def_val_tricky != SqliteStringView("(' ) ')")) {
            printf("   [!] BUG DETECTED: Tricky DEFAULT extracted as '%.*s'\n", def_val_tricky.length(), def_val_tricky.data());
        }
    }

    // 11. Empty Parameter Value
    if (!args.has(SqliteStringView("empty_val")) || args.get_str(SqliteStringView("empty_val")) != SqliteStringView("")) {
        printf("   [!] BUG DETECTED: empty_val = parameter not parsed correctly.\n");
    }

    // 12. Type Parens + Keyword
    int idx_vchar = args.column_index(SqliteStringView("col"));
    if (idx_vchar >= 0 && !args.column_at(idx_vchar).primary_key()) {
        printf("   [!] BUG DETECTED: col VARCHAR(255) PRIMARY KEY did not detect primary key.\n");
    }

    // 13. Multiline Definitions
    int idx_multi = args.column_index(SqliteStringView("multiline"));
    if (idx_multi >= 0) {
        SqliteStringView def_val_multi = args.column_at(idx_multi).default_value();
        if (def_val_multi != SqliteStringView("5")) {
            printf("   [!] BUG DETECTED: Multiline DEFAULT extracted as '%.*s'\n", def_val_multi.length(), def_val_multi.data());
        }
    }

    // 14. Keyword as Column Name
    int idx_pk = args.column_index(SqliteStringView("PRIMARY KEY"));
    if (idx_pk < 0) {
        printf("   [!] BUG DETECTED: '\"PRIMARY KEY\" INT' not detected as a column name.\n");
    }

    // 15. Quoted Commas in Constraints
    bool has_ab = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_unique() && c.has_column(SqliteStringView("a,b"))) {
            has_ab = true;
        }
    });
    if (!has_ab) {
        printf("   [!] BUG DETECTED: UNIQUE (\"a,b\", c) failed to parse \"a,b\".\n");
    }

    // 16. Parameter with equal sign in name and value
    if (!args.has(SqliteStringView("\"par=am\"")) || args.get_str(SqliteStringView("\"par=am\"")) != SqliteStringView("\"key=val\"")) {
        printf("   [!] BUG DETECTED: \"par=am\" = \"key=val\" failed to parse.\n");
    }

    // 17. Escaped quote inside string literal
    int idx_esc = args.column_index(SqliteStringView("escaped_col"));
    if (idx_esc >= 0) {
        SqliteStringView def_val_esc = args.column_at(idx_esc).default_value();
        if (def_val_esc != SqliteStringView("'a '' b'")) {
            printf("   [!] BUG DETECTED: Escaped quote DEFAULT extracted as '%.*s'\n", def_val_esc.length(), def_val_esc.data());
        }
    }

    // 18. Nested parens in CHECK constraint
    bool has_check_in = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_check() && c.columns_raw() == SqliteStringView("a IN (1, 2, 3)")) {
            has_check_in = true;
        }
    });
    if (!has_check_in) {
        printf("   [!] BUG DETECTED: CHECK(a IN (1, 2, 3)) failed to parse columns_raw.\n");
    }

    // 19. Foreign Key trailing clauses
    bool has_fk = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_foreign_key() && c.has_column(SqliteStringView("col1"))) {
            has_fk = true;
        }
    });
    if (!has_fk) {
        printf("   [!] BUG DETECTED: FOREIGN KEY (col1) ... failed to parse.\n");
    }

    // 20. Multiline constraint
    bool has_multiline_const = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_unique() && c.has_column(SqliteStringView("y")) && c.name() == SqliteStringView("newline")) {
            has_multiline_const = true;
        }
    });
    if (!has_multiline_const) {
        printf("   [!] BUG DETECTED: Multiline constraint failed to parse.\n");
    }

    // 21. ASC / DESC in Constraint Columns
    bool has_asc_desc_pk = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_primary_key() && c.has_column(SqliteStringView("tenant_id")) && c.has_column(SqliteStringView("user_id"))) {
            has_asc_desc_pk = true;
        }
    });
    if (!has_asc_desc_pk) {
        printf("   [!] BUG DETECTED: PRIMARY KEY (tenant_id ASC, user_id DESC) column extraction failed.\n");
    }

    // 22. Quoted + COLLATE + ASC/DESC in UNIQUE constraint
    bool has_collate_unique = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_unique() && c.has_column(SqliteStringView("first name")) && c.has_column(SqliteStringView("last name"))) {
            has_collate_unique = true;
        }
    });
    if (!has_collate_unique) {
        printf("   [!] BUG DETECTED: UNIQUE (\"first name\" COLLATE NOCASE ASC, [last name] DESC) column extraction failed.\n");
    }

    // 23. Signed numeric defaults
    int idx_neg = args.column_index(SqliteStringView("negative_val"));
    if (idx_neg >= 0 && args.column_at(idx_neg).default_value() != SqliteStringView("-12.34")) {
        printf("   [!] BUG DETECTED: negative_val DEFAULT mismatch: '%.*s'\n", args.column_at(idx_neg).default_value().length(), args.column_at(idx_neg).default_value().data());
    }
    int idx_pos = args.column_index(SqliteStringView("positive_val"));
    if (idx_pos >= 0 && args.column_at(idx_pos).default_value() != SqliteStringView("+99")) {
        printf("   [!] BUG DETECTED: positive_val DEFAULT mismatch: '%.*s'\n", args.column_at(idx_pos).default_value().length(), args.column_at(idx_pos).default_value().data());
    }

    // 24. Hex literal & Boolean keyword defaults
    int idx_hex = args.column_index(SqliteStringView("blob_hex"));
    if (idx_hex >= 0 && args.column_at(idx_hex).default_value() != SqliteStringView("X'DEADBEEF'")) {
        printf("   [!] BUG DETECTED: blob_hex DEFAULT mismatch: '%.*s'\n", args.column_at(idx_hex).default_value().length(), args.column_at(idx_hex).default_value().data());
    }
    int idx_bool = args.column_index(SqliteStringView("flag_bool"));
    if (idx_bool >= 0 && args.column_at(idx_bool).default_value() != SqliteStringView("TRUE")) {
        printf("   [!] BUG DETECTED: flag_bool DEFAULT mismatch: '%.*s'\n", args.column_at(idx_bool).default_value().length(), args.column_at(idx_bool).default_value().data());
    }

    // 25. JSON string parameter
    if (!args.has(SqliteStringView("json_param")) || args.get_str(SqliteStringView("json_param")) != SqliteStringView("'{\"host\": \"localhost\", \"port\": 8080}'")) {
        printf("   [!] BUG DETECTED: json_param failed to extract raw string.\n");
    }

    // 26. GENERATED ALWAYS column & AUTOINCREMENT
    int idx_calc = args.column_index(SqliteStringView("calc"));
    if (idx_calc >= 0) {
        assert(args.column_at(idx_calc).affinity() == SqliteVTabColAffinity::Integer);
    }
    int idx_auto = args.column_index(SqliteStringView("auto_id"));
    if (idx_auto >= 0) {
        assert(args.column_at(idx_auto).primary_key());
        assert((args.column_at(idx_auto).flags() & ColFlag_AutoIncr) != 0);
    }

    // 27. Unicode identifier
    int idx_unicode = args.column_index(SqliteStringView("ユーザー_id"));
    if (idx_unicode < 0) {
        printf("   [!] BUG DETECTED: Unicode column '[ユーザー_id]' was not found.\n");
    } else {
        assert(args.column_at(idx_unicode).not_null());
    }

    // 28. Type with comma in precision: DECIMAL(10, 2)
    int idx_price = args.column_index(SqliteStringView("price"));
    if (idx_price < 0) {
        printf("   [!] BUG DETECTED: price column was not found.\n");
    } else {
        assert(args.column_at(idx_price).affinity() == SqliteVTabColAffinity::Numeric);
        assert(args.column_at(idx_price).not_null());
    }

    // 29. Timestamp keyword default: CURRENT_TIMESTAMP
    int idx_ts = args.column_index(SqliteStringView("ts"));
    if (idx_ts < 0 || args.column_at(idx_ts).default_value() != SqliteStringView("CURRENT_TIMESTAMP")) {
        printf("   [!] BUG DETECTED: ts DEFAULT mismatch: '%.*s'\n",
               idx_ts >= 0 ? args.column_at(idx_ts).default_value().length() : 0,
               idx_ts >= 0 ? args.column_at(idx_ts).default_value().data() : "");
    }

    // 30. Scientific notation float param
    double scale_val = args.get_double(SqliteStringView("scale"), 0.0);
    if (!args.has(SqliteStringView("scale")) || scale_val < 1.24e-5 || scale_val > 1.26e-5) {
        printf("   [!] BUG DETECTED: scale param failed to parse float (got %f).\n", scale_val);
    }

    // 31. Positive exponent float param
    double thresh_val = args.get_double(SqliteStringView("threshold"), 0.0);
    if (!args.has(SqliteStringView("threshold")) || thresh_val < 3.39e10 || thresh_val > 3.41e10) {
        printf("   [!] BUG DETECTED: threshold param failed to parse float.\n");
    }

    // 32. Boolean words 'yes' / 'off'
    bool b_debug = args.get_bool(SqliteStringView("debug"), false);
    bool b_log = args.get_bool(SqliteStringView("logging"), true);
    if (!args.has(SqliteStringView("debug")) || !b_debug) {
        printf("   [!] BUG DETECTED: debug = yes failed to parse as true.\n");
    }
    if (!args.has(SqliteStringView("logging")) || b_log) {
        printf("   [!] BUG DETECTED: logging = off failed to parse as false.\n");
    }

    // 33. Multiple inline constraints
    int idx_uuid = args.column_index(SqliteStringView("uuid"));
    if (idx_uuid < 0) {
        printf("   [!] BUG DETECTED: uuid column was not found.\n");
    } else {
        assert(args.column_at(idx_uuid).not_null());
        assert(args.column_at(idx_uuid).primary_key());
        assert((args.column_at(idx_uuid).flags() & ColFlag_Unique) != 0);
    }

    // 34. Complex Foreign Key with ON UPDATE and ON DELETE
    bool has_author_fk = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_foreign_key() && c.has_column(SqliteStringView("author_id"))) {
            has_author_fk = true;
        }
    });
    if (!has_author_fk) {
        printf("   [!] BUG DETECTED: FOREIGN KEY (author_id) ... failed to extract author_id.\n");
    }

    // 35. Mixed backticks & brackets composite PK
    bool has_mixed_pk = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_primary_key() && c.has_column(SqliteStringView("user_id")) && c.has_column(SqliteStringView("device_id"))) {
            has_mixed_pk = true;
        }
    });
    if (!has_mixed_pk) {
        printf("   [!] BUG DETECTED: PRIMARY KEY (`user_id`, [device_id]) failed to parse.\n");
    }

    // 36. Empty quoted string param
    if (!args.has(SqliteStringView("empty_quotes")) || args.get_str(SqliteStringView("empty_quotes")) != SqliteStringView("''")) {
        printf("   [!] BUG DETECTED: empty_quotes = '' failed to extract.\n");
    }

    // 37. Boolean tokens ON / NO
    bool b_on = args.get_bool(SqliteStringView("feature_on"), false);
    bool b_no = args.get_bool(SqliteStringView("allow_no"), true);
    if (!args.has(SqliteStringView("feature_on")) || !b_on) {
        printf("   [!] BUG DETECTED: feature_on = ON failed to parse as true.\n");
    }
    if (!args.has(SqliteStringView("allow_no")) || b_no) {
        printf("   [!] BUG DETECTED: allow_no = NO failed to parse as false.\n");
    }

    // 38. Multi-word affinities: UNSIGNED BIG INT & DOUBLE PRECISION
    int idx_big = args.column_index(SqliteStringView("big_val"));
    if (idx_big < 0 || args.column_at(idx_big).affinity() != SqliteVTabColAffinity::Integer) {
        printf("   [!] BUG DETECTED: big_val UNSIGNED BIG INT affinity mismatch.\n");
    }
    int idx_prec = args.column_index(SqliteStringView("precise"));
    if (idx_prec < 0 || args.column_at(idx_prec).affinity() != SqliteVTabColAffinity::Real) {
        printf("   [!] BUG DETECTED: precise DOUBLE PRECISION affinity mismatch.\n");
    }

    // 39. Nested arithmetic expression default
    int idx_expr = args.column_index(SqliteStringView("expr_col"));
    if (idx_expr < 0 || args.column_at(idx_expr).default_value() != SqliteStringView("(1 + (2 * 3))")) {
        printf("   [!] BUG DETECTED: expr_col DEFAULT mismatch: '%.*s'\n",
               idx_expr >= 0 ? args.column_at(idx_expr).default_value().length() : 0,
               idx_expr >= 0 ? args.column_at(idx_expr).default_value().data() : "");
    }

    // 40. NULL keyword vs 'NULL' string literal
    int idx_nkw = args.column_index(SqliteStringView("null_kw"));
    if (idx_nkw < 0 || args.column_at(idx_nkw).default_value() != SqliteStringView("NULL")) {
        printf("   [!] BUG DETECTED: null_kw DEFAULT mismatch.\n");
    }
    int idx_nstr = args.column_index(SqliteStringView("null_str"));
    if (idx_nstr < 0 || args.column_at(idx_nstr).default_value() != SqliteStringView("'NULL'")) {
        printf("   [!] BUG DETECTED: null_str DEFAULT mismatch.\n");
    }

    // 41. Collation with trailing whitespace
    int idx_rtrim = args.column_index(SqliteStringView("col_coll"));
    if (idx_rtrim < 0 || args.column_at(idx_rtrim).collation() != SqliteStringView("RTRIM")) {
        printf("   [!] BUG DETECTED: col_coll collation mismatch: '%.*s'\n",
               idx_rtrim >= 0 ? args.column_at(idx_rtrim).collation().length() : 0,
               idx_rtrim >= 0 ? args.column_at(idx_rtrim).collation().data() : "");
    }

    // 42. Negative integer parameter & 64-bit size
    if (args.get_int(SqliteStringView("offset"), 0) != -100) {
        printf("   [!] BUG DETECTED: offset = -100 failed.\n");
    }
    if (args.get_size(SqliteStringView("max_bytes"), 0) != 4294967296ULL) {
        printf("   [!] BUG DETECTED: max_bytes = 4294967296 failed.\n");
    }

    // 43. 5-column composite PK
    bool has_5_pk = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_primary_key() && c.column_count() == 5) {
            if (c.has_column(SqliteStringView("c1")) && c.has_column(SqliteStringView("c5"))) {
                has_5_pk = true;
            }
        }
    });
    if (!has_5_pk) {
        printf("   [!] BUG DETECTED: 5-column composite PK failed to parse.\n");
    }

    // 44. Escaped quotes in constraint name
    bool has_custom_pk_name = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_primary_key() && c.has_column(SqliteStringView("k_id"))) {
            has_custom_pk_name = true;
        }
    });
    if (!has_custom_pk_name) {
        printf("   [!] BUG DETECTED: CONSTRAINT \"pk\"\"custom\" PRIMARY KEY (k_id) failed to parse.\n");
    }

    // 45. Hidden column flag
    int idx_sec = args.column_index(SqliteStringView("secret_col"));
    if (idx_sec < 0 || !args.column_at(idx_sec).is_hidden() || args.column_at(idx_sec).affinity() != SqliteVTabColAffinity::Blob) {
        printf("   [!] BUG DETECTED: secret_col BLOB HIDDEN failed to parse.\n");
    }
    int idx_int_id = args.column_index(SqliteStringView("internal_id"));
    if (idx_int_id < 0 || !args.column_at(idx_int_id).is_hidden() || !args.column_at(idx_int_id).not_null()) {
        printf("   [!] BUG DETECTED: internal_id INT NOT NULL HIDDEN failed to parse.\n");
    }

    // 46. Untyped column
    int idx_type_less = args.column_index(SqliteStringView("typeless_col"));
    if (idx_type_less < 0 || !args.column_at(idx_type_less).definition().empty()) {
        printf("   [!] BUG DETECTED: typeless_col failed.\n");
    }

    // 47. Untyped with constraints
    int idx_data_col = args.column_index(SqliteStringView("data_col"));
    if (idx_data_col < 0 || !args.column_at(idx_data_col).not_null()) {
        printf("   [!] BUG DETECTED: data_col NOT NULL failed.\n");
    }
    int idx_id_col = args.column_index(SqliteStringView("id_col"));
    if (idx_id_col < 0 || !args.column_at(idx_id_col).primary_key()) {
        printf("   [!] BUG DETECTED: id_col PRIMARY KEY failed.\n");
    }

    // 48. Escaped path & URL params
    if (!args.has(SqliteStringView("db_path")) || args.get_str(SqliteStringView("db_path")) != SqliteStringView("'C:\\data\\test.db'")) {
        printf("   [!] BUG DETECTED: db_path failed.\n");
    }
    if (!args.has(SqliteStringView("endpoint")) || args.get_str(SqliteStringView("endpoint")) != SqliteStringView("'http://localhost:8080/api/v1?query=1&page=2'")) {
        printf("   [!] BUG DETECTED: endpoint failed.\n");
    }

    // 49. Emoji identifiers
    int idx_rocket = args.column_index(SqliteStringView("🚀rocket"));
    if (idx_rocket < 0) {
        printf("   [!] BUG DETECTED: 🚀rocket column failed.\n");
    }
    int idx_fire = args.column_index(SqliteStringView("🔥fire_level"));
    if (idx_fire < 0) {
        printf("   [!] BUG DETECTED: 🔥fire_level column failed.\n");
    }

    // 50. Single-quote, Backtick, and Bracket collation names
    int idx_tsq = args.column_index(SqliteStringView("text_sq"));
    if (idx_tsq < 0 || args.column_at(idx_tsq).collation() != SqliteStringView("'NOCASE'")) {
        printf("   [!] BUG DETECTED: text_sq collation mismatch: '%.*s'\n",
               idx_tsq >= 0 ? args.column_at(idx_tsq).collation().length() : 0,
               idx_tsq >= 0 ? args.column_at(idx_tsq).collation().data() : "");
    }
    int idx_cbt = args.column_index(SqliteStringView("code_bt"));
    if (idx_cbt < 0 || args.column_at(idx_cbt).collation() != SqliteStringView("`BINARY`")) {
        printf("   [!] BUG DETECTED: code_bt collation mismatch.\n");
    }
    int idx_tbr = args.column_index(SqliteStringView("tag_br"));
    if (idx_tbr < 0 || args.column_at(idx_tbr).collation() != SqliteStringView("[RTRIM]")) {
        printf("   [!] BUG DETECTED: tag_br collation mismatch.\n");
    }

    // 51. CURRENT_DATE and CURRENT_TIME defaults
    int idx_cd = args.column_index(SqliteStringView("c_date"));
    if (idx_cd < 0 || args.column_at(idx_cd).default_value() != SqliteStringView("CURRENT_DATE")) {
        printf("   [!] BUG DETECTED: c_date DEFAULT mismatch.\n");
    }
    int idx_ct = args.column_index(SqliteStringView("c_time"));
    if (idx_ct < 0 || args.column_at(idx_ct).default_value() != SqliteStringView("CURRENT_TIME")) {
        printf("   [!] BUG DETECTED: c_time DEFAULT mismatch.\n");
    }

    // 52. Mixed quote styles in composite UNIQUE
    bool has_mixed_quote_unique = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_unique() && c.column_count() == 4) {
            if (c.has_column(SqliteStringView("col A")) &&
                c.has_column(SqliteStringView("col B")) &&
                c.has_column(SqliteStringView("col C")) &&
                c.has_column(SqliteStringView("col_d"))) {
                has_mixed_quote_unique = true;
            }
        }
    });
    if (!has_mixed_quote_unique) {
        printf("   [!] BUG DETECTED: Mixed quote UNIQUE constraint failed to parse.\n");
    }

    // 53. Case-insensitive column lookup
    int idx_ci_price = args.column_index(SqliteStringView("PRICE"));
    if (idx_ci_price < 0 || idx_ci_price != args.column_index(SqliteStringView("price"))) {
        printf("   [!] BUG DETECTED: Case-insensitive column_index for 'PRICE' failed.\n");
    }

    // 54. Escaped backtick & bracket identifiers
    int idx_esc_bt = args.column_index(SqliteStringView("my``name"));
    if (idx_esc_bt < 0) {
        printf("   [!] BUG DETECTED: `my``name` column failed.\n");
    }
    int idx_esc_br = args.column_index(SqliteStringView("my]]name"));
    if (idx_esc_br < 0) {
        printf("   [!] BUG DETECTED: [my]]name] column failed.\n");
    }

    // 55. CHECK with spaces in string
    int idx_tag = args.column_index(SqliteStringView("tag"));
    if (idx_tag < 0 || args.column_at(idx_tag).definition() != SqliteStringView("TEXT CHECK (length(tag) > 0 AND tag != ' ')")) {
        printf("   [!] BUG DETECTED: tag column CHECK failed.\n");
    }

    // 56. SQLite 5-rule affinities
    int idx_pt = args.column_index(SqliteStringView("pointing_coord"));
    if (idx_pt < 0 || args.column_at(idx_pt).affinity() != SqliteVTabColAffinity::Integer) {
        printf("   [!] BUG DETECTED: POINT affinity mismatch (Rule 1).\n");
    }
    int idx_fl = args.column_index(SqliteStringView("floating_val"));
    if (idx_fl < 0 || args.column_at(idx_fl).affinity() != SqliteVTabColAffinity::Real) {
        printf("   [!] BUG DETECTED: FLOAT affinity mismatch (Rule 3).\n");
    }
    int idx_bl = args.column_index(SqliteStringView("blob_field"));
    if (idx_bl < 0 || args.column_at(idx_bl).affinity() != SqliteVTabColAffinity::Blob) {
        printf("   [!] BUG DETECTED: BLOB_DATA affinity mismatch (Rule 4).\n");
    }

    // 57. Integer boolean params
    if (args.get_bool(SqliteStringView("flag_int_0"), true) != false) {
        printf("   [!] BUG DETECTED: flag_int_0 failed.\n");
    }
    if (args.get_bool(SqliteStringView("flag_int_1"), false) != true) {
        printf("   [!] BUG DETECTED: flag_int_1 failed.\n");
    }
    if (args.get_bool(SqliteStringView("flag_int_neg"), false) != true) {
        printf("   [!] BUG DETECTED: flag_int_neg failed.\n");
    }

    // 58. Preserved spaces in string params
    if (args.get_str(SqliteStringView("query_filter")) != SqliteStringView("'  SELECT * FROM test  '")) {
        printf("   [!] BUG DETECTED: query_filter spaces not preserved.\n");
    }

    // 59. Multi-column FK
    bool has_multi_fk = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_foreign_key() && c.column_count() == 2) {
            if (c.has_column(SqliteStringView("fk_a")) && c.has_column(SqliteStringView("fk_b"))) {
                has_multi_fk = true;
            }
        }
    });
    if (!has_multi_fk) {
        printf("   [!] BUG DETECTED: Multi-column FK failed.\n");
    }

    // 60. Mixed collation & quotes in PK
    bool has_mixed_coll_pk = false;
    args.for_each_constraint([&](const SqliteVTabConstraint& c) {
        if (c.is_primary_key() && c.has_column(SqliteStringView("user_code")) && c.has_column(SqliteStringView("org_id"))) {
            has_mixed_coll_pk = true;
        }
    });
    if (!has_mixed_coll_pk) {
        printf("   [!] BUG DETECTED: PRIMARY KEY ([user_code] COLLATE NOCASE ASC, `org_id` DESC) failed.\n");
    }

    // 61. C-style comment in column
    int idx_commented = args.column_index(SqliteStringView("commented_col"));
    if (idx_commented < 0 || !args.column_at(idx_commented).not_null() || args.column_at(idx_commented).affinity() != SqliteVTabColAffinity::Integer) {
        printf("   [!] BUG DETECTED: commented_col failed.\n");
    }

    // 62. All lowercase keywords
    int idx_lower = args.column_index(SqliteStringView("lower_col"));
    if (idx_lower < 0) {
        printf("   [!] BUG DETECTED: lower_col not found.\n");
    } else {
        assert(args.column_at(idx_lower).primary_key());
        assert(args.column_at(idx_lower).not_null());
        assert((args.column_at(idx_lower).flags() & ColFlag_Unique) != 0);
        assert((args.column_at(idx_lower).flags() & ColFlag_AutoIncr) != 0);
    }

    printf("   [-] Additional boundary cases tested (see output for bugs).\n");
}

// ============================================================================
// 10. Extended Features: Unquoted Strings, Metadata, DDL Synthesis & Schema Validation
// ============================================================================
static void test_vtab_args_metadata_unquoting_ddl_and_validation() {
    printf("10. Testing extended features (metadata, unquoting, DDL synthesis, schema validation)...\n");

    // --- 1. Metadata accessors & user_argc / operator[] ---
    {
        const char* meta_argv[] = {
            "my_module", "main", "users_vtab",
            "id INT PRIMARY KEY",
            "name TEXT",
            "capacity=512"
        };
        SqliteVTabArgs args(6, meta_argv, 3);
        assert(args.argc() == 6);
        assert(args.user_argc() == 3);
        assert(args.module_name() == SqliteStringView("my_module"));
        assert(args.db_name() == SqliteStringView("main"));
        assert(args.table_name() == SqliteStringView("users_vtab"));

        assert(args[0].is_column());
        assert(args[0].column().name() == SqliteStringView("id"));
        assert(args[0].column().unquoted_name() == SqliteStringView("id"));
        assert(args[1].is_column());
        assert(args[1].column().name() == SqliteStringView("name"));
        assert(args[2].is_param());
        assert(args[2].param().key() == SqliteStringView("capacity"));

        // Direct column search & presence
        assert(args.has_column(SqliteStringView("id")));
        assert(args.has_column(SqliteStringView("name")));
        assert(!args.has_column(SqliteStringView("non_col")));
        assert(args.find_column(SqliteStringView("id")).name() == SqliteStringView("id"));
        assert(args.find_column(SqliteStringView("name")).name() == SqliteStringView("name"));
        assert(args.find_column(SqliteStringView("non_col")).name().empty());

        // Primary key presence
        assert(args.has_primary_key() == true);

        // find_param direct lookup
        assert(args.find_param(SqliteStringView("capacity")).value() == SqliteStringView("512"));
        assert(args.find_param(SqliteStringView("non_param")).key().empty());

        // Out of bounds operator[] returns empty arg
        assert(args[3].is_empty());
        assert(args[3].kind() == SqliteVTabArg::Kind::Empty);
        assert(args[-1].is_empty());
        assert(args[-1].kind() == SqliteVTabArg::Kind::Empty);

        // Short / empty argv safety
        SqliteVTabArgs empty_args(0, nullptr, 3);
        assert(empty_args.argc() == 0);
        assert(empty_args.user_argc() == 0);
        assert(!empty_args.has_primary_key());
        assert(!empty_args.has_column(SqliteStringView("id")));
        assert(empty_args.module_name().empty());
        assert(empty_args.db_name().empty());
        assert(empty_args.table_name().empty());

        const char* short_argv[] = { "only_mod" };
        SqliteVTabArgs short_args(1, short_argv, 3);
        assert(short_args.module_name() == SqliteStringView("only_mod"));
        assert(short_args.db_name().empty());
        assert(short_args.table_name().empty());
    }

    // --- 1b. Quoted column unquoted_name() verification ---
    {
        SqliteVTabColumn col_quoted(SqliteStringView("[User Identifier] INT NOT NULL"));
        assert(col_quoted.name() == SqliteStringView("[User Identifier]"));
        assert(col_quoted.unquoted_name() == SqliteStringView("User Identifier"));

        SqliteVTabColumn col_dq(SqliteStringView("\"device_code\" TEXT"));
        assert(col_dq.name() == SqliteStringView("\"device_code\""));
        assert(col_dq.unquoted_name() == SqliteStringView("device_code"));
    }

    // --- 2. Unquoted string accessors & quote stripping ---
    {
        const char* unquote_argv[] = {
            "mod", "db", "tbl",
            "single_q = 'hello world'",
            "double_q = \"quoted \\\"value\\\"\"",
            "backtick = `backticked`",
            "bracket = [bracketed]",
            "empty_sq = ''",
            "empty_dq = \"\"",
            "raw_val = plain_text",
            "\"quoted_key\" = 'some_data'",
            "`CASE_INSENSITIVE_KEY` = 'val123'"
        };
        SqliteVTabArgs args(12, unquote_argv, 3);

        // get_str vs get_unquoted_str
        assert(args.get_str(SqliteStringView("single_q")) == SqliteStringView("'hello world'"));
        assert(args.get_unquoted_str(SqliteStringView("single_q")) == SqliteStringView("hello world"));

        assert(args.get_unquoted_str(SqliteStringView("backtick")) == SqliteStringView("backticked"));
        assert(args.get_unquoted_str(SqliteStringView("bracket")) == SqliteStringView("bracketed"));
        assert(args.get_unquoted_str(SqliteStringView("empty_sq")) == SqliteStringView(""));
        assert(args.get_unquoted_str(SqliteStringView("empty_dq")) == SqliteStringView(""));
        assert(args.get_unquoted_str(SqliteStringView("raw_val")) == SqliteStringView("plain_text"));

        // Case-insensitivity & quote stripping in key lookup
        assert(args.has(SqliteStringView("quoted_key")));
        assert(args.has(SqliteStringView("\"quoted_key\"")));
        assert(args.get_unquoted_str(SqliteStringView("quoted_key")) == SqliteStringView("some_data"));

        assert(args.has(SqliteStringView("case_insensitive_key")));
        assert(args.get_unquoted_str(SqliteStringView("case_insensitive_key")) == SqliteStringView("val123"));

        // Default value for missing key
        assert(args.get_unquoted_str(SqliteStringView("non_existent"), SqliteStringView("default")) == SqliteStringView("default"));
    }

    // --- 3. is_without_rowid detection ---
    {
        const char* norowid_argv1[] = {
            "mod", "db", "t",
            "id INT PRIMARY KEY",
            "WITHOUT ROWID"
        };
        SqliteVTabArgs args1(5, norowid_argv1, 3);
        assert(args1.is_without_rowid() == true);
        assert(SqliteVTabArg(norowid_argv1[4]).is_without_rowid() == true);
        assert(SqliteVTabArg(norowid_argv1[4]).is_option() == true);

        const char* norowid_argv2[] = {
            "mod", "db", "t",
            "id INT PRIMARY KEY",
            "  without \t\n rowid  "
        };
        SqliteVTabArgs args2(5, norowid_argv2, 3);
        assert(args2.is_without_rowid() == true);
        assert(SqliteVTabArg(norowid_argv2[4]).is_without_rowid() == true);

        const char* standard_argv[] = {
            "mod", "db", "t",
            "id INT PRIMARY KEY",
            "capacity=1024"
        };
        SqliteVTabArgs args3(5, standard_argv, 3);
        assert(args3.is_without_rowid() == false);
        assert(SqliteVTabArg(standard_argv[4]).is_without_rowid() == false);
    }

    // --- 4. format_declare_vtab_sql DDL synthesis ---
    {
        const char* ddl_argv[] = {
            "mod", "main", "my_custom_table",
            "id INTEGER PRIMARY KEY",
            "name TEXT NOT NULL",
            "capacity = 2048",
            "mode = 'strict'",
            "score REAL DEFAULT 0.0",
            "CONSTRAINT uq_name UNIQUE (name)",
            "FOREIGN KEY (id) REFERENCES parent(id)",
            "WITHOUT ROWID"
        };
        SqliteVTabArgs args(11, ddl_argv, 3);

        char ddl_buf[512] = {0};
        int len = args.format_declare_vtab_sql(ddl_buf, sizeof(ddl_buf));
        assert(len > 0);

        const char* expected_ddl =
            "CREATE TABLE my_custom_table("
            "id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, "
            "score REAL DEFAULT 0.0, "
            "CONSTRAINT uq_name UNIQUE (name), "
            "FOREIGN KEY (id) REFERENCES parent(id)"
            ") WITHOUT ROWID";

        assert(SqliteStringView(ddl_buf) == SqliteStringView(expected_ddl));

        // Custom table name override
        char custom_ddl_buf[512] = {0};
        args.format_declare_vtab_sql(custom_ddl_buf, sizeof(custom_ddl_buf), SqliteStringView("override_tbl"));
        const char* expected_custom =
            "CREATE TABLE override_tbl("
            "id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, "
            "score REAL DEFAULT 0.0, "
            "CONSTRAINT uq_name UNIQUE (name), "
            "FOREIGN KEY (id) REFERENCES parent(id)"
            ") WITHOUT ROWID";
        assert(SqliteStringView(custom_ddl_buf) == SqliteStringView(expected_custom));

        // Extra hidden columns for table-valued functions / custom TVFs
        const char* extra_hidden[] = {
            "query TEXT HIDDEN",
            "top_k INT HIDDEN"
        };
        char tvf_ddl_buf[512] = {0};
        args.format_declare_vtab_sql(tvf_ddl_buf, sizeof(tvf_ddl_buf), SqliteStringView("my_tvf"), extra_hidden, 2);
        const char* expected_tvf =
            "CREATE TABLE my_tvf("
            "id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, "
            "score REAL DEFAULT 0.0, "
            "query TEXT HIDDEN, "
            "top_k INT HIDDEN, "
            "CONSTRAINT uq_name UNIQUE (name), "
            "FOREIGN KEY (id) REFERENCES parent(id)"
            ") WITHOUT ROWID";
        assert(SqliteStringView(tvf_ddl_buf) == SqliteStringView(expected_tvf));

        // Schema containing user-declared HIDDEN columns in argv
        const char* hidden_argv[] = {
            "mod", "main", "tbl_hidden",
            "doc_id INT PRIMARY KEY",
            "body TEXT",
            "match_query TEXT HIDDEN",
            "score REAL HIDDEN",
            "capacity = 100"
        };
        SqliteVTabArgs hidden_args(8, hidden_argv, 3);
        char hidden_ddl[256] = {0};
        hidden_args.format_declare_vtab_sql(hidden_ddl, sizeof(hidden_ddl));
        const char* expected_hidden_ddl =
            "CREATE TABLE tbl_hidden("
            "doc_id INT PRIMARY KEY, "
            "body TEXT, "
            "match_query TEXT HIDDEN, "
            "score REAL HIDDEN"
            ")";
        assert(SqliteStringView(hidden_ddl) == SqliteStringView(expected_hidden_ddl));

        // Truncation safety with small buffer
        char tiny_buf[16] = {0};
        int full_len = args.format_declare_vtab_sql(tiny_buf, sizeof(tiny_buf));
        assert(full_len == static_cast<int>(SqliteStringUtil::sqlite_strlen(expected_ddl)));
        assert(SqliteStringUtil::sqlite_strlen(tiny_buf) == 15); // Properly null terminated at buffer limit
    }

    // --- 5. SqliteVTabParamSchema: validation, unknown parameters, enum matching ---
    {
        const char* const mode_options[] = { "fast", "strict", "lenient" };

        int cap = 0;
        int mode_idx = -1;
        SqliteStringView db_path;
        bool is_debug = false;

        SqliteVTabParamSchema schema;
        schema.bind_int(SqliteStringView("capacity"), &cap)
              .bind_enum(SqliteStringView("mode"), mode_options, 3, &mode_idx)
              .bind_str(SqliteStringView("db_path"), &db_path)
              .bind_bool(SqliteStringView("debug"), &is_debug);

        assert(schema.binding_count() == 4);
        assert(schema.has_binding(SqliteStringView("capacity")));
        assert(schema.has_binding(SqliteStringView("\"CAPACITY\"")));
        assert(schema.has_binding(SqliteStringView("[MODE]")));
        assert(!schema.has_binding(SqliteStringView("unknown_opt")));

        // Valid arguments with quoted values and mixed case keys
        const char* valid_argv[] = {
            "mod", "db", "t",
            "col1 INT",
            "\"CAPACITY\" = 4096",
            "mode = 'STRICT'",
            "db_path = '/var/data.db'",
            "DEBUG = yes"
        };
        SqliteVTabArgs valid_args(8, valid_argv, 3);

        SqliteStringView first_unknown;
        int unknown_count = schema.validate(valid_args, &first_unknown);
        assert(unknown_count == 0);
        assert(first_unknown.empty());

        int parsed = schema.parse(valid_args);
        assert(parsed == 4);
        assert(cap == 4096);
        assert(mode_idx == 1); // "STRICT" matches index 1 even though argv was quoted 'STRICT'
        assert(db_path == SqliteStringView("'/var/data.db'"));
        assert(is_debug == true);

        // Arguments with unrecognized/typo parameter
        const char* invalid_argv[] = {
            "mod", "db", "t",
            "col1 INT",
            "capacity = 1024",
            "invalid_option = 99",
            "another_bad = 'foo'"
        };
        SqliteVTabArgs invalid_args(7, invalid_argv, 3);

        int bad_count = schema.validate(invalid_args, &first_unknown);
        assert(bad_count == 2);
        assert(first_unknown == SqliteStringView("invalid_option"));

        int collected_unknowns = 0;
        schema.for_each_unknown(invalid_args, [&](const SqliteVTabParam& p) {
            if (collected_unknowns == 0) {
                assert(p.key() == SqliteStringView("invalid_option"));
                assert(p.value() == SqliteStringView("99"));
            } else if (collected_unknowns == 1) {
                assert(p.key() == SqliteStringView("another_bad"));
            }
            ++collected_unknowns;
        });
        assert(collected_unknowns == 2);
    }

    printf("   [PASS] Extended features (metadata, unquoting, DDL synthesis, schema validation) verified.\n");
}

// ============================================================================
// 11. Complex Data Types, Column Flags, and Quote Variations
// ============================================================================
static void test_vtab_complex_datatypes_and_flags() {
    printf("11. Testing complex data types, column flags, and quote variations...\n");

    // All affinities under diverse SQL definitions
    struct AffTestCase {
        const char* def;
        SqliteVTabColAffinity expected_aff;
        const char* expected_type;
    };

    const AffTestCase aff_cases[] = {
        { "c1 VARCHAR(255)", SqliteVTabColAffinity::Text, "VARCHAR(255)" },
        { "c2 NVARCHAR(50)", SqliteVTabColAffinity::Text, "NVARCHAR(50)" },
        { "c3 CHARACTER VARYING(100)", SqliteVTabColAffinity::Text, "CHARACTER VARYING(100)" },
        { "c4 CHAR(10)", SqliteVTabColAffinity::Text, "CHAR(10)" },
        { "c5 CLOB", SqliteVTabColAffinity::Text, "CLOB" },
        { "c6 TEXT", SqliteVTabColAffinity::Text, "TEXT" },

        { "c7 INT", SqliteVTabColAffinity::Integer, "INT" },
        { "c8 INTEGER", SqliteVTabColAffinity::Integer, "INTEGER" },
        { "c9 BIGINT", SqliteVTabColAffinity::Integer, "BIGINT" },
        { "c10 TINYINT", SqliteVTabColAffinity::Integer, "TINYINT" },
        { "c11 SMALLINT", SqliteVTabColAffinity::Integer, "SMALLINT" },
        { "c12 UNSIGNED BIG INT", SqliteVTabColAffinity::Integer, "UNSIGNED BIG INT" },
        { "c13 INT2", SqliteVTabColAffinity::Integer, "INT2" },
        { "c14 INT8", SqliteVTabColAffinity::Integer, "INT8" },

        { "c15 REAL", SqliteVTabColAffinity::Real, "REAL" },
        { "c16 DOUBLE", SqliteVTabColAffinity::Real, "DOUBLE" },
        { "c17 DOUBLE PRECISION", SqliteVTabColAffinity::Real, "DOUBLE PRECISION" },
        { "c18 FLOAT", SqliteVTabColAffinity::Real, "FLOAT" },
        { "c19 FLOAT8", SqliteVTabColAffinity::Real, "FLOAT8" },

        { "c20 BLOB", SqliteVTabColAffinity::Blob, "BLOB" },
        { "c21", SqliteVTabColAffinity::Blob, "" },

        { "c22 NUMERIC", SqliteVTabColAffinity::Numeric, "NUMERIC" },
        { "c23 DECIMAL(10, 2)", SqliteVTabColAffinity::Numeric, "DECIMAL(10, 2)" },
        { "c24 BOOLEAN", SqliteVTabColAffinity::Numeric, "BOOLEAN" },
        { "c25 DATETIME", SqliteVTabColAffinity::Numeric, "DATETIME" },
        { "c26 TIMESTAMP", SqliteVTabColAffinity::Numeric, "TIMESTAMP" },
        { "c27 JSON", SqliteVTabColAffinity::Numeric, "JSON" }
    };

    for (const auto& tc : aff_cases) {
        SqliteVTabColumn col(SqliteStringView(tc.def));
        assert(col.affinity() == tc.expected_aff);
        assert(col.data_type() == SqliteStringView(tc.expected_type));
    }

    // Comprehensive column flags combinations
    SqliteVTabColumn c_combo(SqliteStringView("id INT PRIMARY KEY AUTOINCREMENT NOT NULL UNIQUE"));
    assert(c_combo.name() == SqliteStringView("id"));
    assert(c_combo.unquoted_name() == SqliteStringView("id"));
    assert(c_combo.primary_key());
    assert(c_combo.is_autoincrement());
    assert(c_combo.not_null());
    assert(c_combo.is_unique());
    assert(!c_combo.is_hidden());
    assert(c_combo.data_type() == SqliteStringView("INT"));

    SqliteVTabColumn c_hidden(SqliteStringView("tag TEXT HIDDEN NOT NULL DEFAULT 'n/a' COLLATE NOCASE"));
    assert(c_hidden.name() == SqliteStringView("tag"));
    assert(c_hidden.is_hidden());
    assert(c_hidden.not_null());
    assert(c_hidden.has_default());
    assert(c_hidden.default_value() == SqliteStringView("'n/a'"));
    assert(c_hidden.collation() == SqliteStringView("NOCASE"));
    assert(!c_hidden.primary_key());
    assert(!c_hidden.is_unique());

    // Quote unquoting across column styles
    SqliteVTabColumn q1(SqliteStringView("[user id] BIGINT"));
    assert(q1.name() == SqliteStringView("[user id]"));
    assert(q1.unquoted_name() == SqliteStringView("user id"));

    SqliteVTabColumn q2(SqliteStringView("`email_addr` VARCHAR(100)"));
    assert(q2.name() == SqliteStringView("`email_addr`"));
    assert(q2.unquoted_name() == SqliteStringView("email_addr"));

    SqliteVTabColumn q3(SqliteStringView("\"total count\" INT DEFAULT 0"));
    assert(q3.name() == SqliteStringView("\"total count\""));
    assert(q3.unquoted_name() == SqliteStringView("total count"));

    SqliteVTabColumn q4(SqliteStringView("'first name' TEXT"));
    assert(q4.name() == SqliteStringView("'first name'"));
    assert(q4.unquoted_name() == SqliteStringView("first name"));

    printf("   [PASS] Complex data types and column flags verified.\n");
}

// ============================================================================
// 12. Deep Table Constraints, Quoted Columns, and Multi-PK Indexing
// ============================================================================
static void test_vtab_composite_constraints_and_indexed_pks() {
    printf("12. Testing deep table constraints, quoted columns, and indexed PKs...\n");

    // Named composite constraint with quoted column names
    SqliteVTabConstraint pk_quoted(SqliteStringView("CONSTRAINT \"pk_complex\" PRIMARY KEY ([user id], \"device.id\", `timestamp`)"));
    assert(pk_quoted.is_primary_key());
    assert(pk_quoted.has_name());
    assert(pk_quoted.name() == SqliteStringView("pk_complex"));
    assert(pk_quoted.column_count() == 3);
    assert(pk_quoted.has_column(SqliteStringView("user id")));
    assert(pk_quoted.has_column(SqliteStringView("device.id")));
    assert(pk_quoted.has_column(SqliteStringView("timestamp")));

    int col_i = 0;
    pk_quoted.for_each_column_name([&](SqliteStringView col_name) {
        if (col_i == 0) assert(col_name == SqliteStringView("user id"));
        if (col_i == 1) assert(col_name == SqliteStringView("device.id"));
        if (col_i == 2) assert(col_name == SqliteStringView("timestamp"));
        ++col_i;
    });
    assert(col_i == 3);

    // Named UNIQUE constraint
    SqliteVTabConstraint uq_named(SqliteStringView("CONSTRAINT uq_session UNIQUE (user_id, session_token)"));
    assert(uq_named.is_unique());
    assert(uq_named.has_name());
    assert(uq_named.name() == SqliteStringView("uq_session"));
    assert(uq_named.column_count() == 2);
    assert(uq_named.has_column(SqliteStringView("user_id")));
    assert(uq_named.has_column(SqliteStringView("session_token")));

    // CHECK constraint with nested logic
    SqliteVTabConstraint ck_comp(SqliteStringView("CONSTRAINT chk_score CHECK (score >= 0.0 AND (score <= 100.0 OR score IS NULL))"));
    assert(ck_comp.is_check());
    assert(ck_comp.has_name());
    assert(ck_comp.name() == SqliteStringView("chk_score"));

    // FOREIGN KEY with clauses
    SqliteVTabConstraint fk_full(SqliteStringView("FOREIGN KEY (tenant_id, user_id) REFERENCES tenants(id, uid) ON DELETE CASCADE"));
    assert(fk_full.is_foreign_key());
    assert(!fk_full.has_name());
    assert(fk_full.has_column(SqliteStringView("tenant_id")));
    assert(fk_full.has_column(SqliteStringView("user_id")));

    // Batch args with multiple columns and multi-PK indexing
    const char* full_argv[] = {
        "mod", "main", "devices",
        "org_id INT",
        "dept_id INT",
        "device_uuid TEXT",
        "ip_addr TEXT",
        "CONSTRAINT pk_dev PRIMARY KEY (org_id, dept_id, device_uuid)"
    };
    SqliteVTabArgs vargs(8, full_argv, 3);
    assert(vargs.column_count() == 4);
    assert(vargs.primary_key_count() == 3);
    assert(vargs.is_composite_primary_key());
    assert(vargs.has_primary_key());
    assert(vargs.is_primary_key_column(SqliteStringView("org_id")));
    assert(vargs.is_primary_key_column(SqliteStringView("dept_id")));
    assert(vargs.is_primary_key_column(SqliteStringView("device_uuid")));
    assert(!vargs.is_primary_key_column(SqliteStringView("ip_addr")));

    int pk_indexed_count = 0;
    vargs.for_each_primary_key_indexed([&](SqliteStringView pk_col, int idx) {
        if (pk_indexed_count == 0) {
            assert(pk_col == SqliteStringView("org_id"));
            assert(idx == 0);
        } else if (pk_indexed_count == 1) {
            assert(pk_col == SqliteStringView("dept_id"));
            assert(idx == 1);
        } else if (pk_indexed_count == 2) {
            assert(pk_col == SqliteStringView("device_uuid"));
            assert(idx == 2);
        }
        ++pk_indexed_count;
    });
    assert(pk_indexed_count == 3);

    printf("   [PASS] Deep table constraints and indexed PKs verified.\n");
}

// ============================================================================
// 13. TVF (Table-Valued Function) End-to-End SQL Execution with Hidden Columns
// ============================================================================
class SearchTVFTab;

class SearchTVFCursor : public SqliteVTabCursor {
public:
    int m_rowid;
    SqliteStringView m_query;
    int m_limit;
    SearchTVFTab* m_tab;

    SearchTVFCursor(SearchTVFTab* tab) : m_rowid(0), m_limit(5), m_tab(tab) {}

    int filter(int idxNum, const char* idxStr, SqliteUdfArgs argv) override {
        (void)idxNum; (void)idxStr;
        m_rowid = 1;
        if (argv.size() > 0 && argv[0].is_text()) {
            m_query = argv[0].as_text();
        }
        if (argv.size() > 1 && argv[1].is_integer()) {
            m_limit = argv[1].as_int();
        }
        return SQLITE_OK;
    }

    int next() override {
        ++m_rowid;
        return SQLITE_OK;
    }

    bool eof() override {
        return m_rowid > m_limit;
    }

    int column(SqliteContext& ctx, int i) override;

    int rowid(sqlite3_int64& pRowid) override {
        pRowid = m_rowid;
        return SQLITE_OK;
    }
};

class SearchTVFTab : public SqliteVTable {
public:
    SearchTVFTab(sqlite3* db) : SqliteVTable(db) {}

    static int connect(SqliteConnectArgs& args) {
        SqliteVTabArgs vargs(args);

        // Extra hidden columns for TVF arguments:
        const char* tvf_hidden_cols[] = {
            "query TEXT HIDDEN",
            "max_results INT HIDDEN"
        };

        char ddl[512] = {0};
        vargs.format_declare_vtab_sql(ddl, sizeof(ddl), SqliteStringView(""), tvf_hidden_cols, 2);

        int rc = sqlite3_declare_vtab(args.db(), ddl);
        if (rc != SQLITE_OK) return rc;

        args.set_instance(sqlite_new<SearchTVFTab>(args.db()));
        return SQLITE_OK;
    }

    static int create(SqliteConnectArgs& args) {
        return connect(args);
    }

    int bestIndex(SqliteIndexInfo& info) override {
        int argv_idx = 1;
        for (int i = 0; i < info.num_constraints(); ++i) {
            const auto& c = info.constraint(i);
            if (c.usable && c.op == SQLITE_INDEX_CONSTRAINT_EQ) {
                int col = c.iColumn;
                if (col == 2 || col == 3) { // query or max_results hidden columns
                    info.usage(i).argvIndex = argv_idx++;
                    info.usage(i).omit = 1;
                }
            }
        }
        info.set_estimated_cost(1.0);
        return SQLITE_OK;
    }

    SqliteVTabCursor* open() override {
        return sqlite_new<SearchTVFCursor>(this);
    }
};

inline int SearchTVFCursor::column(SqliteContext& ctx, int i) {
    if (i == 0) ctx.result_int(m_rowid);
    else if (i == 1) ctx.result_text(SqliteStringView("result_item"));
    else if (i == 2) ctx.result_text(m_query);
    else if (i == 3) ctx.result_int(m_limit);
    return SQLITE_OK;
}

static void test_vtab_tvf_and_ddl_synthesis_end_to_end() {
    printf("13. Testing TVF end-to-end SQL execution with synthesized hidden columns...\n");

    sqlite3* db = nullptr;
    int rc = sqlite3_open(":memory:", &db);
    assert(rc == SQLITE_OK && db);

    rc = SqliteVTab::define<SearchTVFTab>(db, "search_tvf");
    assert(rc == SQLITE_OK);

    // Create table with output columns id and title
    rc = sqlite3_exec(db, "CREATE VIRTUAL TABLE t_search USING search_tvf(id INT, title TEXT);", nullptr, nullptr, nullptr);
    assert(rc == SQLITE_OK);

    // Execute TVF table-valued function query
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, "SELECT id, title, query, max_results FROM t_search('sqlite3', 3);", -1, &stmt, nullptr);
    assert(rc == SQLITE_OK && stmt);

    int rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* title = (const char*)sqlite3_column_text(stmt, 1);
        const char* q = (const char*)sqlite3_column_text(stmt, 2);
        int max_r = sqlite3_column_int(stmt, 3);

        assert(id == rows + 1);
        assert(strcmp(title, "result_item") == 0);
        assert(strcmp(q, "sqlite3") == 0);
        assert(max_r == 3);
        ++rows;
    }
    assert(rows == 3);
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    printf("   [PASS] TVF end-to-end SQL execution verified.\n");
}

// ============================================================================
// 14. Direct Enum, Scalar Helpers, Rowid Alias, and Generated Columns
// ============================================================================
static void test_vtab_direct_enum_scalars_rowid_and_generated_cols() {
    printf("14. Testing direct enum, scalar helpers, rowid alias, hidden & generated columns...\n");

    // 1. Direct enum matching & scalar accessors on SqliteVTabParam
    const char* const kTransports[] = { "tcp", "udp", "quic", "unix" };
    SqliteVTabParam p_proto(SqliteStringView("proto"), SqliteStringView("'QUIC'"));
    assert(p_proto.as_enum(kTransports, 4) == 2);
    assert(p_proto.as_enum(kTransports, 4, -1) == 2);

    SqliteVTabParam p_bad(SqliteStringView("proto"), SqliteStringView("http"));
    assert(p_bad.as_enum(kTransports, 4, -1) == -1);

    SqliteVTabParam p_float(SqliteStringView("rate"), SqliteStringView("2.718"));
    float f_val = 0.0f;
    assert(p_float.as_float(f_val));
    assert(f_val > 2.71f && f_val < 2.72f);

    SqliteVTabParam p_uint(SqliteStringView("mask"), SqliteStringView("4294967295"));
    unsigned int u_val = 0;
    assert(p_uint.as_uint(u_val));
    assert(u_val == 4294967295U);

    // 2. SqliteVTabArgs direct getters & schema binding
    const char* config_argv[] = {
        "mod", "main", "net_tab",
        "proto = 'UDP'",
        "sample_rate = 1.5",
        "port = 8080"
    };
    SqliteVTabArgs config_args(6, config_argv, 3);
    assert(config_args.get_enum(SqliteStringView("proto"), kTransports, 4) == 1); // "udp"
    assert(config_args.get_float(SqliteStringView("sample_rate")) == 1.5f);
    assert(config_args.get_uint(SqliteStringView("port")) == 8080U);

    float schema_rate = 0.0f;
    unsigned int schema_port = 0;
    SqliteVTabParamSchema schema;
    schema.bind_float(SqliteStringView("sample_rate"), &schema_rate)
          .bind_uint(SqliteStringView("port"), &schema_port);
    assert(schema.parse(config_args) == 2);
    assert(schema_rate == 1.5f);
    assert(schema_port == 8080U);

    // 3. Rowid Alias & Hidden Columns
    const char* rowid_tab_argv[] = {
        "mod", "main", "users",
        "id INTEGER PRIMARY KEY",
        "name TEXT",
        "secret_token TEXT HIDDEN",
        "audit_log BLOB HIDDEN"
    };
    SqliteVTabArgs rowid_args(7, rowid_tab_argv, 3);
    assert(rowid_args.has_hidden_columns());
    assert(rowid_args.hidden_column_count() == 2);
    assert(rowid_args.rowid_alias_column_index() == 0);
    assert(rowid_args.rowid_alias_column_name() == SqliteStringView("id"));

    int hidden_count = 0;
    rowid_args.for_each_hidden_column([&](const SqliteVTabColumn& col) {
        if (hidden_count == 0) assert(col.name() == SqliteStringView("secret_token"));
        if (hidden_count == 1) assert(col.name() == SqliteStringView("audit_log"));
        ++hidden_count;
    });
    assert(hidden_count == 2);

    // WITHOUT ROWID table has NO rowid alias
    const char* without_rowid_argv[] = {
        "mod", "main", "users_norowid",
        "id INTEGER PRIMARY KEY",
        "name TEXT",
        "WITHOUT ROWID"
    };
    SqliteVTabArgs norowid_args(6, without_rowid_argv, 3);
    assert(norowid_args.is_without_rowid());
    assert(norowid_args.rowid_alias_column_index() == -1);
    assert(norowid_args.rowid_alias_column_name().empty());

    // Non-INTEGER primary key has NO rowid alias
    const char* text_pk_argv[] = {
        "mod", "main", "kv",
        "key TEXT PRIMARY KEY",
        "val BLOB"
    };
    SqliteVTabArgs text_pk_args(5, text_pk_argv, 3);
    assert(text_pk_args.rowid_alias_column_index() == -1);

    // 4. Generated Columns Parsing (SQLite 3.31+)
    SqliteVTabColumn gen1(SqliteStringView("full_name TEXT GENERATED ALWAYS AS (first_name || ' ' || last_name) STORED"));
    assert(gen1.name() == SqliteStringView("full_name"));
    assert(gen1.data_type() == SqliteStringView("TEXT"));
    assert(gen1.affinity() == SqliteVTabColAffinity::Text);
    assert(gen1.is_generated());
    assert(gen1.is_stored());
    assert(!gen1.is_virtual_generated());
    assert(gen1.generated_expression() == SqliteStringView("first_name || ' ' || last_name"));

    SqliteVTabColumn gen2(SqliteStringView("area REAL AS (width * height) VIRTUAL"));
    assert(gen2.name() == SqliteStringView("area"));
    assert(gen2.data_type() == SqliteStringView("REAL"));
    assert(gen2.affinity() == SqliteVTabColAffinity::Real);
    assert(gen2.is_generated());
    assert(gen2.is_virtual_generated());
    assert(!gen2.is_stored());
    assert(gen2.generated_expression() == SqliteStringView("width * height"));

    SqliteVTabColumn non_gen(SqliteStringView("score REAL DEFAULT 0.0"));
    assert(!non_gen.is_generated());
    assert(!non_gen.is_stored());
    assert(!non_gen.is_virtual_generated());
    assert(non_gen.generated_expression().empty());

    printf("   [PASS] Direct enum, scalars, rowid alias, hidden & generated columns verified.\n");
}

// ============================================================================
// 15. Advanced Stress, Negative Numerics, and Complex Constraint Slices
// ============================================================================
static void test_vtab_advanced_stress_and_edge_cases() {
    printf("15. Testing advanced stress, negative numerics, and complex constraint slices...\n");

    // 1. Negative numbers across all numeric types
    SqliteVTabParam p_neg_int(SqliteStringView("k"), SqliteStringView("-42"));
    int neg_i = 0;
    assert(p_neg_int.as_int(neg_i) && neg_i == -42);

    SqliteVTabParam p_neg_long(SqliteStringView("k"), SqliteStringView("-9223372036854775807"));
    long long neg_l = 0;
    assert(p_neg_long.as_long(neg_l) && neg_l == -9223372036854775807LL);

    sqlite3_int64 neg_i64 = 0;
    assert(p_neg_long.as_int64(neg_i64) && neg_i64 == -9223372036854775807LL);

    SqliteVTabParam p_neg_float(SqliteStringView("k"), SqliteStringView("-12.5"));
    float neg_f = 0.0f;
    assert(p_neg_float.as_float(neg_f) && neg_f == -12.5f);

    SqliteVTabParam p_neg_double(SqliteStringView("k"), SqliteStringView("-123.456789"));
    double neg_d = 0.0;
    assert(p_neg_double.as_double(neg_d) && neg_d < -123.45 && neg_d > -123.46);

    // Exponential notation
    SqliteVTabParam p_exp(SqliteStringView("k"), SqliteStringView("2.5e3"));
    double exp_d = 0.0;
    assert(p_exp.as_double(exp_d) && exp_d == 2500.0);

    // 2. Multi-clause column declarations
    SqliteVTabColumn col_full(SqliteStringView("status TEXT COLLATE NOCASE DEFAULT 'draft' NOT NULL PRIMARY KEY AUTOINCREMENT"));
    assert(col_full.name() == SqliteStringView("status"));
    assert(col_full.data_type() == SqliteStringView("TEXT"));
    assert(col_full.affinity() == SqliteVTabColAffinity::Text);
    assert(col_full.not_null());
    assert(col_full.primary_key());
    assert(col_full.is_autoincrement());
    assert(col_full.has_default());
    assert(col_full.default_value() == SqliteStringView("'draft'"));
    assert(col_full.collation() == SqliteStringView("NOCASE"));

    SqliteVTabColumn col_gen_calc(SqliteStringView("result REAL GENERATED ALWAYS AS (sin(radians(angle)) + 1.0) STORED NOT NULL"));
    assert(col_gen_calc.name() == SqliteStringView("result"));
    assert(col_gen_calc.data_type() == SqliteStringView("REAL"));
    assert(col_gen_calc.affinity() == SqliteVTabColAffinity::Real);
    assert(col_gen_calc.is_generated());
    assert(col_gen_calc.is_stored());
    assert(col_gen_calc.not_null());
    assert(col_gen_calc.generated_expression() == SqliteStringView("sin(radians(angle)) + 1.0"));

    // 3. Composite constraint with ASC, DESC, and COLLATE clauses on column names
    SqliteVTabConstraint pk_sorted(SqliteStringView("CONSTRAINT pk_multi PRIMARY KEY (col1 ASC, \"col 2\" DESC, `col3` COLLATE NOCASE ASC)"));
    assert(pk_sorted.is_primary_key());
    assert(pk_sorted.has_name());
    assert(pk_sorted.name() == SqliteStringView("pk_multi"));
    assert(pk_sorted.column_count() == 3);
    assert(pk_sorted.has_column(SqliteStringView("col1")));
    assert(pk_sorted.has_column(SqliteStringView("col 2")));
    assert(pk_sorted.has_column(SqliteStringView("col3")));

    int extracted_cols = 0;
    pk_sorted.for_each_column_name([&](SqliteStringView c) {
        if (extracted_cols == 0) assert(c == SqliteStringView("col1"));
        if (extracted_cols == 1) assert(c == SqliteStringView("col 2"));
        if (extracted_cols == 2) assert(c == SqliteStringView("col3"));
        ++extracted_cols;
    });
    assert(extracted_cols == 3);

    // 4. Custom user_start offset on SqliteVTabArgs (e.g. user_start = 0 for standalone arg arrays)
    const char* standalone_argv[] = {
        "host = 'localhost'",
        "port = 9000",
        "timeout = 30.5"
    };
    SqliteVTabArgs standalone_args(3, standalone_argv, 0);
    assert(standalone_args.argc() == 3);
    assert(standalone_args.user_argc() == 3);
    assert(standalone_args.has(SqliteStringView("host")));
    assert(standalone_args.get_unquoted_str(SqliteStringView("host")) == SqliteStringView("localhost"));
    assert(standalone_args.get_uint(SqliteStringView("port")) == 9000U);
    assert(standalone_args.get_float(SqliteStringView("timeout")) == 30.5f);

    // Out of bounds user argument access
    assert(standalone_args[-1].is_empty());
    assert(standalone_args[3].is_empty());
    assert(!standalone_args[0].is_empty());

    // 5. Empty argv safety
    SqliteVTabArgs empty_args(0, nullptr, 0);
    assert(empty_args.argc() == 0);
    assert(empty_args.user_argc() == 0);
    assert(empty_args.column_count() == 0);
    assert(empty_args.param_count() == 0);
    assert(empty_args.constraint_count() == 0);
    assert(!empty_args.has(SqliteStringView("any")));
    assert(empty_args.get_int(SqliteStringView("any"), 123) == 123);
    assert(empty_args[-1].is_empty());
    assert(empty_args[0].is_empty());

    printf("   [PASS] Advanced stress, negative numerics, and complex constraint slices verified.\n");
}

int main() {
    printf("=================================================================\n");
    printf("Running Virtual Table Argument Parser (sqlite3_vtab_arg.hpp) Tests\n");
    printf("=================================================================\n");

    test_vtab_arg_internal_utilities();
    test_vtab_param_accessors_and_conversions();
    test_vtab_column_parsing_and_affinities();
    test_vtab_constraint_parsing_and_types();
    test_vtab_arg_tagged_union_lifecycle();
    test_vtab_args_batch_extraction_and_lookup();
    test_vtab_args_mixed_order_and_defaults();
    test_vtab_param_schema_binding_and_parsing();
    test_vtab_sql_table_creation_and_querying();
    test_vtab_args_syntax_edge_cases_and_boundaries();
    test_vtab_args_metadata_unquoting_ddl_and_validation();
    test_vtab_complex_datatypes_and_flags();
    test_vtab_composite_constraints_and_indexed_pks();
    test_vtab_tvf_and_ddl_synthesis_end_to_end();
    test_vtab_direct_enum_scalars_rowid_and_generated_cols();
    test_vtab_advanced_stress_and_edge_cases();

    printf("=================================================================\n");
    printf("All Virtual Table Argument Parser Tests Passed Successfully (100%%)!\n");
    printf("=================================================================\n");
    return 0;
}

