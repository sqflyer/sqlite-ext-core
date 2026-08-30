#ifndef SQLITE3_VTAB_ARG_HPP
#define SQLITE3_VTAB_ARG_HPP

/**
 * @file sqlite3_vtab_arg.hpp
 * @brief Typed parser for SQLite virtual table CREATE VIRTUAL TABLE arguments.
 *
 * ── Overview ─────────────────────────────────────────────────────────────────
 *
 * Virtual table modules receive user-supplied arguments as raw C-strings in
 * argv[3..argc-1]. Arguments come in three distinct forms:
 *
 *   1. Param  (key=value)       — engine parameters, e.g. "capacity=1024", "ttl=30",
 *                                 "mode=strict", "ratio=0.75", "enabled=true"
 *   2. Column (col def)         — SQL column declarations, e.g. "event_name TEXT",
 *                                 "id INTEGER PRIMARY KEY", "score REAL NOT NULL"
 *   3. TableConstraint          — table-level constraints, e.g. "PRIMARY KEY (a, b)",
 *                                 "CONSTRAINT pk PRIMARY KEY (user_id, device_id)",
 *                                 "UNIQUE (email, org_id)", "CHECK (age >= 0)"
 *
 * ── Type hierarchy ───────────────────────────────────────────────────────────
 *
 *   SqliteVTabParam       — view into a key=value arg; typed as_*() accessors.
 *   SqliteVTabColumn      — view into a column-definition arg; affinity & flags.
 *   SqliteVTabConstraint  — view into a table-level constraint (e.g. multi/composite PK).
 *   SqliteVTabArg         — tagged union: Kind::Param | Kind::Column | Kind::Constraint | Kind::Empty
 *   SqliteVTabArgs        — batch parser wrapping argv[3..]; get_*() typed lookups,
 *                           for_each_param(), for_each_column(), for_each_constraint(),
 *                           for_each_primary_key(), is_primary_key_column(),
 *                           is_composite_primary_key().
 *   SqliteVTabParamSchema — declarative parameter schema with fluent binding & enums.
 *
 * ── Usage ────────────────────────────────────────────────────────────────────
 *
 *   static int connect(SqliteConnectArgs& args) {
 *       SqliteVTabArgs vargs(args);
 *
 *       // Typed parameter lookup with fallback defaults:
 *       size_t cap    = vargs.get_size ("capacity", 1024);
 *       int    ttl    = vargs.get_int  ("ttl",      60);
 *       bool   strict = vargs.get_bool ("strict",   false);
 *
 *       // Composite Primary Key inspection:
 *       if (vargs.is_composite_primary_key()) {
 *           vargs.for_each_primary_key([](SqliteStringView pk_col) {
 *               // Handles both single-column and multi-column PRIMARY KEY (a, b)
 *           });
 *       }
 *
 *       // Column-only iteration for schema building:
 *       vargs.for_each_column([](const SqliteVTabColumn& col) {
 *           // col.name(), col.definition(), col.affinity(), col.not_null()
 *       });
 *   }
 *
 * ── Memory ───────────────────────────────────────────────────────────────────
 *
 *   Zero dynamic allocations. 100% freestanding (`-nostdlib++`).
 *   All SqliteStringView members point into the original argv strings which SQLite
 *   owns for the duration of connect() / create().
 */

#include <sqlite3.h>
#include <stdio.h>
#include "sqlite3_value.hpp"

// =============================================================================
// Compiler warning suppression for scalar sscanf parsing
// =============================================================================

#if defined(_MSC_VER)
#  define SQLITE_VTAB_ARG_WARN_PUSH \
       __pragma(warning(push))      \
       __pragma(warning(disable: 4996))
#  define SQLITE_VTAB_ARG_WARN_POP  \
       __pragma(warning(pop))
#elif defined(__clang__)
#  define SQLITE_VTAB_ARG_WARN_PUSH \
       _Pragma("clang diagnostic push") \
       _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"")
#  define SQLITE_VTAB_ARG_WARN_POP  \
       _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
#  define SQLITE_VTAB_ARG_WARN_PUSH \
       _Pragma("GCC diagnostic push") \
       _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#  define SQLITE_VTAB_ARG_WARN_POP  \
       _Pragma("GCC diagnostic pop")
#else
#  define SQLITE_VTAB_ARG_WARN_PUSH
#  define SQLITE_VTAB_ARG_WARN_POP
#endif

// =============================================================================
// Helper String Scanning Utilities
// =============================================================================

/** @cond INTERNAL */
namespace sqlite_vtab_arg_internal {

    static inline SqliteStringView trim_slice(const char* base, int lo, int hi) {
        while (lo < hi && (base[lo] == ' ' || base[lo] == '\t' || base[lo] == '\r' || base[lo] == '\n')) ++lo;
        while (hi > lo && (base[hi - 1] == ' ' || base[hi - 1] == '\t' || base[hi - 1] == '\r' || base[hi - 1] == '\n')) --hi;
        return SqliteStringView(base + lo, hi - lo);
    }

    static inline bool ci_contains(const char* haystack, int hlen, const char* needle, int nlen) {
        if (nlen > hlen) return false;
        for (int i = 0; i <= hlen - nlen; ++i) {
            if (sqlite3_strnicmp(haystack + i, needle, nlen) == 0) return true;
        }
        return false;
    }

    static inline bool ci_starts_with(const char* str, int len, const char* prefix, int plen) {
        if (len < plen) return false;
        return sqlite3_strnicmp(str, prefix, plen) == 0;
    }

    static inline bool ci_equals(SqliteStringView a, const char* b) {
        if (!b) return false;
        int b_len = SqliteStringUtil::sqlite_strlen(b);
        if (a.length() != b_len) return false;
        return sqlite3_strnicmp(a.data(), b, a.length()) == 0;
    }

} // namespace sqlite_vtab_arg_internal
/** @endcond */

// =============================================================================
// 1. SqliteVTabParam — key=value argument
// =============================================================================

/**
 * @struct SqliteVTabParam
 * @brief View into a key=value vtab argument with typed value accessors.
 *
 * Both key and value are SqliteStringViews into the original argv string —
 * zero copies, zero allocations.
 */
struct SqliteVTabParam {
    SqliteStringView m_key;    ///< Parameter key (trimmed), e.g. "capacity"
    SqliteStringView m_value;  ///< Parameter value (trimmed), e.g. "1024"

    inline SqliteVTabParam() noexcept
        : m_key("", 0), m_value("", 0) {}

    inline SqliteVTabParam(SqliteStringView key, SqliteStringView value) noexcept
        : m_key(key), m_value(value) {}

    /// The key portion, e.g. "capacity".
    inline SqliteStringView key()   const noexcept { return m_key;   }

    /// The value portion as a raw string view, e.g. "1024" or "strict".
    inline SqliteStringView value() const noexcept { return m_value; }

    /** Parses value as a 32-bit signed integer ("%d"). Returns true on success. */
    inline bool as_int(int& out) const {
        if (m_value.empty()) return false;
        int parsed = 0;
        SQLITE_VTAB_ARG_WARN_PUSH
        int rc = sscanf(m_value.data(), "%d", &parsed);
        SQLITE_VTAB_ARG_WARN_POP
        if (rc == 1) { out = parsed; return true; }
        return false;
    }

    /** Parses value as a 64-bit signed integer ("%lld"). Returns true on success. */
    inline bool as_long(long long& out) const {
        if (m_value.empty()) return false;
        long long parsed = 0;
        SQLITE_VTAB_ARG_WARN_PUSH
        int rc = sscanf(m_value.data(), "%lld", &parsed);
        SQLITE_VTAB_ARG_WARN_POP
        if (rc == 1) { out = parsed; return true; }
        return false;
    }

    /** Parses value as a double-precision float ("%lf"). Returns true on success. */
    inline bool as_double(double& out) const {
        if (m_value.empty()) return false;
        double parsed = 0.0;
        SQLITE_VTAB_ARG_WARN_PUSH
        int rc = sscanf(m_value.data(), "%lf", &parsed);
        SQLITE_VTAB_ARG_WARN_POP
        if (rc == 1) { out = parsed; return true; }
        return false;
    }

    /** Parses value as an unsigned size_t ("%llu"). Returns true on success. */
    inline bool as_size(size_t& out) const {
        if (m_value.empty()) return false;
        unsigned long long parsed = 0;
        SQLITE_VTAB_ARG_WARN_PUSH
        int rc = sscanf(m_value.data(), "%llu", &parsed);
        SQLITE_VTAB_ARG_WARN_POP
        if (rc == 1) { out = static_cast<size_t>(parsed); return true; }
        return false;
    }

    /**
     * @brief Parses value as a boolean.
     *
     * Numeric non-zero → true, zero → false.
     * Text tokens (case-insensitive):
     *   true  — "true", "yes", "on", "1"
     *   false — "false", "no", "off", "0"
     */
    inline bool as_bool(bool& out) const {
        if (m_value.empty()) return false;
        const char* v = m_value.data();
        int         l = m_value.length();

        int num = 0;
        SQLITE_VTAB_ARG_WARN_PUSH
        int rc = sscanf(v, "%d", &num);
        SQLITE_VTAB_ARG_WARN_POP
        if (rc == 1) { out = (num != 0); return true; }

        auto ci = [&](const char* tok) -> bool {
            int tlen = SqliteStringUtil::sqlite_strlen(tok);
            return l == tlen && sqlite3_strnicmp(v, tok, l) == 0;
        };

        if (ci("true") || ci("yes") || ci("on"))  { out = true;  return true; }
        if (ci("false")|| ci("no")  || ci("off")) { out = false; return true; }
        return false;
    }

    /** Returns the value directly as a SqliteStringView (string param). */
    inline SqliteStringView as_str() const noexcept { return m_value; }

    /**
     * @brief Parses the parameter value into a dynamically typed SqliteValueOwned.
     *
     * Delegates to SqliteValueOwned::from_literal() with automatic SQL type inference:
     *   - "null" / ""                         → SQLITE_NULL
     *   - "true", "false", "yes", "no", "on"  → SQLITE_INTEGER (tagged with SQLITE_SUBTYPE_BOOL)
     *   - Quoted strings ('text' or "text")    → SQLITE_TEXT (quotes stripped)
     *   - Integer numbers ("1024", "-42")     → SQLITE_INTEGER
     *   - Floating-point ("3.1415", "1e-4")   → SQLITE_FLOAT
     *   - Unquoted identifiers ("fast", "xyz") → SQLITE_TEXT
     */
    inline SqliteValueOwned as_value() const {
        return SqliteValueOwned::from_literal(m_value);
    }
};

// =============================================================================
// 2. SqliteVTabColAffinity & SqliteVTabColFlags
// =============================================================================

/**
 * @enum SqliteVTabColAffinity
 * @brief SQLite's 5 type affinities, determined from column definition rules.
 *
 *   Rule 1: definition contains "INT"              → Integer
 *   Rule 2: definition contains "CHAR","CLOB","TEXT" → Text
 *   Rule 3: definition contains "BLOB" or is empty → Blob (no affinity)
 *   Rule 4: definition contains "REAL","FLOA","DOUB" → Real
 *   Rule 5: otherwise                              → Numeric
 */
enum class SqliteVTabColAffinity : unsigned char {
    Unknown = 0,  ///< Not yet determined / default-constructed
    Integer,      ///< Contains "INT"                        (SQLITE_AFF_INTEGER)
    Text,         ///< Contains "CHAR","CLOB","TEXT"         (SQLITE_AFF_TEXT)
    Blob,         ///< Contains "BLOB" or empty              (SQLITE_AFF_BLOB)
    Real,         ///< Contains "REAL","FLOA","DOUB"         (SQLITE_AFF_REAL)
    Numeric,      ///< Everything else                       (SQLITE_AFF_NUMERIC)
};

/**
 * @enum SqliteVTabColFlags
 * @brief Bitmask of column constraints parsed from inline column definition.
 */
enum SqliteVTabColFlags : unsigned int {
    ColFlag_None        = 0,
    ColFlag_NotNull     = 1u << 0,  ///< "NOT NULL" present
    ColFlag_PrimaryKey  = 1u << 1,  ///< "PRIMARY KEY" present inline
    ColFlag_Unique      = 1u << 2,  ///< "UNIQUE" present
    ColFlag_AutoIncr    = 1u << 3,  ///< "AUTOINCREMENT" present
    ColFlag_Hidden      = 1u << 4,  ///< "HIDDEN" present (SQLite virtual table hidden column)
};

// =============================================================================
// 3. SqliteVTabColumn — single column-definition argument
// =============================================================================

/**
 * @struct SqliteVTabColumn
 * @brief View into a column-definition vtab argument split into name + definition.
 *
 * Example: "id INTEGER PRIMARY KEY NOT NULL"
 *   name()       → "id"
 *   definition() → "INTEGER PRIMARY KEY NOT NULL"
 *   full_def()   → "id INTEGER PRIMARY KEY NOT NULL"
 */
struct SqliteVTabColumn {
    SqliteStringView m_name;        ///< Column name (e.g. "id")
    SqliteStringView m_definition;  ///< Type + constraints (e.g. "INTEGER PRIMARY KEY")

    inline SqliteVTabColumn() noexcept
        : m_name("", 0), m_definition("", 0) {}

    inline explicit SqliteVTabColumn(SqliteStringView full) noexcept
        : m_name("", 0), m_definition("", 0)
    {
        const char* p   = full.data();
        int         len = full.length();
        if (len == 0) return;

        int name_end = 0;
        SQLITE_VTAB_ARG_WARN_PUSH
        sscanf(p, "%*[^ \t\r\n]%n", &name_end);
        SQLITE_VTAB_ARG_WARN_POP

        m_name = SqliteStringView(p, name_end);

        int skip = 0;
        if (name_end < len) {
            SQLITE_VTAB_ARG_WARN_PUSH
            sscanf(p + name_end, "%*[ \t\r\n]%n", &skip);
            SQLITE_VTAB_ARG_WARN_POP
        }

        int def_start = name_end + skip;
        m_definition  = SqliteStringView(p + def_start, len - def_start);
    }

    inline SqliteVTabColumn(SqliteStringView name, SqliteStringView definition) noexcept
        : m_name(name), m_definition(definition) {}

    /// Column name, e.g. "event_name".
    inline SqliteStringView name()       const noexcept { return m_name;       }

    /// SQL type + constraints, e.g. "TEXT NOT NULL".
    inline SqliteStringView definition() const noexcept { return m_definition; }

    /// Reconstructs the full "name type" string view without copying.
    inline SqliteStringView full_def() const noexcept {
        if (m_definition.empty()) return m_name;
        int total = static_cast<int>((m_definition.data() + m_definition.length()) - m_name.data());
        return SqliteStringView(m_name.data(), total);
    }

    /// Computes SQLite type affinity based on SQLite's 5 official rules.
    inline SqliteVTabColAffinity affinity() const noexcept {
        const char* d = m_definition.data();
        int         n = m_definition.length();
        if (n == 0) return SqliteVTabColAffinity::Blob;

        if (sqlite_vtab_arg_internal::ci_contains(d, n, "INT", 3)) return SqliteVTabColAffinity::Integer;
        if (sqlite_vtab_arg_internal::ci_contains(d, n, "CHAR", 4) ||
            sqlite_vtab_arg_internal::ci_contains(d, n, "CLOB", 4) ||
            sqlite_vtab_arg_internal::ci_contains(d, n, "TEXT", 4)) return SqliteVTabColAffinity::Text;
        if (sqlite_vtab_arg_internal::ci_contains(d, n, "BLOB", 4)) return SqliteVTabColAffinity::Blob;
        if (sqlite_vtab_arg_internal::ci_contains(d, n, "REAL", 4) ||
            sqlite_vtab_arg_internal::ci_contains(d, n, "FLOA", 4) ||
            sqlite_vtab_arg_internal::ci_contains(d, n, "DOUB", 4)) return SqliteVTabColAffinity::Real;
        return SqliteVTabColAffinity::Numeric;
    }

    /// Scans definition for constraint keywords and returns bitmask flags.
    inline unsigned int flags() const noexcept {
        const char* d = m_definition.data();
        int         n = m_definition.length();
        unsigned int f = ColFlag_None;
        if (n == 0) return f;

        if (sqlite_vtab_arg_internal::ci_contains(d, n, "NOT NULL", 8))       f |= ColFlag_NotNull;
        if (sqlite_vtab_arg_internal::ci_contains(d, n, "PRIMARY KEY", 11))   f |= ColFlag_PrimaryKey;
        if (sqlite_vtab_arg_internal::ci_contains(d, n, "UNIQUE", 6))         f |= ColFlag_Unique;
        if (sqlite_vtab_arg_internal::ci_contains(d, n, "AUTOINCREMENT", 13)) f |= ColFlag_AutoIncr;
        if (sqlite_vtab_arg_internal::ci_contains(d, n, "HIDDEN", 6))        f |= ColFlag_Hidden;
        return f;
    }

    inline bool not_null()    const noexcept { return (flags() & ColFlag_NotNull)    != 0; }
    inline bool primary_key() const noexcept { return (flags() & ColFlag_PrimaryKey) != 0; }
    inline bool is_hidden()   const noexcept { return (flags() & ColFlag_Hidden)      != 0; }

    /**
     * @brief Extracts the COLLATE sequence name if specified (e.g. "NOCASE", "BINARY", "RTRIM").
     * Returns empty SqliteStringView if no collation is specified.
     */
    inline SqliteStringView collation() const noexcept {
        const char* d = m_definition.data();
        int         n = m_definition.length();
        if (n < 7) return SqliteStringView("", 0);

        for (int i = 0; i <= n - 7; ++i) {
            if (sqlite3_strnicmp(d + i, "COLLATE", 7) == 0) {
                int start = i + 7;
                while (start < n && (d[start] == ' ' || d[start] == '\t')) ++start;
                int end = start;
                while (end < n && d[end] != ' ' && d[end] != '\t' && d[end] != ',' && d[end] != ')') ++end;
                return SqliteStringView(d + start, end - start);
            }
        }
        return SqliteStringView("", 0);
    }

    /**
     * @brief Extracts the DEFAULT value clause if specified (e.g. "'active'", "0", "1.0", "(now())").
     * Returns empty SqliteStringView if no DEFAULT clause is present.
     */
    inline SqliteStringView default_value() const noexcept {
        const char* d = m_definition.data();
        int         n = m_definition.length();
        if (n < 7) return SqliteStringView("", 0);

        for (int i = 0; i <= n - 7; ++i) {
            if (sqlite3_strnicmp(d + i, "DEFAULT", 7) == 0) {
                int start = i + 7;
                while (start < n && (d[start] == ' ' || d[start] == '\t')) ++start;
                if (start >= n) return SqliteStringView("", 0);

                int end = start;
                if (d[start] == '\'') {
                    // Quoted string literal
                    ++end;
                    while (end < n && d[end] != '\'') ++end;
                    if (end < n && d[end] == '\'') ++end;
                } else if (d[start] == '(') {
                    // Parenthesized expression
                    int paren_depth = 1;
                    ++end;
                    while (end < n && paren_depth > 0) {
                        if (d[end] == '(') ++paren_depth;
                        else if (d[end] == ')') --paren_depth;
                        ++end;
                    }
                } else {
                    // Single token / number
                    while (end < n && d[end] != ' ' && d[end] != '\t' && d[end] != ',' && d[end] != ')') ++end;
                }
                return SqliteStringView(d + start, end - start);
            }
        }
        return SqliteStringView("", 0);
    }

    inline bool has_default() const noexcept {
        return !default_value().empty();
    }
};

// =============================================================================
// 4. SqliteVTabConstraint — table-level constraint (Composite Primary Key, Unique, etc.)
// =============================================================================

/**
 * @enum SqliteVTabConstraintKind
 * @brief Categorization of table-level constraints.
 */
enum class SqliteVTabConstraintKind : unsigned char {
    Unknown = 0,
    PrimaryKey,   ///< PRIMARY KEY (col1, col2, ...)
    Unique,       ///< UNIQUE (col1, col2, ...)
    Check,        ///< CHECK (expression)
    ForeignKey,   ///< FOREIGN KEY (col1, col2) REFERENCES ...
};

/**
 * @struct SqliteVTabConstraint
 * @brief View into a table-level constraint argument (e.g. multi/composite primary key).
 *
 * Supports formats:
 *   - "PRIMARY KEY (user_id, device_id)"
 *   - "CONSTRAINT pk_name PRIMARY KEY (col1, col2)"
 *   - "UNIQUE (tenant_id, email)"
 *   - "CHECK (score >= 0)"
 */
struct SqliteVTabConstraint {
    SqliteVTabConstraintKind m_kind;
    SqliteStringView         m_name;        ///< Constraint name (if "CONSTRAINT name ..."), or empty
    SqliteStringView         m_columns_raw; ///< Raw text inside parentheses, e.g. "user_id, device_id"
    SqliteStringView         m_full;        ///< Full trimmed constraint string

    inline SqliteVTabConstraint() noexcept
        : m_kind(SqliteVTabConstraintKind::Unknown), m_name("", 0), m_columns_raw("", 0), m_full("", 0) {}

    inline explicit SqliteVTabConstraint(SqliteStringView full) noexcept
        : m_kind(SqliteVTabConstraintKind::Unknown), m_name("", 0), m_columns_raw("", 0), m_full(full)
    {
        const char* p = full.data();
        int len = full.length();
        if (len == 0) return;

        int offset = 0;
        // Check for optional "CONSTRAINT <name>" prefix
        if (sqlite_vtab_arg_internal::ci_starts_with(p, len, "CONSTRAINT", 10)) {
            offset = 10;
            while (offset < len && (p[offset] == ' ' || p[offset] == '\t')) ++offset;
            int name_start = offset;
            while (offset < len && p[offset] != ' ' && p[offset] != '\t' && p[offset] != '(') ++offset;
            m_name = SqliteStringView(p + name_start, offset - name_start);
            while (offset < len && (p[offset] == ' ' || p[offset] == '\t')) ++offset;
        }

        const char* rem = p + offset;
        int rem_len = len - offset;

        // Classify constraint kind
        if (sqlite_vtab_arg_internal::ci_starts_with(rem, rem_len, "PRIMARY KEY", 11) ||
            sqlite_vtab_arg_internal::ci_starts_with(rem, rem_len, "PRIMARY_KEY", 11)) {
            m_kind = SqliteVTabConstraintKind::PrimaryKey;
        } else if (sqlite_vtab_arg_internal::ci_starts_with(rem, rem_len, "UNIQUE", 6)) {
            m_kind = SqliteVTabConstraintKind::Unique;
        } else if (sqlite_vtab_arg_internal::ci_starts_with(rem, rem_len, "CHECK", 5)) {
            m_kind = SqliteVTabConstraintKind::Check;
        } else if (sqlite_vtab_arg_internal::ci_starts_with(rem, rem_len, "FOREIGN KEY", 11)) {
            m_kind = SqliteVTabConstraintKind::ForeignKey;
        }

        // Extract content inside first '(' and matching ')'
        int paren_open = -1;
        int paren_close = -1;
        for (int i = 0; i < len; ++i) {
            if (p[i] == '(' && paren_open < 0) paren_open = i;
            if (p[i] == ')') paren_close = i;
        }

        if (paren_open >= 0 && paren_close > paren_open) {
            m_columns_raw = sqlite_vtab_arg_internal::trim_slice(p, paren_open + 1, paren_close);
        }
    }

    inline SqliteVTabConstraintKind kind() const noexcept { return m_kind; }
    inline bool is_primary_key() const noexcept { return m_kind == SqliteVTabConstraintKind::PrimaryKey; }
    inline bool is_unique()      const noexcept { return m_kind == SqliteVTabConstraintKind::Unique; }
    inline bool is_check()       const noexcept { return m_kind == SqliteVTabConstraintKind::Check; }
    inline bool is_foreign_key() const noexcept { return m_kind == SqliteVTabConstraintKind::ForeignKey; }

    inline SqliteStringView name()        const noexcept { return m_name; }
    inline SqliteStringView columns_raw() const noexcept { return m_columns_raw; }
    inline SqliteStringView full_def()    const noexcept { return m_full; }

    /**
     * @brief Iterates over each comma-separated column name in the composite constraint.
     * @tparam Fn Callable: void(SqliteStringView column_name).
     */
    template <typename Fn>
    inline void for_each_column_name(Fn fn) const {
        if (m_columns_raw.empty()) return;
        const char* p = m_columns_raw.data();
        int len = m_columns_raw.length();
        int start = 0;
        for (int i = 0; i <= len; ++i) {
            if (i == len || p[i] == ',') {
                SqliteStringView col = sqlite_vtab_arg_internal::trim_slice(p, start, i);
                if (!col.empty()) {
                    fn(col);
                }
                start = i + 1;
            }
        }
    }

    /** Returns the number of columns participating in this composite constraint. */
    inline int column_count() const noexcept {
        int count = 0;
        for_each_column_name([&](SqliteStringView) {
            ++count;
        });
        return count;
    }

    /** Returns true if col_name matches any column in this constraint (case-insensitive). */
    inline bool has_column(SqliteStringView col_name) const noexcept {
        bool found = false;
        for_each_column_name([&](SqliteStringView c) {
            if (c.length() == col_name.length() &&
                sqlite3_strnicmp(c.data(), col_name.data(), c.length()) == 0) {
                found = true;
            }
        });
        return found;
    }
};

// =============================================================================
// 5. SqliteVTabArg — Tagged Union (Param | Column | TableConstraint | Empty)
// =============================================================================

/**
 * @class SqliteVTabArg
 * @brief Tagged union representing one CREATE VIRTUAL TABLE argument.
 *
 * Discriminant:
 *   - is_param()      → access via param()
 *   - is_column()     → access via column()
 *   - is_constraint() → access via constraint()
 *   - is_empty()      → null/uninitialized
 */
class SqliteVTabArg {
public:
    enum class Kind : unsigned char {
        Empty      = 0,
        Param      = 1,  ///< key=value argument
        Column     = 2,  ///< Column declaration
        Constraint = 3,  ///< Table-level constraint (PRIMARY KEY (a,b), etc.)
    };

private:
    Kind m_kind;

    union {
        SqliteVTabParam      m_param;
        SqliteVTabColumn     m_column;
        SqliteVTabConstraint m_constraint;
    };

    static inline bool is_table_constraint_prefix(const char* base, int len) {
        if (sqlite_vtab_arg_internal::ci_starts_with(base, len, "PRIMARY KEY", 11) ||
            sqlite_vtab_arg_internal::ci_starts_with(base, len, "PRIMARY_KEY", 11) ||
            sqlite_vtab_arg_internal::ci_starts_with(base, len, "UNIQUE", 6) ||
            sqlite_vtab_arg_internal::ci_starts_with(base, len, "CHECK", 5) ||
            sqlite_vtab_arg_internal::ci_starts_with(base, len, "FOREIGN KEY", 11) ||
            sqlite_vtab_arg_internal::ci_starts_with(base, len, "CONSTRAINT", 10)) {
            return true;
        }
        return false;
    }

public:
    inline SqliteVTabArg() noexcept : m_kind(Kind::Empty), m_param() {}

    static inline SqliteVTabArg make_param(SqliteStringView key, SqliteStringView value) {
        SqliteVTabArg a;
        a.m_kind = Kind::Param;
        a.m_param = SqliteVTabParam(key, value);
        return a;
    }

    static inline SqliteVTabArg make_column(SqliteStringView full_def) {
        SqliteVTabArg a;
        a.m_kind = Kind::Column;
        a.m_column = SqliteVTabColumn(full_def);
        return a;
    }

    static inline SqliteVTabArg make_constraint(SqliteStringView full_def) {
        SqliteVTabArg a;
        a.m_kind = Kind::Constraint;
        a.m_constraint = SqliteVTabConstraint(full_def);
        return a;
    }

    /**
     * @brief Parses a raw argv string into Param, Constraint, or Column.
     */
    inline explicit SqliteVTabArg(const char* raw)
        : m_kind(Kind::Empty), m_param()
    {
        if (!raw) return;

        int total = SqliteStringUtil::sqlite_strlen(raw);
        int start = 0;
        while (start < total && (raw[start] == ' ' || raw[start] == '\t' || raw[start] == '\r' || raw[start] == '\n')) ++start;
        const char* base = raw + start;
        int len = total - start;
        if (len == 0) return;

        // Step 1: Detect Table Constraint (e.g. PRIMARY KEY(a, b), UNIQUE(a, b), CONSTRAINT ...)
        if (is_table_constraint_prefix(base, len)) {
            m_kind = Kind::Constraint;
            m_constraint = SqliteVTabConstraint(sqlite_vtab_arg_internal::trim_slice(base, 0, len));
            return;
        }

        // Step 2: Detect Key=Value Parameter (e.g. capacity=1024, mode=strict)
        // '=' must appear before any space or open-parenthesis
        int eq_pos = -1;
        for (int i = 0; i < len; ++i) {
            if (base[i] == '(' || base[i] == ' ' || base[i] == '\t') break;
            if (base[i] == '=') {
                eq_pos = i;
                break;
            }
        }

        if (eq_pos > 0 && eq_pos < len) {
            m_kind = Kind::Param;
            m_param = SqliteVTabParam(
                sqlite_vtab_arg_internal::trim_slice(base, 0, eq_pos),
                sqlite_vtab_arg_internal::trim_slice(base, eq_pos + 1, len)
            );
            return;
        }

        // Step 3: Default to Column Definition (e.g. "id INTEGER PRIMARY KEY")
        m_kind = Kind::Column;
        m_column = SqliteVTabColumn(sqlite_vtab_arg_internal::trim_slice(base, 0, len));
    }

    SqliteVTabArg(const SqliteVTabArg& o) noexcept : m_kind(o.m_kind) {
        switch (m_kind) {
            case Kind::Param:      m_param = o.m_param; break;
            case Kind::Column:     m_column = o.m_column; break;
            case Kind::Constraint: m_constraint = o.m_constraint; break;
            default:               m_param = SqliteVTabParam(); break;
        }
    }

    SqliteVTabArg& operator=(const SqliteVTabArg& o) noexcept {
        if (this != &o) {
            m_kind = o.m_kind;
            switch (m_kind) {
                case Kind::Param:      m_param = o.m_param; break;
                case Kind::Column:     m_column = o.m_column; break;
                case Kind::Constraint: m_constraint = o.m_constraint; break;
                default:               m_param = SqliteVTabParam(); break;
            }
        }
        return *this;
    }

    ~SqliteVTabArg() = default;

    inline Kind kind()          const noexcept { return m_kind; }
    inline bool is_empty()      const noexcept { return m_kind == Kind::Empty; }
    inline bool is_param()      const noexcept { return m_kind == Kind::Param; }
    inline bool is_column()     const noexcept { return m_kind == Kind::Column; }
    inline bool is_constraint() const noexcept { return m_kind == Kind::Constraint; }

    inline const SqliteVTabParam&      param()      const noexcept { return m_param; }
    inline       SqliteVTabParam&      param()            noexcept { return m_param; }

    inline const SqliteVTabColumn&     column()     const noexcept { return m_column; }
    inline       SqliteVTabColumn&     column()           noexcept { return m_column; }

    inline const SqliteVTabConstraint& constraint() const noexcept { return m_constraint; }
    inline       SqliteVTabConstraint& constraint()       noexcept { return m_constraint; }

    inline bool key_is(SqliteStringView k) const noexcept {
        return m_kind == Kind::Param && (m_param.key() == k);
    }
};

// =============================================================================
// 6. SqliteVTabArgs — Batch parser for virtual table CREATE arguments
// =============================================================================

/**
 * @class SqliteVTabArgs
 * @brief Comprehensive batch parser for all CREATE VIRTUAL TABLE arguments.
 *
 * Wraps argv[user_start..argc-1] with typed parameter accessors, column iteration,
 * and unified single/multi-column primary key extraction.
 */
class SqliteVTabArgs {
private:
    const char* const* m_argv;
    int                m_argc;
    int                m_start;

    inline SqliteVTabParam find_param(SqliteStringView name) const {
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.key_is(name)) return a.param();
        }
        return SqliteVTabParam();
    }

public:
    inline SqliteVTabArgs(int argc, const char* const* argv, int user_start = 3)
        : m_argv(argv), m_argc(argc), m_start(user_start) {}

    template <typename ConnectArgs>
    inline explicit SqliteVTabArgs(const ConnectArgs& args, int user_start = 3)
        : m_argv(args.argv()), m_argc(args.size()), m_start(user_start) {}

    // -- Parameter Presence & Typed Getters -----------------------------------

    inline bool has(SqliteStringView name) const {
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.key_is(name)) return true;
        }
        return false;
    }

    inline int get_int(SqliteStringView name, int def = 0) const {
        int v = def; find_param(name).as_int(v); return v;
    }

    inline long long get_long(SqliteStringView name, long long def = 0LL) const {
        long long v = def; find_param(name).as_long(v); return v;
    }

    inline double get_double(SqliteStringView name, double def = 0.0) const {
        double v = def; find_param(name).as_double(v); return v;
    }

    inline size_t get_size(SqliteStringView name, size_t def = 0) const {
        size_t v = def; find_param(name).as_size(v); return v;
    }

    inline bool get_bool(SqliteStringView name, bool def = false) const {
        bool v = def; find_param(name).as_bool(v); return v;
    }

    inline SqliteStringView get_str(SqliteStringView name,
                                    SqliteStringView def = SqliteStringView("", 0)) const {
        SqliteVTabParam p = find_param(name);
        return p.key().empty() ? def : p.as_str();
    }

    inline SqliteValueOwned get_value(SqliteStringView name,
                                      SqliteValueOwned def = SqliteValueOwned()) const {
        SqliteVTabParam p = find_param(name);
        return p.key().empty() ? def : p.as_value();
    }

    // -- Iteration Callbacks -------------------------------------------------

    /** Calls fn(const SqliteVTabArg&) for all arguments. */
    template <typename Fn>
    inline void for_each(Fn fn) const {
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (!a.is_empty()) fn(a);
        }
    }

    /** Calls fn(const SqliteVTabParam&) for key=value parameters only. */
    template <typename Fn>
    inline void for_each_param(Fn fn) const {
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_param()) fn(a.param());
        }
    }

    /** Calls fn(const SqliteVTabColumn&) for column definitions only. */
    template <typename Fn>
    inline void for_each_column(Fn fn) const {
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column()) fn(a.column());
        }
    }

    /** Calls fn(const SqliteVTabColumn&, int col_index) for column definitions with 0-based column index. */
    template <typename Fn>
    inline void for_each_column_indexed(Fn fn) const {
        int idx = 0;
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column()) {
                fn(a.column(), idx++);
            }
        }
    }

    /** Calls fn(const SqliteVTabConstraint&) for table constraints only. */
    template <typename Fn>
    inline void for_each_constraint(Fn fn) const {
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_constraint()) fn(a.constraint());
        }
    }

    // -- Index & Counting Helpers --------------------------------------------

    /** Returns total number of declared SQL columns. */
    inline int column_count() const noexcept {
        int count = 0;
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column()) ++count;
        }
        return count;
    }

    /** Returns total number of key=value engine parameters. */
    inline int param_count() const noexcept {
        int count = 0;
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_param()) ++count;
        }
        return count;
    }

    /** Returns total number of table-level constraints. */
    inline int constraint_count() const noexcept {
        int count = 0;
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_constraint()) ++count;
        }
        return count;
    }

    /**
     * @brief Finds the 0-based column index by column name.
     * @return 0-based column index, or -1 if not found.
     */
    inline int column_index(SqliteStringView col_name) const noexcept {
        int idx = 0;
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column()) {
                SqliteStringView name = a.column().name();
                if (name.length() == col_name.length() &&
                    sqlite3_strnicmp(name.data(), col_name.data(), col_name.length()) == 0) {
                    return idx;
                }
                ++idx;
            }
        }
        return -1;
    }

    /**
     * @brief Retrieves the i-th declared column (0-based).
     */
    inline SqliteVTabColumn column_at(int target_idx) const noexcept {
        int idx = 0;
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column()) {
                if (idx == target_idx) return a.column();
                ++idx;
            }
        }
        return SqliteVTabColumn();
    }

    // -- Primary Key Inspection (Single Column + Multi-Column Composite PK) --

    /**
     * @brief Iterates over every Primary Key column name across the virtual table.
     *
     * Automatically aggregates:
     *   1. Inline column primary keys (e.g. "id INTEGER PRIMARY KEY")
     *   2. Table-level composite primary keys (e.g. "PRIMARY KEY (user_id, device_id)")
     *
     * @tparam Fn Callable: void(SqliteStringView pk_col_name).
     */
    template <typename Fn>
    inline void for_each_primary_key(Fn fn) const {
        // 1. Scan inline column primary keys
        for_each_column([&](const SqliteVTabColumn& col) {
            if (col.primary_key()) {
                fn(col.name());
            }
        });

        // 2. Scan table-level primary key constraints
        for_each_constraint([&](const SqliteVTabConstraint& c) {
            if (c.is_primary_key()) {
                c.for_each_column_name([&](SqliteStringView col_name) {
                    fn(col_name);
                });
            }
        });
    }

    /** Returns total number of Primary Key columns across both inline and table constraints. */
    inline int primary_key_count() const noexcept {
        int count = 0;
        for_each_primary_key([&](SqliteStringView) {
            ++count;
        });
        return count;
    }

    /** Returns true if the virtual table schema specifies 2 or more Primary Key columns. */
    inline bool is_composite_primary_key() const noexcept {
        return primary_key_count() > 1;
    }

    /** Returns true if the given column name participates in the primary key. */
    inline bool is_primary_key_column(SqliteStringView col_name) const noexcept {
        bool match = false;
        for_each_primary_key([&](SqliteStringView pk_col) {
            if (pk_col.length() == col_name.length() &&
                sqlite3_strnicmp(pk_col.data(), col_name.data(), pk_col.length()) == 0) {
                match = true;
            }
        });
        return match;
    }

    // -- Raw Access ----------------------------------------------------------

    inline int         argc()      const noexcept { return m_argc; }
    inline int         user_argc() const noexcept {
        return (m_argc > m_start) ? m_argc - m_start : 0;
    }
    inline SqliteVTabArg operator[](int i) const {
        int idx = m_start + i;
        return (idx >= 0 && idx < m_argc) ? SqliteVTabArg(m_argv[idx])
                                           : SqliteVTabArg();
    }
};

// =============================================================================
// 7. SqliteVTabParamSchema — Declarative schema with fluent binding
// =============================================================================

/**
 * @class SqliteVTabParamSchema
 * @brief Pre-declares expected vtab parameters, then parses them in a single pass.
 */
class SqliteVTabParamSchema {
public:
    static constexpr int MAX_PARAMS = 16;

private:
    enum SlotType : unsigned char {
        None = 0,
        Int,
        Long,
        Double,
        Size,
        Bool,
        Str,
        EnumStr,
        ValueOwned,
    };

    struct Slot {
        SqliteStringView   name;
        SlotType           type;
        const char* const* enum_values;
        int                enum_count;
        union {
            int*              as_int;
            long long*        as_long;
            double*           as_double;
            size_t*           as_size;
            bool*             as_bool;
            SqliteStringView* as_str;
            SqliteValueOwned* as_value_owned;
            void*             as_void;
        } out;
    };

    Slot m_slots[MAX_PARAMS];
    int  m_count;

    inline Slot* alloc_slot(SqliteStringView name, SlotType type) {
        if (m_count >= MAX_PARAMS) return nullptr;
        Slot& s        = m_slots[m_count++];
        s.name         = name;
        s.type         = type;
        s.enum_values  = nullptr;
        s.enum_count   = 0;
        s.out.as_void  = nullptr;
        return &s;
    }

public:
    inline SqliteVTabParamSchema() : m_count(0) {
        for (int i = 0; i < MAX_PARAMS; ++i) {
            m_slots[i].type        = None;
            m_slots[i].enum_values = nullptr;
            m_slots[i].enum_count  = 0;
            m_slots[i].out.as_void = nullptr;
        }
    }

    inline SqliteVTabParamSchema& bind_int(SqliteStringView name, int* out) {
        if (Slot* s = alloc_slot(name, Int)) s->out.as_int = out;
        return *this;
    }

    inline SqliteVTabParamSchema& bind_long(SqliteStringView name, long long* out) {
        if (Slot* s = alloc_slot(name, Long)) s->out.as_long = out;
        return *this;
    }

    inline SqliteVTabParamSchema& bind_double(SqliteStringView name, double* out) {
        if (Slot* s = alloc_slot(name, Double)) s->out.as_double = out;
        return *this;
    }

    inline SqliteVTabParamSchema& bind_size(SqliteStringView name, size_t* out) {
        if (Slot* s = alloc_slot(name, Size)) s->out.as_size = out;
        return *this;
    }

    inline SqliteVTabParamSchema& bind_bool(SqliteStringView name, bool* out) {
        if (Slot* s = alloc_slot(name, Bool)) s->out.as_bool = out;
        return *this;
    }

    inline SqliteVTabParamSchema& bind_str(SqliteStringView name, SqliteStringView* out) {
        if (Slot* s = alloc_slot(name, Str)) s->out.as_str = out;
        return *this;
    }

    inline SqliteVTabParamSchema& bind_value(SqliteStringView name, SqliteValueOwned* out) {
        if (Slot* s = alloc_slot(name, ValueOwned)) s->out.as_value_owned = out;
        return *this;
    }

    inline SqliteVTabParamSchema& bind_enum(SqliteStringView   name,
                                            const char* const* values,
                                            int                count,
                                            int*               out) {
        if (Slot* s = alloc_slot(name, EnumStr)) {
            s->enum_values = values;
            s->enum_count  = count;
            s->out.as_int  = out;
        }
        return *this;
    }

    inline int parse(const SqliteVTabArgs& args) const {
        int matched = 0;
        args.for_each_param([&](const SqliteVTabParam& p) {
            for (int i = 0; i < m_count; ++i) {
                const Slot& s = m_slots[i];
                if (s.type == None || !(p.key() == s.name)) continue;

                bool ok = false;
                switch (s.type) {
                case Int:
                    ok = s.out.as_int    && p.as_int   (*s.out.as_int);    break;
                case Long:
                    ok = s.out.as_long   && p.as_long  (*s.out.as_long);   break;
                case Double:
                    ok = s.out.as_double && p.as_double(*s.out.as_double); break;
                case Size:
                    ok = s.out.as_size   && p.as_size  (*s.out.as_size);   break;
                case Bool:
                    ok = s.out.as_bool   && p.as_bool  (*s.out.as_bool);   break;
                case Str:
                    if (s.out.as_str) { *s.out.as_str = p.as_str(); ok = true; }
                    break;
                case ValueOwned:
                    if (s.out.as_value_owned) { *s.out.as_value_owned = p.as_value(); ok = true; }
                    break;

                case EnumStr: {
                    if (!s.out.as_int) break;
                    SqliteStringView val = p.value();
                    int found = -1;
                    for (int j = 0; j < s.enum_count; ++j) {
                        if (sqlite_vtab_arg_internal::ci_equals(val, s.enum_values[j])) {
                            found = j;
                            break;
                        }
                    }
                    *s.out.as_int = found;
                    ok = true;
                    break;
                }

                default: break;
                }
                if (ok) ++matched;
                break;
            }
        });
        return matched;
    }

    inline int binding_count() const noexcept { return m_count; }
};

#undef SQLITE_VTAB_ARG_WARN_PUSH
#undef SQLITE_VTAB_ARG_WARN_POP

#endif // SQLITE3_VTAB_ARG_HPP
