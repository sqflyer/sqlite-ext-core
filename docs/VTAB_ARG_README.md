# SQLite Virtual Table Argument Parser (`sqlite3_vtab_arg.hpp`)

A zero-allocation, high-performance C++17 argument parser, schema inspector, validator, and SQL DDL synthesizer for SQLite `CREATE VIRTUAL TABLE` statements.

> **Deep Architecture Reference**: For an exhaustive breakdown of the internal scanning state machines, quote handling, and memory models, see [`docs/VTAB_ARG_ARCHITECTURE.md`](VTAB_ARG_ARCHITECTURE.md).

---

## Table of Contents

1. [Features & Design Goals](#1-features--design-goals)
2. [Virtual Table Argument Lifecycle & Architecture](#2-virtual-table-argument-lifecycle--architecture)
3. [Quickstart Tutorial](#3-quickstart-tutorial)
4. [Exhaustive API Reference](#4-exhaustive-api-reference)
   - [4.1 `SqliteVTabParam` (Key=Value View)](#41-sqlitevtabparam-keyvalue-view)
   - [4.2 `SqliteVTabColAffinity` & `SqliteVTabColFlags`](#42-sqlitevtabcolaffinity--sqlitevtabcolflags)
   - [4.3 `SqliteVTabColumn` (Column Definition View)](#43-sqlitevtabcolumn-column-definition-view)
   - [4.4 `SqliteVTabConstraint` (Table Constraint View)](#44-sqlitevtabconstraint-table-constraint-view)
   - [4.5 `SqliteVTabArg` (Tagged Union)](#45-sqlitevtabarg-tagged-union)
   - [4.6 `SqliteVTabArgs` (Batch Parser & Table Aggregator)](#46-sqlitevtabargs-batch-parser--table-aggregator)
   - [4.7 `SqliteVTabParamSchema` (Declarative Schema Builder)](#47-sqlitevtabparamschema-declarative-schema-builder)
5. [Complete Case Coverage & Practical Recipes](#5-complete-case-coverage--practical-recipes)
   - [Case 1: Table-Valued Functions (TVF) with Programmatic Hidden Columns](#case-1-table-valued-functions-tvf-with-programmatic-hidden-columns)
   - [Case 2: Composite Primary Keys & `WITHOUT ROWID` Tables](#case-2-composite-primary-keys--without-rowid-tables)
   - [Case 3: Strict Schema Validation & Error Reporting](#case-3-strict-schema-validation--error-reporting)
   - [Case 4: Generated Column Parsing (`STORED` vs `VIRTUAL` vs Shorthand)](#case-4-generated-column-parsing-stored-vs-virtual-vs-shorthand)
   - [Case 5: Integer RowID Alias Detection & Constraints](#case-5-integer-rowid-alias-detection--constraints)
   - [Case 6: Default Values & Collation Sequences](#case-6-default-values--collation-sequences)
   - [Case 7: Mixed-Order Arguments (Top, Middle, Bottom)](#case-7-mixed-order-arguments-top-middle-bottom)
   - [Case 8: Named & Complex Table Constraints (CHECK, UNIQUE, FK)](#case-8-named--complex-table-constraints-check-unique-fk)
   - [Case 9: Standalone Slicing for Config, CLI, & Sub-Modules (`user_start = 0`)](#case-9-standalone-slicing-for-config-cli--sub-modules-user_start--0)
   - [Case 10: Dynamic Literal Type Inference via `SqliteValueOwned`](#case-10-dynamic-literal-type-inference-via-sqlitevalueowned)
   - [Case 11: Negative Numbers, Hex, Exponents, and Edge Numerics](#case-11-negative-numbers-hex-exponents-and-edge-numerics)
   - [Case 12: Direct Enum Matching with Fallbacks](#case-12-direct-enum-matching-with-fallbacks)
6. [Quote Normalization & Syntax Resilience](#6-quote-normalization--syntax-resilience)
7. [Memory Model & Lifetime Invariants](#7-memory-model--lifetime-invariants)
8. [Compiler Support & Verification](#8-compiler-support--verification)
9. [Troubleshooting & Frequently Asked Questions](#9-troubleshooting--frequently-asked-questions)
10. [Test Suite Coverage & Verification Matrix](#10-test-suite-coverage--verification-matrix)

---

## 1. Features & Design Goals

| Feature | Technical Specification |
| :--- | :--- |
| **100% Zero Heap Allocations** | Zero dynamic heap calls (`malloc`, `new`, `std::vector`, `std::string`). All string views (`SqliteStringView`) reference SQLite-owned `argv` memory directly. |
| **Freestanding & `-nostdlib++`** | 100% header-only implementation with zero runtime C++ standard library dependencies, suitable for embedded systems and strict kernel modules. |
| **Quote Resilience** | Transparently unwraps matching quotes across all 4 SQLite quote dialects (`""`, `''`, ```` ` ````, `[]`) while preserving nested escaped quotes (e.g. `''` or `""`). |
| **Official SQLite Type Affinities** | Implements the official 5-rule SQLite type affinity determination (`Integer`, `Text`, `Blob`, `Real`, `Numeric`). |
| **Multi-PK Aggregator** | Seamlessly aggregates inline primary keys (`id INT PRIMARY KEY`) with table-level composite constraints (`PRIMARY KEY (tenant_id, device_id)`). |
| **RowID Alias Detection** | Distinguishes between standard primary keys and 64-bit signed integer rowid aliases (`INTEGER PRIMARY KEY`). |
| **Generated Column Parsing** | Recognizes SQLite 3.31+ generated column definitions (`GENERATED ALWAYS AS (expr) STORED/VIRTUAL` or `AS (expr)`) and isolates expressions. |
| **Hidden Column Identification** | Detects virtual table parameter columns marked with `HIDDEN` and supports filtered iteration. |
| **Declarative `SqliteVTabParamSchema`** | Fluent bounded builder (`MAX_PARAMS = 16`) providing single-pass parameter parsing, enum string coercion, and unknown key error detection. |
| **SQL DDL Synthesis** | `format_declare_vtab_sql()` synthesizes valid `CREATE TABLE` DDL for `sqlite3_declare_vtab()`, stripping engine parameters and injecting programmatic TVF hidden columns. |
| **Multi-Compiler Verification** | 100% warning-clean on MSVC (`/W4`), Clang (`-Wall -Wextra`), and GCC (`-fsanitize=address,leak`). |

---

## 2. Virtual Table Argument Lifecycle & Architecture

When SQLite encounters a `CREATE VIRTUAL TABLE` statement, it parses the statement into an `argc`/`argv` array and delivers it to the module's `xCreate` or `xConnect` callback:

```sql
CREATE VIRTUAL TABLE my_catalog USING custom_engine(
    tenant_id    INTEGER,
    device_name  VARCHAR(64) NOT NULL,
    payload      BLOB,
    total_cost   REAL GENERATED ALWAYS AS (unit_price * qty) STORED,
    "CAPACITY"   = 4096,
    mode         = 'strict',
    timeout_ms   = 5000,
    PRIMARY KEY (tenant_id, device_name),
    WITHOUT ROWID
);
```

### Argument Layout in `argv`:

```
+========================================================================================================+
| SQLite Core Raw ARGV Array (Passed to xConnect / xCreate)                                              |
+--------------------------------------------------------------------------------------------------------+
| argv[0] = "custom_engine"                                            [Module Name]                     |
| argv[1] = "main"                                                     [Database Name]                   |
| argv[2] = "my_catalog"                                               [Table Name]                      |
| argv[3] = "tenant_id INTEGER"                                        -> SqliteVTabColumn (Col 0)       |
| argv[4] = "device_name VARCHAR(64) NOT NULL"                         -> SqliteVTabColumn (Col 1)       |
| argv[5] = "payload BLOB"                                             -> SqliteVTabColumn (Col 2)       |
| argv[6] = "total_cost REAL GENERATED ALWAYS AS (...) STORED"         -> SqliteVTabColumn (Col 3)       |
| argv[7] = "\"CAPACITY\" = 4096"                                      -> SqliteVTabParam  (capacity)    |
| argv[8] = "mode = 'strict'"                                          -> SqliteVTabParam  (mode)        |
| argv[9] = "timeout_ms = 5000"                                        -> SqliteVTabParam  (timeout_ms)  |
| argv[10]= "PRIMARY KEY (tenant_id, device_name)"                     -> SqliteVTabConstraint (PK)      |
| argv[11]= "WITHOUT ROWID"                                            -> SqliteVTabArg (Kind::Option)   |
+========================================================================================================+
                                   |
                                   v
+========================================================================================================+
| SqliteVTabArgs Batch Parser & Schema Validation                                                        |
|  - Table Metadata: Module="custom_engine", DB="main", Table="my_catalog", WITHOUT ROWID=true           |
|  - Parameters: capacity=4096, mode="strict" (index 1), timeout_ms=5000                                 |
|  - Schema Validation: schema.validate() verifies no unrecognized keys exist                            |
|  - Primary Key Aggregation: Composite PK = ("tenant_id" @ col 0, "device_name" @ col 1)                |
+========================================================================================================+
                                   |
                                   v
+========================================================================================================+
| DDL Synthesis: format_declare_vtab_sql() -> sqlite3_declare_vtab()                                     |
|  "CREATE TABLE my_catalog(tenant_id INTEGER, device_name VARCHAR(64) NOT NULL, payload BLOB,           |
|   total_cost REAL GENERATED ALWAYS AS (unit_price * qty) STORED,                                       |
|   PRIMARY KEY (tenant_id, device_name)) WITHOUT ROWID"                                                 |
+========================================================================================================+
```

---

## 3. Quickstart Tutorial

Below is a complete, production-grade virtual table implementation demonstrating how `SqliteVTabArgs` and `SqliteVTabParamSchema` simplify `connect()` / `create()`:

```cpp
#include <sqlite3.h>
#include "sqlite3_vtab_arg.hpp"
#include "sqlite3_vtab.hpp"
#include "sqlite3_allocator.hpp"

class StorageEngineTable : public SqliteVTable {
private:
    int    m_capacity;
    int    m_mode_idx;
    double m_sample_rate;
    bool   m_debug;
    bool   m_is_without_rowid;

public:
    StorageEngineTable(sqlite3* db, int cap, int mode, double rate, bool debug, bool without_rowid)
        : SqliteVTable(db),
          m_capacity(cap),
          m_mode_idx(mode),
          m_sample_rate(rate),
          m_debug(debug),
          m_is_without_rowid(without_rowid) {}

    static int connect(SqliteConnectArgs& args) {
        // 1. Wrap raw argv into batch parser (user arguments start at argv[3])
        SqliteVTabArgs vargs(args);

        // 2. Declare parameter schema with defaults
        static const char* const kModes[] = { "fast", "strict", "lenient" };
        int    capacity    = 1024;
        int    mode_idx    = 0;       // Default: "fast" (index 0)
        double sample_rate = 1.0;
        bool   debug_mode  = false;

        SqliteVTabParamSchema schema;
        schema.bind_int("capacity", &capacity)
              .bind_enum("mode", kModes, 3, &mode_idx)
              .bind_double("sample_rate", &sample_rate)
              .bind_bool("debug", &debug_mode);

        // 3. Validate arguments & reject unknown parameters
        SqliteStringView unknown_key;
        if (schema.validate(vargs, &unknown_key) > 0) {
            return args.set_error("unrecognized parameter '%.*s'", 
                                  unknown_key.length(), unknown_key.data());
        }
        schema.parse(vargs);

        // 4. Synthesize clean DDL for sqlite3_declare_vtab (strips engine parameters)
        char ddl[512] = {0};
        vargs.format_declare_vtab_sql(ddl, sizeof(ddl));
        int rc = sqlite3_declare_vtab(args.db(), ddl);
        if (rc != SQLITE_OK) return rc;

        // 5. Inspect primary keys & table options
        bool without_rowid = vargs.is_without_rowid();
        if (vargs.is_composite_primary_key()) {
            vargs.for_each_primary_key_indexed([](SqliteStringView pk_col, int col_idx) {
                // Configure composite index layouts...
            });
        }

        // 6. Instantiate table object using SQLite allocator
        args.set_instance(sqlite_new<StorageEngineTable>(
            args.db(), capacity, mode_idx, sample_rate, debug_mode, without_rowid
        ));
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
        // Return cursor instance...
        return nullptr;
    }
};
```

---

## 4. Exhaustive API Reference

### 4.1 `SqliteVTabParam` (Key=Value View)

Represents a single parsed `key=value` argument slice without allocating memory.

```cpp
struct SqliteVTabParam {
    SqliteVTabParam() noexcept;
    SqliteVTabParam(SqliteStringView key, SqliteStringView value) noexcept;

    SqliteStringView key() const noexcept;
    SqliteStringView value() const noexcept;
    SqliteStringView as_str() const noexcept;
    SqliteStringView as_unquoted_str() const noexcept;
    SqliteValueOwned as_value() const;

    bool as_int(int& out) const;
    bool as_long(long long& out) const;
    bool as_int64(sqlite3_int64& out) const;
    bool as_double(double& out) const;
    bool as_float(float& out) const;
    bool as_uint(unsigned int& out) const;
    bool as_size(size_t& out) const;
    bool as_bool(bool& out) const;
    int  as_enum(const char* const* values, int count, int def = -1) const noexcept;
};
```

#### Method Details:

- **`key()` / `value()`**: Returns the trimmed parameter key and value as `SqliteStringView`.
- **`as_unquoted_str()`**: Strips outer quotes (`''`, `""`, ```` ` ````, `[]`) from the value string view.
- **`as_int(int& out)` / `as_long(long long& out)` / `as_int64(sqlite3_int64& out)`**: Parses signed integers (`"%d"` / `"%lld"`). Returns `true` on success. Handles negative numbers (e.g. `"-42"`), zero, and max ranges.
- **`as_double(double& out)` / `as_float(float& out)`**: Parses floating-point numbers (`"%lf"` / `"%f"`). Returns `true` on success. Supports decimals, negative floats, and scientific exponent notation (`1e-4`, `2.5e3`).
- **`as_uint(unsigned int& out)` / `as_size(size_t& out)`**: Parses unsigned integers (`"%u"` / `"%llu"`). Returns `true` on success.
- **`as_bool(bool& out)`**: Recognizes:
  - `true` tokens: `true`, `yes`, `on`, `1` (case-insensitive) or any non-zero integer (`-1`, `42`).
  - `false` tokens: `false`, `no`, `off`, `0` (case-insensitive) or zero (`0`).
- **`as_enum(const char* const* values, int count, int def = -1)`**: Performs a case-insensitive, quote-stripped search against an array of strings. Returns 0-based index if matched, or `def` if not matched.
- **`as_value()`**: Automatically converts literal strings into strongly typed `SqliteValueOwned` objects:
  - `"null"` or empty $\to$ `SQLITE_NULL`
  - `"true"`, `"false"`, `"yes"`, `"no"`, `"on"` $\to$ `SQLITE_INTEGER` (tagged with `SQLITE_SUBTYPE_BOOL`)
  - Quoted string literals $\to$ `SQLITE_TEXT` (quotes stripped)
  - Integer numbers $\to$ `SQLITE_INTEGER`
  - Decimal / exponential numbers $\to$ `SQLITE_FLOAT`
  - Unquoted tokens $\to$ `SQLITE_TEXT`

---

### 4.2 `SqliteVTabColAffinity` & `SqliteVTabColFlags`

#### `SqliteVTabColAffinity`
Evaluates declared SQL data types according to SQLite's official 5 Type Affinity Determination Rules:

```cpp
enum class SqliteVTabColAffinity : unsigned char {
    Unknown = 0,  ///< Default-constructed / unassigned
    Integer,      ///< Contains "INT" (INTEGER, BIGINT, TINYINT, etc.)
    Text,         ///< Contains "CHAR", "CLOB", "TEXT" (VARCHAR(255), etc.)
    Blob,         ///< Contains "BLOB" or untyped (NONE)
    Real,         ///< Contains "REAL", "FLOA", "DOUB" (DOUBLE PRECISION, etc.)
    Numeric,      ///< Everything else (DECIMAL, BOOLEAN, DATE, DATETIME, NUMERIC)
};
```

| Rule | Substring Match (Case-Insensitive) | Resulting Affinity | Example SQL Data Types |
| :--- | :--- | :--- | :--- |
| **1** | Contains `"INT"` | `Integer` | `INT`, `INTEGER`, `BIGINT`, `SMALLINT`, `UNSIGNED BIG INT` |
| **2** | Contains `"CHAR"`, `"CLOB"`, `"TEXT"` | `Text` | `VARCHAR(255)`, `CHARACTER(20)`, `TEXT`, `CLOB`, `NCHAR(55)` |
| **3** | Contains `"BLOB"` or is empty | `Blob` | `BLOB`, empty type definition |
| **4** | Contains `"REAL"`, `"FLOA"`, `"DOUB"` | `Real` | `REAL`, `FLOAT`, `DOUBLE`, `DOUBLE PRECISION` |
| **5** | All other types | `Numeric` | `NUMERIC`, `DECIMAL(10,5)`, `BOOLEAN`, `DATE`, `DATETIME`, `JSON` |

#### `SqliteVTabColFlags`
Bitmask flags identifying inline constraints and column modifiers:

```cpp
enum SqliteVTabColFlags : unsigned int {
    ColFlag_None        = 0,
    ColFlag_NotNull     = 1u << 0,  ///< NOT NULL
    ColFlag_PrimaryKey  = 1u << 1,  ///< PRIMARY KEY
    ColFlag_Unique      = 1u << 2,  ///< UNIQUE
    ColFlag_AutoIncr    = 1u << 3,  ///< AUTOINCREMENT
    ColFlag_Hidden      = 1u << 4,  ///< HIDDEN (Virtual Table parameter column)
    ColFlag_Generated   = 1u << 5,  ///< GENERATED ALWAYS AS or AS (...)
    ColFlag_Stored      = 1u << 6,  ///< STORED generated column
    ColFlag_Virtual     = 1u << 7,  ///< VIRTUAL generated column
};
```

---

### 4.3 `SqliteVTabColumn` (Column Definition View)

Represents a parsed column declaration, separating the column identifier, declared data type, constraints, defaults, and expressions.

```cpp
struct SqliteVTabColumn {
    SqliteVTabColumn() noexcept;
    explicit SqliteVTabColumn(SqliteStringView full_def) noexcept;
    SqliteVTabColumn(SqliteStringView name, SqliteStringView definition) noexcept;

    SqliteStringView name() const noexcept;
    SqliteStringView unquoted_name() const noexcept;
    SqliteStringView definition() const noexcept;
    SqliteStringView full_def() const noexcept;
    SqliteStringView data_type() const noexcept;
    SqliteVTabColAffinity affinity() const noexcept;
    unsigned int flags() const noexcept;

    bool not_null() const noexcept;
    bool primary_key() const noexcept;
    bool is_unique() const noexcept;
    bool is_autoincrement() const noexcept;
    bool is_hidden() const noexcept;
    bool is_rowid_alias() const noexcept;

    bool is_generated() const noexcept;
    bool is_stored() const noexcept;
    bool is_virtual_generated() const noexcept;
    SqliteStringView generated_expression() const noexcept;

    SqliteStringView collation() const noexcept;
    SqliteStringView default_value() const noexcept;
    bool has_default() const noexcept;
};
```

#### Method Details:
- **`name()` / `unquoted_name()`**: Returns the declared column name. `unquoted_name()` automatically strips matching `""`, `''`, ```` ` ````, or `[]`.
- **`data_type()`**: Isolates the pure type declaration (e.g. `"VARCHAR(255)"`, `"INTEGER"`, `"DECIMAL(10, 2)"`) by scanning past complex expressions and stopping before constraint keywords (`NOT NULL`, `PRIMARY KEY`, `AS`, etc.).
- **`is_rowid_alias()`**: Returns `true` if this column acts as the direct alias for the 64-bit integer rowid (i.e. `primary_key() == true` and `affinity() == SqliteVTabColAffinity::Integer`).
- **`is_generated()` / `generated_expression()`**: Extracts the expression inside `GENERATED ALWAYS AS (expr)` or `AS (expr)`.
- **`is_stored()` / `is_virtual_generated()`**: Identifies whether a generated column is persisted (`STORED`) or computed dynamically (`VIRTUAL`).
- **`collation()`**: Extracts the collation sequence name (e.g. `"NOCASE"`, `"BINARY"`, `"RTRIM"`).
- **`default_value()` / `has_default()`**: Extracts literal or parenthesized default value expressions (e.g. `'active'`, `0`, `(datetime('now'))`).

---

### 4.4 `SqliteVTabConstraint` (Table Constraint View)

Represents table-level constraints (`PRIMARY KEY`, `UNIQUE`, `CHECK`, `FOREIGN KEY`).

```cpp
enum class SqliteVTabConstraintKind : unsigned char {
    Unknown = 0,
    PrimaryKey,   ///< PRIMARY KEY (col1, col2, ...)
    Unique,       ///< UNIQUE (col1, col2, ...)
    Check,        ///< CHECK (expr)
    ForeignKey,   ///< FOREIGN KEY (col1, col2) REFERENCES ...
};

struct SqliteVTabConstraint {
    SqliteVTabConstraint() noexcept;
    explicit SqliteVTabConstraint(SqliteStringView full_def) noexcept;

    SqliteVTabConstraintKind kind() const noexcept;
    bool is_primary_key() const noexcept;
    bool is_unique() const noexcept;
    bool is_check() const noexcept;
    bool is_foreign_key() const noexcept;

    SqliteStringView name() const noexcept;
    bool has_name() const noexcept;
    SqliteStringView columns_raw() const noexcept;
    SqliteStringView full_def() const noexcept;

    template <typename Fn> void for_each_column_name(Fn fn) const;
    int column_count() const noexcept;
    bool has_column(SqliteStringView col_name) const noexcept;
};
```

---

### 4.5 `SqliteVTabArg` (Tagged Union)

A tagged union representing any single CREATE argument in `argv[3..argc-1]`:

```cpp
class SqliteVTabArg {
public:
    enum class Kind : unsigned char {
        Empty      = 0,
        Param      = 1,  ///< Key=value argument
        Column     = 2,  ///< Column declaration
        Constraint = 3,  ///< Table constraint (PRIMARY KEY, etc.)
        Option     = 4,  ///< Table option (WITHOUT ROWID)
    };

    SqliteVTabArg() noexcept;
    explicit SqliteVTabArg(const char* raw) noexcept;

    static SqliteVTabArg make_param(SqliteStringView key, SqliteStringView value) noexcept;
    static SqliteVTabArg make_column(SqliteStringView full_def) noexcept;
    static SqliteVTabArg make_constraint(SqliteStringView full_def) noexcept;

    Kind kind() const noexcept;
    bool is_empty() const noexcept;
    bool is_param() const noexcept;
    bool is_column() const noexcept;
    bool is_constraint() const noexcept;
    bool is_option() const noexcept;
    bool is_without_rowid() const noexcept;

    const SqliteVTabParam&      param() const noexcept;
    const SqliteVTabColumn&     column() const noexcept;
    const SqliteVTabConstraint& constraint() const noexcept;

    bool key_is(SqliteStringView name) const noexcept;
};
```

---

### 4.6 `SqliteVTabArgs` (Batch Parser & Table Aggregator)

Wraps the entire `argv` array passed to `xConnect` or `xCreate`.

```cpp
class SqliteVTabArgs {
public:
    SqliteVTabArgs(int argc, const char* const* argv, int user_start = 3) noexcept;
    template <typename ConnectArgs>
    explicit SqliteVTabArgs(const ConnectArgs& args, int user_start = 3) noexcept;

    // Table Metadata:
    SqliteStringView module_name() const noexcept; // argv[0]
    SqliteStringView db_name() const noexcept;     // argv[1]
    SqliteStringView table_name() const noexcept;  // argv[2]
    bool is_without_rowid() const noexcept;

    // Parameter Lookups:
    SqliteVTabParam find_param(SqliteStringView name) const noexcept;
    bool has(SqliteStringView name) const noexcept;
    int get_int(SqliteStringView name, int def = 0) const;
    long long get_long(SqliteStringView name, long long def = 0) const;
    sqlite3_int64 get_int64(SqliteStringView name, sqlite3_int64 def = 0) const;
    double get_double(SqliteStringView name, double def = 0.0) const;
    float get_float(SqliteStringView name, float def = 0.0f) const;
    unsigned int get_uint(SqliteStringView name, unsigned int def = 0) const;
    size_t get_size(SqliteStringView name, size_t def = 0) const;
    bool get_bool(SqliteStringView name, bool def = false) const;
    SqliteStringView get_str(SqliteStringView name, SqliteStringView def = "") const;
    SqliteStringView get_unquoted_str(SqliteStringView name, SqliteStringView def = "") const;
    int get_enum(SqliteStringView name, const char* const* values, int count, int def = -1) const noexcept;
    SqliteValueOwned get_value(SqliteStringView name, SqliteValueOwned def = SqliteValueOwned()) const;

    // Column & Constraint Queries:
    int column_count() const noexcept;
    int param_count() const noexcept;
    int constraint_count() const noexcept;
    int column_index(SqliteStringView col_name) const noexcept;
    SqliteVTabColumn column_at(int idx) const noexcept;
    bool has_column(SqliteStringView col_name) const noexcept;
    SqliteVTabColumn find_column(SqliteStringView col_name) const noexcept;

    // Hidden Columns & RowID Alias:
    bool has_hidden_columns() const noexcept;
    int hidden_column_count() const noexcept;
    template <typename Fn> void for_each_hidden_column(Fn fn) const;
    int rowid_alias_column_index() const noexcept;
    SqliteStringView rowid_alias_column_name() const noexcept;

    // Primary Key Aggregation:
    bool has_primary_key() const noexcept;
    bool is_composite_primary_key() const noexcept;
    int primary_key_count() const noexcept;
    bool is_primary_key_column(SqliteStringView col_name) const noexcept;
    template <typename Fn> void for_each_primary_key(Fn fn) const;
    template <typename Fn> void for_each_primary_key_indexed(Fn fn) const;

    // Iteration:
    template <typename Fn> void for_each(Fn fn) const;
    template <typename Fn> void for_each_param(Fn fn) const;
    template <typename Fn> void for_each_column(Fn fn) const;
    template <typename Fn> void for_each_column_indexed(Fn fn) const;
    template <typename Fn> void for_each_constraint(Fn fn) const;

    // DDL Synthesis:
    int format_declare_vtab_sql(char* out, size_t out_cap,
                                SqliteStringView custom_tbl_name = "",
                                const char* const* extra_cols = nullptr,
                                int extra_col_count = 0) const noexcept;

    // Subscript & Counts:
    int argc() const noexcept;
    int user_argc() const noexcept;
    SqliteVTabArg operator[](int i) const noexcept;
};
```

---

### 4.7 `SqliteVTabParamSchema` (Declarative Schema Builder)

Provides a fluent, bounded schema builder (`MAX_PARAMS = 16`) for validating and binding parameter variables in a single pass.

```cpp
class SqliteVTabParamSchema {
public:
    static constexpr int MAX_PARAMS = 16;

    SqliteVTabParamSchema() noexcept;

    SqliteVTabParamSchema& bind_int(SqliteStringView name, int* out) noexcept;
    SqliteVTabParamSchema& bind_long(SqliteStringView name, long long* out) noexcept;
    SqliteVTabParamSchema& bind_int64(SqliteStringView name, sqlite3_int64* out) noexcept;
    SqliteVTabParamSchema& bind_double(SqliteStringView name, double* out) noexcept;
    SqliteVTabParamSchema& bind_float(SqliteStringView name, float* out) noexcept;
    SqliteVTabParamSchema& bind_uint(SqliteStringView name, unsigned int* out) noexcept;
    SqliteVTabParamSchema& bind_size(SqliteStringView name, size_t* out) noexcept;
    SqliteVTabParamSchema& bind_bool(SqliteStringView name, bool* out) noexcept;
    SqliteVTabParamSchema& bind_str(SqliteStringView name, SqliteStringView* out) noexcept;
    SqliteVTabParamSchema& bind_value(SqliteStringView name, SqliteValueOwned* out) noexcept;
    SqliteVTabParamSchema& bind_enum(SqliteStringView name, const char* const* values, int count, int* out) noexcept;

    int parse(const SqliteVTabArgs& args) const;
    bool has_binding(SqliteStringView name) const noexcept;
    int validate(const SqliteVTabArgs& args, SqliteStringView* first_unknown_out = nullptr) const;
    template <typename Fn> void for_each_unknown(const SqliteVTabArgs& args, Fn fn) const;
    int binding_count() const noexcept;
};
```

---

## 5. Complete Case Coverage & Practical Recipes

### Case 1: Table-Valued Functions (TVF) with Programmatic Hidden Columns

When implementing eponymous Table-Valued Functions (e.g. full-text search, web scraping, or vector similarity search), users pass arguments in the `WHERE` clause or function invocation syntax:

```sql
SELECT * FROM vector_search('machine learning embeddings', 10);
```

SQLite transforms this query into hidden column constraints:

```cpp
static int connect(SqliteConnectArgs& args) {
    SqliteVTabArgs vargs(args);

    // Declare programmatic hidden columns needed for function arguments
    const char* tvf_columns[] = {
        "query TEXT HIDDEN",
        "top_k INT HIDDEN"
    };

    char ddl[512] = {0};
    vargs.format_declare_vtab_sql(ddl, sizeof(ddl),
                                  SqliteStringView(""), 
                                  tvf_columns, 
                                  /*extra_col_count=*/2);

    // Synthesizes: CREATE TABLE vector_search(id INT, score REAL, query TEXT HIDDEN, top_k INT HIDDEN)
    int rc = sqlite3_declare_vtab(args.db(), ddl);
    if (rc != SQLITE_OK) return rc;

    args.set_instance(sqlite_new<VectorSearchTable>(args.db()));
    return SQLITE_OK;
}
```

---

### Case 2: Composite Primary Keys & `WITHOUT ROWID` Tables

For high-throughput key-value storage engines, supporting composite primary keys and `WITHOUT ROWID` enables optimal index structures:

```sql
CREATE VIRTUAL TABLE timeseries USING metrics_engine(
    tenant_id INT,
    device_id INT,
    ts        INT,
    val       REAL,
    PRIMARY KEY (tenant_id, device_id, ts),
    WITHOUT ROWID
);
```

```cpp
SqliteVTabArgs vargs(args);

if (vargs.is_without_rowid()) {
    // Engine allocates clustered B-Tree index without 64-bit rowid overhead
}

if (vargs.is_composite_primary_key()) {
    vargs.for_each_primary_key_indexed([](SqliteStringView pk_col, int col_idx) {
        // pk_col = "tenant_id", col_idx = 0
        // pk_col = "device_id", col_idx = 1
        // pk_col = "ts",        col_idx = 2
    });
}
```

---

### Case 3: Strict Schema Validation & Error Reporting

Reject misspelled parameters and provide actionable error messages back to SQLite:

```cpp
int cap = 1000;
SqliteStringView engine;

SqliteVTabParamSchema schema;
schema.bind_int("capacity", &cap)
      .bind_str("engine", &engine);

SqliteStringView unrecognized;
if (schema.validate(vargs, &unrecognized) > 0) {
    return args.set_error("unrecognized parameter '%.*s' for module %.*s",
                          unrecognized.length(), unrecognized.data(),
                          vargs.module_name().length(), vargs.module_name().data());
}
schema.parse(vargs);
```

---

### Case 4: Generated Column Parsing (`STORED` vs `VIRTUAL` vs Shorthand)

SQLite 3.31+ allows generated columns defined via SQL expressions:

```sql
CREATE VIRTUAL TABLE inventory USING store_mod(
    unit_price  REAL,
    quantity    INT,
    subtotal    REAL GENERATED ALWAYS AS (unit_price * quantity) STORED,
    tax         REAL AS (subtotal * 0.08) VIRTUAL,
    discounted  REAL AS (subtotal * 0.9)
);
```

```cpp
SqliteVTabArgs vargs(args);

for (int i = 0; i < vargs.column_count(); ++i) {
    SqliteVTabColumn col = vargs.column_at(i);
    if (col.is_generated()) {
        SqliteStringView expr = col.generated_expression(); 
        // subtotal   -> "unit_price * quantity"
        // tax        -> "subtotal * 0.08"
        // discounted -> "subtotal * 0.9"

        bool is_stored  = col.is_stored();           // true for subtotal
        bool is_virtual = col.is_virtual_generated();// true for tax
        SqliteStringView type = col.data_type();     // "REAL"
        SqliteVTabColAffinity aff = col.affinity();  // SqliteVTabColAffinity::Real
    }
}
```

---

### Case 5: Integer RowID Alias Detection & Constraints

In SQLite, only exact `INTEGER PRIMARY KEY` columns act as the 64-bit rowid alias. `sqlite3_vtab_arg.hpp` accurately evaluates this invariant:

```sql
CREATE VIRTUAL TABLE logs USING log_mod(
    id INTEGER PRIMARY KEY,
    level VARCHAR(16) NOT NULL,
    msg TEXT
);
```

```cpp
SqliteVTabArgs vargs(args);

int rowid_col_idx = vargs.rowid_alias_column_index(); // Returns 0
SqliteStringView rowid_name = vargs.rowid_alias_column_name(); // "id"

// Edge Cases:
// - `id INT PRIMARY KEY`      -> col.is_rowid_alias() == true (affinity Integer), rowid_alias_column_index() == 0
// - `id BIGINT PRIMARY KEY`   -> col.is_rowid_alias() == true (affinity Integer), rowid_alias_column_index() == 0
// - `id TEXT PRIMARY KEY`     -> col.is_rowid_alias() == false (affinity Text), rowid_alias_column_index() == -1
// - Table is `WITHOUT ROWID`  -> rowid_alias_column_index() == -1
// - Composite PRIMARY KEY     -> rowid_alias_column_index() == -1
```

---

### Case 6: Default Values & Collation Sequences

Extract default value literals or parenthesized expressions, and collations:

```sql
CREATE VIRTUAL TABLE users USING user_mod(
    username  TEXT COLLATE NOCASE,
    status    VARCHAR(32) DEFAULT 'active',
    created   TEXT DEFAULT (datetime('now', 'localtime')),
    retry_cnt INT DEFAULT 0
);
```

```cpp
SqliteVTabArgs vargs(args);

SqliteVTabColumn c0 = vargs.column_at(0); // username
SqliteStringView coll = c0.collation();   // "NOCASE"

SqliteVTabColumn c1 = vargs.column_at(1); // status
SqliteStringView def1 = c1.default_value(); // "'active'"
assert(c1.has_default());

SqliteVTabColumn c2 = vargs.column_at(2); // created
SqliteStringView def2 = c2.default_value(); // "(datetime('now', 'localtime'))"

SqliteVTabColumn c3 = vargs.column_at(3); // retry_cnt
SqliteStringView def3 = c3.default_value(); // "0"
```

---

### Case 7: Mixed-Order Arguments (Top, Middle, Bottom)

Arguments passed to `CREATE VIRTUAL TABLE` can appear in any order. `SqliteVTabArgs` scans arguments without ordering assumptions:

```sql
CREATE VIRTUAL TABLE mixed_table USING sample_mod(
    "timeout_ms" = 5000,
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    "strict_mode" = true,
    score REAL,
    "retries" = 3
);
```

```cpp
SqliteVTabArgs vargs(args);

assert(vargs.column_count() == 3);  // id, name, score
assert(vargs.param_count() == 3);   // timeout_ms, strict_mode, retries
assert(vargs.get_int("timeout_ms") == 5000);
assert(vargs.get_bool("strict_mode") == true);
assert(vargs.get_int("retries") == 3);
```

---

### Case 8: Named & Complex Table Constraints (CHECK, UNIQUE, FK)

Extract constraint names, participating columns, and nested expressions:

```sql
CREATE VIRTUAL TABLE accounts USING acc_mod(
    org_id     INT,
    account_id INT,
    email      TEXT,
    balance    REAL,
    CONSTRAINT pk_org_acc PRIMARY KEY (org_id, account_id),
    CONSTRAINT uq_email UNIQUE (org_id, email),
    CONSTRAINT chk_bal CHECK (balance >= 0.0 AND (org_id > 0)),
    FOREIGN KEY (org_id) REFERENCES organizations(id)
);
```

```cpp
SqliteVTabArgs vargs(args);

assert(vargs.constraint_count() == 4);

vargs.for_each_constraint([](const SqliteVTabConstraint& c) {
    if (c.is_primary_key()) {
        assert(c.has_name() && c.name() == SqliteStringView("pk_org_acc"));
        assert(c.column_count() == 2);
        assert(c.has_column(SqliteStringView("org_id")));
        assert(c.has_column(SqliteStringView("account_id")));
    } else if (c.is_unique()) {
        assert(c.name() == SqliteStringView("uq_email"));
        assert(c.column_count() == 2);
    } else if (c.is_check()) {
        assert(c.name() == SqliteStringView("chk_bal"));
        // c.columns_raw() yields "balance >= 0.0 AND (org_id > 0)"
    } else if (c.is_foreign_key()) {
        assert(c.is_foreign_key());
    }
});
```

---

### Case 9: Standalone Slicing for Config, CLI, & Sub-Modules (`user_start = 0`)

Parse arbitrary configuration strings outside of virtual table `xConnect` routines:

```cpp
const char* raw_options[] = {
    "host = 'db.internal.lan'",
    "port = 5432",
    "ssl = on",
    "pool_size = 32"
};

// user_start = 0 instructs parser that index 0 is already user configuration
SqliteVTabArgs opts(4, raw_options, /*user_start=*/0);

assert(opts.get_unquoted_str("host") == SqliteStringView("db.internal.lan"));
assert(opts.get_int("port") == 5432);
assert(opts.get_bool("ssl") == true);
assert(opts.get_size("pool_size") == 32);
```

---

### Case 10: Dynamic Literal Type Inference via `SqliteValueOwned`

When virtual table parameters can be integers, strings, floats, booleans, or NULL dynamically:

```cpp
SqliteVTabArgs vargs(args);

SqliteValueOwned val = vargs.get_value("threshold");
if (val.is_null()) {
    // Parameter omitted or explicit null
} else if (val.is_int()) {
    int ival = val.as_int();
} else if (val.is_float()) {
    double fval = val.as_double();
} else if (val.is_text()) {
    SqliteStringView sval = val.as_str();
}
```

---

### Case 11: Negative Numbers, Hex, Exponents, and Edge Numerics

`SqliteVTabParam` and `SqliteVTabArgs` handle all valid integer and floating-point representations:

```cpp
const char* raw[] = {
    "module", "db", "tbl",
    "neg_int = -42",
    "neg_long = -9223372036854775807",
    "neg_float = -0.005",
    "scientific = 1e-4",
    "sci_upper = 2.5E3"
};

SqliteVTabArgs vargs(6, raw, 3);

assert(vargs.get_int("neg_int") == -42);
assert(vargs.get_long("neg_long") == -9223372036854775807LL);
assert(vargs.get_double("neg_float") < 0.0);
assert(vargs.get_double("scientific") == 0.0001);
assert(vargs.get_double("sci_upper") == 2500.0);
```

---

### Case 12: Direct Enum Matching with Fallbacks

Match string parameters against valid enum values case-insensitively with default fallbacks:

```cpp
static const char* const kCompressLevels[] = { "none", "fast", "best" };

// Parameter: compression = 'FAST'
int level = vargs.get_enum("compression", kCompressLevels, 3, /*def=*/0);
assert(level == 1); // Matched "fast" (index 1)

// Parameter: compression = 'invalid'
int fallback = vargs.get_enum("compression", kCompressLevels, 3, /*def=*/0);
// Returns def (0)
```

---

## 6. Quote Normalization & Syntax Resilience

`sqlite3_vtab_arg.hpp` handles all 4 standard SQLite quoting styles without memory allocation:

| Quote Notation | Dialect / Standard | Example Syntax | `unquoted_name()` / `as_unquoted_str()` |
| :--- | :--- | :--- | :--- |
| **Double Quotes** | ANSI SQL standard | `"column name"` | `column name` |
| **Single Quotes** | SQL String Literal | `'literal value'` | `literal value` |
| **Backticks** | MySQL compat dialect | `` `ident_name` `` | `ident_name` |
| **Square Brackets** | MS Access / SQL Server | `[custom ident]` | `custom ident` |

### Escaped Quotes & Nested Expressions
- **Doubled Internal Quotes**: Quoted strings containing doubled quotes (e.g. `'John''s Data'` or `"col""name"`) are scanned safely without premature token termination.
- **Nested Parentheses**: Commas and keywords within nested parentheses (e.g. `CHECK (a > 0 AND (b < 10))`, `DEFAULT (datetime('now', 'localtime'))`) do not break column boundaries.

---

## 7. Memory Model & Lifetime Invariants

```
+───────────────────────────────────────────────────────────────────────────+
| SQLite Engine Process Memory (Owns raw argv string buffers)               |
|                                                                           |
|  argv[3] ──> [ "c" | "a" | "p" | "a" | "c" | "i" | "t" | "y" | "=" | ... ] |
+───────────────────────────────────────────────────────────────────────────+
                      ▲                         ▲
                      │                         │
     [SqliteStringView m_key]         [SqliteStringView m_value]
     (ptr = &argv[3][0], len = 8)     (ptr = &argv[3][9], len = 4)
                      │                         │
                      └─────────┬───────────────┘
                                │
                    [SqliteVTabParam on Stack]
```

### Invariants:
1. **Zero Runtime Heap Allocation**: Neither `SqliteVTabArgs`, `SqliteVTabColumn`, `SqliteVTabConstraint`, nor `SqliteVTabParamSchema` call dynamic memory allocators.
2. **Buffer Lifetime**: `SqliteStringView` slices point directly into `argv[...]`. They remain valid for the duration of the `xConnect` or `xCreate` callback invocation. If parameters must outlive the `xConnect` callback, copy them into table instance member variables.

---

## 8. Compiler Support & Verification

| Compiler | Target OS | Flags / Configuration | Status |
| :--- | :--- | :--- | :--- |
| **MSVC (`cl.exe`)** | Windows 10/11 / Server | `/W4 /O2 /permissive- /std:c++17` | Clean (0 Warnings) |
| **Clang (`clang++`)** | MSYS2 / Linux / macOS | `-Wall -Wextra -Werror -O2 -std=c++17` | Clean (0 Warnings) |
| **GCC (`g++`)** | Linux / Ubuntu (WSL) | `-Wall -Wextra -Werror -O2 -fsanitize=address,leak` | Clean (0 Warnings, 0 Leaks) |
| **Freestanding** | Bare Metal / Embedded | `-nostdlib++` | Clean (0 STD Dependencies) |

---

## 9. Troubleshooting & Frequently Asked Questions

### Q: Why does `sqlite3_declare_vtab()` fail with syntax errors when using custom virtual table options?
> **A:** SQLite's core DDL parser in `sqlite3_declare_vtab()` only accepts valid SQL column declarations and table constraints. It rejects engine configuration parameters (like `capacity=1024`). Always use `vargs.format_declare_vtab_sql(ddl, sizeof(ddl))` which automatically strips engine parameters while retaining all SQL columns, constraints, and `WITHOUT ROWID`.

### Q: How do I handle columns that have default values or collation clauses?
> **A:** Use `col.default_value()` and `col.collation()`. For example, `col.default_value()` will return `"0"` for `val INT DEFAULT 0`, and `col.collation()` will return `"NOCASE"` for `name TEXT COLLATE NOCASE`.

### Q: Why does `is_rowid_alias()` return false for `id INT PRIMARY KEY`?
> **A:** In SQLite, only exact `INTEGER PRIMARY KEY` columns act as 64-bit rowid aliases. Data types like `INT PRIMARY KEY`, `BIGINT PRIMARY KEY`, or `SHORTINT PRIMARY KEY` have integer affinity but are stored in SQLite's internal index structures rather than serving as the rowid. `col.is_rowid_alias()` correctly requires both `affinity() == SqliteVTabColAffinity::Integer` and `primary_key() == true`.

---

## 10. Test Suite Coverage & Verification Matrix

The header is thoroughly tested by **16 distinct test suites** located in [`tests/cpp_vtab/test_vtab_arg.cpp`](../tests/cpp_vtab/test_vtab_arg.cpp), covering every corner case and syntax permutation:

| Test Suite | Function Name | Scenarios & Invariants Covered |
| :--- | :--- | :--- |
| **Test 0** | `test_vtab_arg_internal_utilities()` | Boundary slice trimming (`trim_slice`), quote stripping (`strip_quotes`), keyword detection (`ci_starts_with_kw`), doubled quote escapes, mismatched quotes. |
| **Test 1** | `test_vtab_param_accessors_and_conversions()` | Typed scalar conversions (`as_int`, `as_long`, `as_int64`, `as_double`, `as_float`, `as_uint`, `as_size`), full boolean truth tables (`true/false`, `yes/no`, `on/off`, `1/0`, numbers, invalid), and `as_value()` automatic literal inference. |
| **Test 2** | `test_vtab_column_parsing_and_affinities()` | Official SQLite 5 Type Affinities (`Integer`, `Text`, `Blob`, `Real`, `Numeric`), column flags (`NOT NULL`, `PRIMARY KEY`, `UNIQUE`, `AUTOINCREMENT`, `HIDDEN`), `is_rowid_alias()`, `collation()`, and `default_value()`. |
| **Test 3** | `test_vtab_constraint_parsing_and_types()` | Table-level constraints (`PRIMARY KEY`, `UNIQUE`, `CHECK`, `FOREIGN KEY`), named constraints (`CONSTRAINT pk ...`), comma-separated column iteration (`for_each_column_name`), and nested expression parsing. |
| **Test 4** | `test_vtab_arg_tagged_union_lifecycle()` | Tagged union classification (`SqliteVTabArg`), factory methods (`make_param`, `make_column`, `make_constraint`), copy construction, copy assignment, and empty argument safety. |
| **Test 5** | `test_vtab_args_batch_extraction_and_lookup()` | Batch parameter lookups (`get_*`), column indexing (`column_index`, `column_at`), and multi-PK aggregators (`for_each_primary_key`, `has_primary_key`, `is_composite_primary_key`). |
| **Test 6** | `test_vtab_args_mixed_order_and_defaults()` | Arguments in arbitrary order (parameters at top, middle, and bottom of schema) and default fallback returns when keys are absent. |
| **Test 7** | `test_vtab_param_schema_binding_and_parsing()` | `SqliteVTabParamSchema` with all fluent bindings (`bind_int`, `bind_long`, `bind_double`, `bind_float`, `bind_uint`, `bind_size`, `bind_bool`, `bind_str`, `bind_value`, `bind_enum`), validation of unknown keys, and single-pass `parse()`. |
| **Test 8** | `test_vtab_sql_table_creation_and_querying()` | End-to-end SQLite virtual table execution, creating virtual tables with parameters, verifying `sqlite3_declare_vtab()` success, and querying data. |
| **Test 9** | `test_vtab_args_syntax_edge_cases_and_boundaries()` | Extra whitespace, surrounding quotes, doubled quotes inside literals (`''`, `""`), square brackets (`[col]`), MySQL backticks (`` `col` ``), and case-insensitivity. |
| **Test 10** | `test_vtab_args_metadata_unquoting_ddl_and_validation()` | System metadata (`module_name`, `db_name`, `table_name`), clean DDL synthesis (`format_declare_vtab_sql`), parameter stripping, and `WITHOUT ROWID` preservation. |
| **Test 11** | `test_vtab_complex_datatypes_and_flags()` | Complex SQL data types (`DECIMAL(10, 2)`, `VARCHAR(255)`, `UNSIGNED BIG INT`) and bitmask flags. |
| **Test 12** | `test_vtab_composite_constraints_and_indexed_pks()` | Multi-column PK aggregation with column indices (`for_each_primary_key_indexed`) and constraint ordering clauses (`ASC`/`DESC`). |
| **Test 13** | `test_vtab_tvf_and_ddl_synthesis_end_to_end()` | Table-Valued Functions (TVF) with programmatic hidden columns injected via `format_declare_vtab_sql(..., extra_cols, extra_col_count)`. |
| **Test 14** | `test_vtab_direct_enum_scalars_rowid_and_generated_cols()` | Direct enum matching (`as_enum`, `get_enum`), scalar helpers (`get_float`, `get_uint`, `get_size`, `get_int64`), rowid alias index/name, hidden columns, and generated columns (`GENERATED ALWAYS AS`, `AS (expr) STORED/VIRTUAL`). |
| **Test 15** | `test_vtab_advanced_stress_and_edge_cases()` | Negative integers/floats (`-42`, `-9223372036854775807`, `-0.005`), scientific notation (`1e-4`, `2.5E3`), complex math generated expressions, composite constraints with collations, standalone `user_start = 0` parsing, out-of-bounds subscripts, and empty `argv` safety. |

---

## License

Part of the `sqlite-ext-core` library. Distributed under the MIT / Apache 2.0 dual license.
