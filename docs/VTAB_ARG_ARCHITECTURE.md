# Virtual Table Argument Parser Architecture (`sqlite3_vtab_arg.hpp`)

High-performance, zero-allocation C++ argument parser and schema synthesizer for SQLite `CREATE VIRTUAL TABLE` statements.

---

## 1. The Core Architectural Challenge

When SQLite executes `CREATE VIRTUAL TABLE <table_name> USING <module_name>(<args...>)`, SQLite's core engine splits comma-separated arguments and passes them to the virtual table's `xConnect` or `xCreate` callback as an array of raw C-strings in `argv[0..argc-1]`:

```
argv[0] = module_name        (e.g., "memkv")
argv[1] = db_name            (e.g., "main")
argv[2] = table_name         (e.g., "cache_vtab")
argv[3..argc-1] = user_args  (raw argument strings)
```

In modern SQLite extensions, user arguments come in four fundamentally different syntactic categories:

1. **Engine Parameters (`key=value`)**:
   - e.g., `capacity=1024`, `mode='strict'`, `ttl=60`, `enabled=true`, `ratio=0.875`
2. **SQL Column Declarations**:
   - e.g., `id INTEGER PRIMARY KEY AUTOINCREMENT`, `username TEXT NOT NULL`, `area REAL AS (w * h) STORED`, `tag TEXT HIDDEN`
3. **Table-Level Constraints**:
   - e.g., `PRIMARY KEY (user_id, device_id)`, `CONSTRAINT uq_name UNIQUE (email)`, `CHECK (age >= 0)`, `FOREIGN KEY (dept_id) REFERENCES departments(id)`
4. **Table-Level Options**:
   - e.g., `WITHOUT ROWID`

### The Complexities of Virtual Table Argument Parsing:
- **No Schema Separation**: SQLite mixes engine parameters, column declarations, and constraints in arbitrary order within `argv[3..argc-1]`.
- **Quote Variations**: Identifiers, keys, and string values can be wrapped in single quotes (`'...'`), double quotes (`"..."`), backticks (`` `...` ``), or brackets (`[...]`).
- **Parentheses Nesting**: Column defaults and generated expressions (`DEFAULT (datetime('now'))`, `AS (width * height)`) contain nested parentheses, operators, and commas that must not be mistaken for argument delimiters or constraints.
- **SQLite DDL Synthesis**: The virtual table module must register its SQL schema with SQLite via `sqlite3_declare_vtab()`, but engine parameters (`capacity=1024`) must be filtered out while preserving valid columns, hidden TVF arguments, and table constraints.
- **Zero-Allocation Requirement**: In high-throughput environments and `-nostdlib++` freestanding builds, parsing arguments must avoid heap memory allocations (`malloc` / `new`).

---

## 2. Architectural Design & Type Hierarchy

`sqlite3_vtab_arg.hpp` solves these challenges through a layered, zero-allocation type hierarchy:

```
+========================================================================================================+
| SQLite argv[3..argc-1] Memory Buffer (Owned by SQLite Core Engine)                                     |
+========================================================================================================+
                                   |
                                   v
+========================================================================================================+
| SqliteVTabArgs (Batch Parser & Table Aggregator)                                                       |
|  - Module / DB / Table Metadata Accessors (argv[0..2])                                                 |
|  - Typed Parameter Lookups & Direct Enum Resolution                                                    |
|  - Column Indexing & Multi-PK Aggregator (Inline PK + Table Composite PK)                              |
|  - Rowid Alias & Hidden Column Inspection                                                              |
|  - DDL Synthesizer for sqlite3_declare_vtab()                                                          |
+========================================================================================================+
         |                                |                                   |
         v                                v                                   v
+------------------+            +-------------------+               +--------------------+
| SqliteVTabParam  |            | SqliteVTabColumn  |               | SqliteVTabConstraint
| (key=value view) |            | (SQL column view) |               | (table constraint) |
+------------------+            +-------------------+               +--------------------+
| - key()          |            | - name()          |               | - kind()           |
| - value()        |            | - unquoted_name() |               | - name()           |
| - as_int()       |            | - data_type()     |               | - columns_raw()    |
| - as_int64()     |            | - affinity()      |               | - column_count()   |
| - as_double()    |            | - flags()         |               | - for_each_column()|
| - as_float()     |            | - not_null()      |               | - has_column()     |
| - as_uint()      |            | - primary_key()   |               +--------------------+
| - as_size()      |            | - is_unique()     |
| - as_bool()      |            | - is_autoincrement|
| - as_unquoted_str|            | - is_hidden()     |
| - as_enum()      |            | - is_generated()  |
| - as_value()     |            | - is_rowid_alias()|
+------------------+            | - default_value() |
                                | - collation()     |
                                +-------------------+
                                          |
                                          v
                    +===========================================+
                    | SqliteVTabParamSchema (Declarative Pass)  |
                    |  - Fluent Bounded Slot Bindings (MAX=16)  |
                    |  - Case-Insensitive / Quote-Stripped Match|
                    |  - Type Coercion & Enum String Mapping    |
                    |  - Unknown Parameter Detection & Erroring |
                    +===========================================+
```

---

## 3. Parsing Pipeline & State Machine

When `SqliteVTabArg(const char* raw)` parses a raw argument slice, it executes a deterministic 4-step categorization:

```
[Raw Argument String]
         |
         |---> 1. Starts with "PRIMARY KEY", "UNIQUE", "CHECK", "FOREIGN KEY", "CONSTRAINT"?
         |        └─> [Kind::Constraint] -> Constructs SqliteVTabConstraint
         |
         |---> 2. Contains "WITHOUT ROWID" sequence?
         |        └─> [Kind::Option] -> Marks table-level WITHOUT ROWID
         |
         |---> 3. Contains unparenthesized '=' with valid identifier key?
         |        └─> [Kind::Param] -> Slices into key and value string views
         |
         └─> 4. Default:
                  └─> [Kind::Column] -> Slices column name from definition and constraints
```

### Quote-Aware Scanning Primitives:
The `sqlite_vtab_arg_internal` namespace provides zero-allocation scanning routines:
- **`skip_quoted(s, len, pos)`**: Safely steps past quoted tokens (`''`, `""`, ```` ````, `[]`), properly consuming escaped doubled quotes (`''` / `""`).
- **`skip_parenthesized(s, len, pos)`**: Depth-aware scanning that balances parentheses while ignoring inner quote characters and operators.
- **`strip_quotes(SqliteStringView)`**: Unwraps matching outer quote pairs in $O(1)$ time without copying.
- **`ci_has_kw_sequence(s, len, kw1, kw2)`**: Case-insensitive identifier-boundary keyword sequence search using `sqlite3_strnicmp`.

---

## 4. SQLite Affinity & Column Analysis Engine

`SqliteVTabColumn::affinity()` implements SQLite's official 5 Type Affinity Rules:

| Rule | Definition Pattern | Determined Affinity | SQLite Constant |
| :--- | :--- | :--- | :--- |
| **Rule 1** | Definition contains `"INT"` (`INTEGER`, `BIGINT`, `TINYINT`, `INT2`, etc.) | `SqliteVTabColAffinity::Integer` | `SQLITE_AFF_INTEGER` |
| **Rule 2** | Definition contains `"CHAR"`, `"CLOB"`, or `"TEXT"` (`VARCHAR(255)`, etc.) | `SqliteVTabColAffinity::Text` | `SQLITE_AFF_TEXT` |
| **Rule 3** | Definition contains `"BLOB"` or is empty / untyped | `SqliteVTabColAffinity::Blob` | `SQLITE_AFF_BLOB` |
| **Rule 4** | Definition contains `"REAL"`, `"FLOA"`, or `"DOUB"` (`DOUBLE PRECISION`, etc.) | `SqliteVTabColAffinity::Real` | `SQLITE_AFF_REAL` |
| **Rule 5** | Everything else (`NUMERIC`, `DECIMAL(10,2)`, `BOOLEAN`, `DATETIME`, etc.) | `SqliteVTabColAffinity::Numeric` | `SQLITE_AFF_NUMERIC` |

### Column Flags Engine:
`SqliteVTabColumn::flags()` parses column-level constraints into a compact bitmask (`unsigned int`):
- `ColFlag_NotNull` (`NOT NULL`)
- `ColFlag_PrimaryKey` (`PRIMARY KEY`)
- `ColFlag_Unique` (`UNIQUE`)
- `ColFlag_AutoIncr` (`AUTOINCREMENT`)
- `ColFlag_Hidden` (`HIDDEN`)
- `ColFlag_Generated` (`GENERATED ALWAYS AS` or `AS`)
- `ColFlag_Stored` (`STORED`)
- `ColFlag_Virtual` (`VIRTUAL`)

---

## 5. DDL Synthesis Architecture (`format_declare_vtab_sql`)

SQLite virtual tables must invoke `sqlite3_declare_vtab(db, ddl)` during `xConnect`/`xCreate` to declare their column layout. `SqliteVTabArgs::format_declare_vtab_sql` generates clean, standards-compliant SQL:

```
+========================================================================================================+
| CREATE TABLE <table_name> (                                                                            |
|   1. Declared Columns   (Preserving types, constraints, HIDDEN modifiers from argv)                    |
|   2. TVF Extra Columns  (Optional programmatic hidden argument columns, e.g. "query TEXT HIDDEN")      |
|   3. Table Constraints  (Preserving PRIMARY KEY(a,b), UNIQUE, CHECK, FOREIGN KEY)                      |
| ) [WITHOUT ROWID]                                                                                      |
+========================================================================================================+
```

### DDL Formatting Properties:
- **Parameter Stripping**: Completely excludes engine parameters (`capacity=1024`, `mode=strict`).
- **Buffer Safety**: Writes directly to a caller-provided fixed buffer (`char* out, size_t out_cap`) with boundary checks and guarantees null termination.
- **TVF Hidden Arguments**: Seamlessly appends programmatic hidden arguments required by Table-Valued Functions before closing the column list.

---

## 6. Declarative Parameter Validation (`SqliteVTabParamSchema`)

`SqliteVTabParamSchema` provides a declarative, fluent builder pattern for parsing and validating virtual table parameters:

```cpp
SqliteVTabParamSchema schema;
schema.bind_int("capacity", &cap)
      .bind_enum("mode", kModes, 3, &mode_idx)
      .bind_float("sample_rate", &rate)
      .bind_bool("debug", &is_debug);

// Single-pass validation:
SqliteStringView unknown_key;
if (schema.validate(vargs, &unknown_key) > 0) {
    return args.set_error("unrecognized parameter '%s'", unknown_key.data());
}
schema.parse(vargs);
```

### Schema Architecture Details:
- **Zero Allocations**: Uses a fixed 16-slot bounded array (`m_slots[MAX_PARAMS]`) on the stack.
- **Quote-Resilient Matching**: Matching strips outer quotes from both schema keys and user argument keys (`"CAPACITY"` matches `capacity=...` and `[capacity]=...`).
- **Case-Insensitive Collation**: Key comparisons use `sqlite3_strnicmp`.
- **Unknown Parameter Traversal**: `for_each_unknown()` and `validate()` detect typos and unrecognized keys in a single pass.

---

## 7. Zero-Overhead & `-nostdlib++` Guarantees

1. **No Dynamic Allocations**: String views (`SqliteStringView`) borrow memory directly from SQLite's `argv` strings.
2. **Freestanding Ready**: All string manipulations rely on SQLite's internal functions (`sqlite3_strnicmp`, `sqlite3_snprintf`) rather than `std::string` or `std::regex`.
3. **No Exceptions or RTTI**: Fully compliant with `-fno-exceptions -fno-rtti` compiler configurations.
4. **Compile-Time Constant Safety**: Enum values, affinities, and flags are evaluated with `constexpr` and inline optimizations.
