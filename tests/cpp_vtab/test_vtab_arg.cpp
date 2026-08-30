#define SQLITE_CORE
#include <sqlite3.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "sqlite3_vtab_arg.hpp"
#include "sqlite3_vtab.hpp"
#include "sqlite3_statement.hpp"

// ============================================================================
// 1. SqliteVTabParam Unit Tests (Accessors, Parsing, Edge Cases)
// ============================================================================
static void test_vtab_param() {
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
static void test_vtab_column() {
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
    assert(c1.definition() == SqliteStringView("INTEGER PRIMARY KEY NOT NULL AUTOINCREMENT"));
    assert(c1.full_def() == SqliteStringView("id INTEGER PRIMARY KEY NOT NULL AUTOINCREMENT"));
    assert(c1.affinity() == SqliteVTabColAffinity::Integer);
    assert(c1.not_null());
    assert(c1.primary_key());
    assert(c1.flags() & ColFlag_AutoIncr);
    assert(!c1.is_hidden());

    SqliteVTabColumn c1b(SqliteStringView("big_count BIGINT"));
    assert(c1b.affinity() == SqliteVTabColAffinity::Integer);

    // Rule 2: TEXT Affinity ("CHAR", "VARCHAR", "CLOB", "TEXT")
    SqliteVTabColumn c2(SqliteStringView("email VARCHAR(255) UNIQUE NOT NULL"));
    assert(c2.name() == SqliteStringView("email"));
    assert(c2.affinity() == SqliteVTabColAffinity::Text);
    assert(c2.not_null());
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

    printf("   [PASS] SqliteVTabColumn verified.\n");
}

// ============================================================================
// 3. SqliteVTabConstraint Unit Tests (Composite PK, Unique, Check, FK)
// ============================================================================
static void test_vtab_constraint() {
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
    assert(pk1_alt.column_count() == 2);

    // Named Primary Key constraint
    SqliteVTabConstraint pk2(SqliteStringView("CONSTRAINT pk_custom PRIMARY KEY (tenant_id, org_id)"));
    assert(pk2.is_primary_key());
    assert(pk2.name() == SqliteStringView("pk_custom"));
    assert(pk2.column_count() == 2);
    assert(pk2.has_column(SqliteStringView("tenant_id")));
    assert(pk2.has_column(SqliteStringView("org_id")));

    // Unique table constraint
    SqliteVTabConstraint uq(SqliteStringView("CONSTRAINT uq_email UNIQUE (domain, username)"));
    assert(uq.is_unique());
    assert(!uq.is_primary_key());
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
static void test_vtab_arg_union_lifecycle() {
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
static void test_vtab_args_batch() {
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
static void test_vtab_args_mixed_order() {
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
static void test_vtab_param_schema_full_coverage() {
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

static void test_vtab_sql_integration() {
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

int main() {
    printf("=================================================================\n");
    printf("Running Virtual Table Argument Parser (sqlite3_vtab_arg.hpp) Tests\n");
    printf("=================================================================\n");

    test_vtab_param();
    test_vtab_column();
    test_vtab_constraint();
    test_vtab_arg_union_lifecycle();
    test_vtab_args_batch();
    test_vtab_args_mixed_order();
    test_vtab_param_schema_full_coverage();
    test_vtab_sql_integration();

    printf("=================================================================\n");
    printf("All Virtual Table Argument Parser Tests Passed Successfully (100%%)!\n");
    printf("=================================================================\n");
    return 0;
}
