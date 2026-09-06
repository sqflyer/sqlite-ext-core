#ifndef SQLITE3_VTAB_ARG_HPP
#define SQLITE3_VTAB_ARG_HPP

/**
 * @file sqlite3_vtab_arg.hpp
 * @brief High-performance, zero-allocation argument parser for SQLite CREATE VIRTUAL TABLE.
 *
 * ── Overview ─────────────────────────────────────────────────────────────────
 *
 * Virtual table modules receive user-supplied arguments as raw C-strings in
 * `argv[3..argc-1]`. Arguments come in four distinct forms:
 *
 *   1. Param (key=value)        — Engine parameters, e.g. "capacity=1024", "ttl=30",
 *                                 "mode=strict", "ratio=0.75", "enabled=true"
 *   2. Column (col def)         — SQL column declarations, e.g. "event_name TEXT",
 *                                 "id INTEGER PRIMARY KEY", "score REAL NOT NULL"
 *   3. TableConstraint          — Table-level constraints, e.g. "PRIMARY KEY (a, b)",
 *                                 "CONSTRAINT pk PRIMARY KEY (user_id, device_id)",
 *                                 "UNIQUE (email, org_id)", "CHECK (age >= 0)"
 *   4. TableOption              — Table-level options, e.g. "WITHOUT ROWID"
 *
 * ── Type Hierarchy ───────────────────────────────────────────────────────────
 *
 *   - SqliteVTabParam       : View into a key=value parameter; typed as_*() accessors.
 *   - SqliteVTabColumn      : View into a column declaration; affinity, data_type, and constraints.
 *   - SqliteVTabConstraint  : View into a table constraint (composite PK, UNIQUE, CHECK, FK).
 *   - SqliteVTabArg         : Tagged union representing any single CREATE argument.
 *   - SqliteVTabArgs        : Batch parser wrapping argv[3..]; typed lookups, column indexers,
 *                             composite PK aggregators, and DDL synthesizers.
 *   - SqliteVTabParamSchema : Declarative parameter schema with fluent binding, type conversions,
 *                             enum mapping, and unknown parameter validation.
 *
 * ── Key Architectural Properties ─────────────────────────────────────────────
 *
 *   - Zero Heap Allocations : All string views (SqliteStringView) reference SQLite-owned
 *                             argv memory slices without allocating runtime buffers.
 *   - Freestanding Ready    : Compatible with `-nostdlib++` environments.
 *   - Quote Resilience      : Transparently strips and normalizes outer quotes (`''`, `""`,
 *                             `` ` ``, `[]`) across keys, column names, and values.
 *   - Case-Insensitive      : Matches keywords, identifiers, and enum strings using SQLite's
 *                             official collation routines (`sqlite3_strnicmp`).
 *
 * ── Usage Example ────────────────────────────────────────────────────────────
 *
 *   @code
 *   static int connect(SqliteConnectArgs& args) {
 *       SqliteVTabArgs vargs(args);
 *
 *       // 1. Declarative parameter validation & parsing:
 *       int cap = 1024;
 *       SqliteStringView mode;
 *       SqliteVTabParamSchema schema;
 *       schema.bind_int("capacity", &cap)
 *             .bind_str("mode", &mode);
 *
 *       SqliteStringView unknown_key;
 *       if (schema.validate(vargs, &unknown_key) > 0) {
 *           return args.set_error("unrecognized parameter '%s'", unknown_key.data());
 *       }
 *       schema.parse(vargs);
 *
 *       // 2. Synthesize clean DDL for sqlite3_declare_vtab (stripping engine params):
 *       char ddl[512];
 *       vargs.format_declare_vtab_sql(ddl, sizeof(ddl));
 *       int rc = sqlite3_declare_vtab(args.db(), ddl);
 *       if (rc != SQLITE_OK) return rc;
 *
 *       // 3. Inspect composite or single primary keys:
 *       if (vargs.is_composite_primary_key()) {
 *           vargs.for_each_primary_key([](SqliteStringView pk_col) { ... });
 *       }
 *       return SQLITE_OK;
 *   }
 *   @endcode
 */

#include <sqlite3.h>
#include <stdio.h>
#include "sqlite3_value.hpp"

// =============================================================================
// Compiler Warning Suppression for Scalar Formatting / Parsing
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
// Helper String Scanning & Parsing Primitives
// =============================================================================

/** @cond INTERNAL */
namespace sqlite_vtab_arg_internal {

    /// Checks if a character is ASCII whitespace.
    static inline bool is_space(char c) noexcept {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    /// Advances pos past any contiguous whitespace characters.
    static inline void skip_spaces(const char* s, int len, int& pos) noexcept {
        while (pos < len && is_space(s[pos])) ++pos;
    }

    /// Trims leading and trailing whitespace from a slice of a string.
    static inline SqliteStringView trim_slice(const char* base, int lo, int hi) noexcept {
        while (lo < hi && is_space(base[lo])) ++lo;
        while (hi > lo && is_space(base[hi - 1])) --hi;
        return (lo < hi) ? SqliteStringView(base + lo, hi - lo) : SqliteStringView("", 0);
    }

    /// Checks if a character is a valid SQL identifier character ([a-zA-Z0-9_]).
    static inline bool is_ident_char(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    /// Checks if a character is an opening quote character ('', "", ``, [).
    static inline bool is_quote_open(char c) noexcept {
        return c == '\'' || c == '"' || c == '`' || c == '[';
    }

    /// Returns the matching closing quote character for an opening quote.
    static inline char matching_quote_close(char open_c) noexcept {
        return (open_c == '[') ? ']' : open_c;
    }

    /// Advances pos past a quoted literal (e.g. 'str', "col", `ident`, [col]), handling escaped doubled quotes.
    static inline void skip_quoted(const char* s, int len, int& pos) noexcept {
        if (pos >= len || !is_quote_open(s[pos])) return;
        char close_c = matching_quote_close(s[pos++]);
        while (pos < len) {
            if (s[pos] == close_c) {
                if (pos + 1 < len && s[pos + 1] == close_c) {
                    pos += 2; // skip escaped doubled quote
                } else {
                    ++pos;
                    break;
                }
            } else {
                ++pos;
            }
        }
    }

    /// Advances pos past a parenthesized expression (depth-aware, quote-aware).
    static inline void skip_parenthesized(const char* s, int len, int& pos) noexcept {
        if (pos >= len || s[pos] != '(') return;
        int depth = 1;
        ++pos;
        while (pos < len && depth > 0) {
            if (is_quote_open(s[pos])) {
                skip_quoted(s, len, pos);
            } else if (s[pos] == '(') {
                ++depth;
                ++pos;
            } else if (s[pos] == ')') {
                --depth;
                ++pos;
            } else {
                ++pos;
            }
        }
    }

    /// Checks if haystack contains needle (case-insensitive).
    static inline bool ci_contains(const char* haystack, int hlen, const char* needle, int nlen) noexcept {
        if (nlen > hlen) return false;
        for (int i = 0; i <= hlen - nlen; ++i) {
            if (sqlite3_strnicmp(haystack + i, needle, nlen) == 0) return true;
        }
        return false;
    }

    /// Checks if haystack contains keyword sequences (ignoring quoted strings and variable whitespace).
    static inline bool ci_has_kw_sequence(const char* haystack, int hlen, const char* kw1, const char* kw2 = nullptr) noexcept {
        if (!haystack || hlen == 0) return false;
        int kw1_len = SqliteStringUtil::sqlite_strlen(kw1);
        int kw2_len = kw2 ? SqliteStringUtil::sqlite_strlen(kw2) : 0;

        int i = 0;
        while (i < hlen) {
            if (is_quote_open(haystack[i])) {
                skip_quoted(haystack, hlen, i);
                continue;
            }

            if (i + kw1_len <= hlen && sqlite3_strnicmp(haystack + i, kw1, kw1_len) == 0) {
                bool prev_ok = (i == 0 || !is_ident_char(haystack[i - 1]));
                bool next_ok = (i + kw1_len == hlen || !is_ident_char(haystack[i + kw1_len]));
                if (prev_ok && next_ok) {
                    if (!kw2) return true;
                    int j = i + kw1_len;
                    skip_spaces(haystack, hlen, j);
                    if (j + kw2_len <= hlen && sqlite3_strnicmp(haystack + j, kw2, kw2_len) == 0) {
                        bool n2_ok = (j + kw2_len == hlen || !is_ident_char(haystack[j + kw2_len]));
                        if (n2_ok) return true;
                    }
                }
            }
            ++i;
        }
        return false;
    }

    /// Checks if a string starts with a keyword (case-insensitive, respecting identifier boundaries).
    static inline bool ci_starts_with_kw(const char* str, int len, const char* prefix, int plen) noexcept {
        if (len < plen) return false;
        if (sqlite3_strnicmp(str, prefix, plen) != 0) return false;
        if (len == plen) return true;
        return !is_ident_char(str[plen]);
    }

    /// Strips wrapping quotes ('', "", ``, []) from a string view.
    static inline SqliteStringView strip_quotes(SqliteStringView s) noexcept {
        if (s.length() >= 2) {
            char f = s.data()[0];
            char l = s.data()[s.length() - 1];
            if ((f == '"' && l == '"') || (f == '`' && l == '`') || (f == '[' && l == ']') || (f == '\'' && l == '\'')) {
                return SqliteStringView(s.data() + 1, s.length() - 2);
            }
        }
        return s;
    }

    /// Extracts base column identifier from expressions like "col ASC", "[name] DESC", "x COLLATE NOCASE".
    static inline SqliteStringView extract_base_column_name(SqliteStringView raw) noexcept {
        const char* p = raw.data();
        int len = raw.length();
        int start = 0;
        skip_spaces(p, len, start);
        if (start >= len) return SqliteStringView("", 0);

        int end = start;
        if (is_quote_open(p[start])) {
            skip_quoted(p, len, end);
        } else {
            while (end < len && !is_space(p[end]) && p[end] != ',' && p[end] != ')') ++end;
        }
        return strip_quotes(SqliteStringView(p + start, end - start));
    }

    /// Checks if a string view equals a null-terminated string (case-insensitive).
    static inline bool ci_equals(SqliteStringView a, const char* b) noexcept {
        if (!b) return false;
        int b_len = SqliteStringUtil::sqlite_strlen(b);
        return a.length() == b_len && sqlite3_strnicmp(a.data(), b, b_len) == 0;
    }

} // namespace sqlite_vtab_arg_internal
/** @endcond */

// =============================================================================
// 1. SqliteVTabParam — Key=Value Argument View
// =============================================================================

/**
 * @struct SqliteVTabParam
 * @brief Zero-allocation view into a key=value parameter with typed conversion accessors.
 *
 * Both key and value are represented as @ref SqliteStringView slices pointing directly
 * into the SQLite-managed `argv` buffers.
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

    /// The value portion as a raw string view, e.g. "1024" or "'strict'".
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

    /** Parses value as a 64-bit signed integer (sqlite3_int64). Returns true on success. */
    inline bool as_int64(sqlite3_int64& out) const {
        return as_long(out);
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

    /** Parses value as a single-precision float ("%f"). Returns true on success. */
    inline bool as_float(float& out) const {
        if (m_value.empty()) return false;
        float parsed = 0.0f;
        SQLITE_VTAB_ARG_WARN_PUSH
        int rc = sscanf(m_value.data(), "%f", &parsed);
        SQLITE_VTAB_ARG_WARN_POP
        if (rc == 1) { out = parsed; return true; }
        return false;
    }

    /** Parses value as an unsigned 32-bit integer ("%u"). Returns true on success. */
    inline bool as_uint(unsigned int& out) const {
        if (m_value.empty()) return false;
        unsigned int parsed = 0;
        SQLITE_VTAB_ARG_WARN_PUSH
        int rc = sscanf(m_value.data(), "%u", &parsed);
        SQLITE_VTAB_ARG_WARN_POP
        if (rc == 1) { out = parsed; return true; }
        return false;
    }

    /**
     * @brief Matches parameter value against an array of valid enum string values (case-insensitive & quote-stripped).
     * @param values Array of string options, e.g. {"fast", "strict", "lenient"}.
     * @param count Number of elements in values.
     * @param def Default index to return if not matched or parameter value is empty.
     * @return 0-based index of matched enum value, or `def` if not matched.
     */
    inline int as_enum(const char* const* values, int count, int def = -1) const noexcept {
        if (m_value.empty() || !values || count <= 0) return def;
        SqliteStringView unquoted = as_unquoted_str();
        for (int i = 0; i < count; ++i) {
            if (values[i] && sqlite_vtab_arg_internal::ci_equals(unquoted, values[i])) {
                return i;
            }
        }
        return def;
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
     * Rules:
     *   - Numeric non-zero → true, zero → false.
     *   - Case-insensitive true tokens  : "true", "yes", "on", "1"
     *   - Case-insensitive false tokens : "false", "no", "off", "0"
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

    /** Returns the raw value string view. */
    inline SqliteStringView as_str() const noexcept { return m_value; }

    /** Returns the value with wrapping quotes ('', "", ``, []) stripped. */
    inline SqliteStringView as_unquoted_str() const noexcept {
        return sqlite_vtab_arg_internal::strip_quotes(m_value);
    }

    /**
     * @brief Parses the parameter value into a dynamically typed @ref SqliteValueOwned.
     *
     * Uses automatic SQL literal inference:
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
 * @brief SQLite's 5 official type affinities determined from column definitions.
 *
 *   - Rule 1: Contains "INT"                       → Integer (SQLITE_AFF_INTEGER)
 *   - Rule 2: Contains "CHAR", "CLOB", "TEXT"      → Text    (SQLITE_AFF_TEXT)
 *   - Rule 3: Contains "BLOB" or is empty          → Blob    (SQLITE_AFF_BLOB)
 *   - Rule 4: Contains "REAL", "FLOA", "DOUB"      → Real    (SQLITE_AFF_REAL)
 *   - Rule 5: Otherwise                            → Numeric (SQLITE_AFF_NUMERIC)
 */
enum class SqliteVTabColAffinity : unsigned char {
    Unknown = 0,  ///< Not yet determined / default-constructed
    Integer,      ///< Contains "INT"                        (SQLITE_AFF_INTEGER)
    Text,         ///< Contains "CHAR", "CLOB", "TEXT"       (SQLITE_AFF_TEXT)
    Blob,         ///< Contains "BLOB" or empty              (SQLITE_AFF_BLOB)
    Real,         ///< Contains "REAL", "FLOA", "DOUB"       (SQLITE_AFF_REAL)
    Numeric,      ///< Everything else                       (SQLITE_AFF_NUMERIC)
};

/**
 * @enum SqliteVTabColFlags
 * @brief Bitmask of inline column constraints parsed from column definitions.
 */
enum SqliteVTabColFlags : unsigned int {
    ColFlag_None        = 0,
    ColFlag_NotNull     = 1u << 0,  ///< "NOT NULL" constraint
    ColFlag_PrimaryKey  = 1u << 1,  ///< Inline "PRIMARY KEY" constraint
    ColFlag_Unique      = 1u << 2,  ///< "UNIQUE" constraint
    ColFlag_AutoIncr    = 1u << 3,  ///< "AUTOINCREMENT" clause
    ColFlag_Hidden      = 1u << 4,  ///< "HIDDEN" virtual table column clause
    ColFlag_Generated   = 1u << 5,  ///< "GENERATED ALWAYS AS" or "AS (...)" clause
    ColFlag_Stored      = 1u << 6,  ///< "STORED" generated column
    ColFlag_Virtual     = 1u << 7,  ///< "VIRTUAL" generated column
};

// =============================================================================
// 3. SqliteVTabColumn — Column Definition View
// =============================================================================

/**
 * @struct SqliteVTabColumn
 * @brief View into a column declaration argument split into column name, declared type, and constraints.
 *
 * Example: "id INTEGER PRIMARY KEY NOT NULL"
 *   - name()       → "id"
 *   - data_type()  → "INTEGER"
 *   - definition() → "INTEGER PRIMARY KEY NOT NULL"
 *   - full_def()   → "id INTEGER PRIMARY KEY NOT NULL"
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
        if (sqlite_vtab_arg_internal::is_quote_open(p[0])) {
            sqlite_vtab_arg_internal::skip_quoted(p, len, name_end);
        } else {
            SQLITE_VTAB_ARG_WARN_PUSH
            sscanf(p, "%*[^ \t\r\n]%n", &name_end);
            SQLITE_VTAB_ARG_WARN_POP
        }

        m_name = SqliteStringView(p, name_end);

        int def_start = name_end;
        sqlite_vtab_arg_internal::skip_spaces(p, len, def_start);
        m_definition = SqliteStringView(p + def_start, len - def_start);
    }

    inline SqliteVTabColumn(SqliteStringView name, SqliteStringView definition) noexcept
        : m_name(name), m_definition(definition) {}

    /// Column name, e.g. "event_name" or "[my col]".
    inline SqliteStringView name()          const noexcept { return m_name; }

    /// Column name with wrapping quotes ('', "", ``, []) stripped, e.g. "my col".
    inline SqliteStringView unquoted_name() const noexcept {
        return sqlite_vtab_arg_internal::strip_quotes(m_name);
    }

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

        if (sqlite_vtab_arg_internal::ci_has_kw_sequence(d, n, "NOT", "NULL"))     f |= ColFlag_NotNull;
        if (sqlite_vtab_arg_internal::ci_has_kw_sequence(d, n, "PRIMARY", "KEY"))  f |= ColFlag_PrimaryKey;
        if (sqlite_vtab_arg_internal::ci_has_kw_sequence(d, n, "UNIQUE"))          f |= ColFlag_Unique;
        if (sqlite_vtab_arg_internal::ci_has_kw_sequence(d, n, "AUTOINCREMENT"))   f |= ColFlag_AutoIncr;
        if (sqlite_vtab_arg_internal::ci_has_kw_sequence(d, n, "HIDDEN"))          f |= ColFlag_Hidden;
        if (sqlite_vtab_arg_internal::ci_has_kw_sequence(d, n, "GENERATED") ||
            sqlite_vtab_arg_internal::ci_has_kw_sequence(d, n, "AS"))              f |= ColFlag_Generated;
        if (sqlite_vtab_arg_internal::ci_has_kw_sequence(d, n, "STORED"))          f |= ColFlag_Stored;
        if (sqlite_vtab_arg_internal::ci_has_kw_sequence(d, n, "VIRTUAL"))         f |= ColFlag_Virtual;
        return f;
    }

    inline bool not_null()             const noexcept { return (flags() & ColFlag_NotNull)   != 0; }
    inline bool primary_key()          const noexcept { return (flags() & ColFlag_PrimaryKey)!= 0; }
    inline bool is_unique()            const noexcept { return (flags() & ColFlag_Unique)    != 0; }
    inline bool is_autoincrement()     const noexcept { return (flags() & ColFlag_AutoIncr)  != 0; }
    inline bool is_hidden()            const noexcept { return (flags() & ColFlag_Hidden)    != 0; }
    inline bool is_stored()            const noexcept { return (flags() & ColFlag_Stored)    != 0; }
    inline bool is_virtual_generated() const noexcept { return (flags() & ColFlag_Virtual)   != 0; }

    /**
     * @brief Returns true if this column is an alias for the 64-bit signed integer rowid
     * (i.e. declared as INTEGER PRIMARY KEY).
     */
    inline bool is_rowid_alias() const noexcept {
        return primary_key() && affinity() == SqliteVTabColAffinity::Integer;
    }

    /**
     * @brief Extracts the expression from a GENERATED ALWAYS AS (expr) or AS (expr) column definition.
     * Returns empty SqliteStringView if not a generated column.
     */
    inline SqliteStringView generated_expression() const noexcept {
        const char* d = m_definition.data();
        int         n = m_definition.length();
        if (n < 4) return SqliteStringView("", 0);

        int i = 0;
        while (i < n) {
            if (sqlite_vtab_arg_internal::is_quote_open(d[i])) {
                sqlite_vtab_arg_internal::skip_quoted(d, n, i);
                continue;
            }
            if (sqlite_vtab_arg_internal::ci_starts_with_kw(d + i, n - i, "AS", 2)) {
                int start = i + 2;
                sqlite_vtab_arg_internal::skip_spaces(d, n, start);
                if (start < n && d[start] == '(') {
                    int end = start;
                    sqlite_vtab_arg_internal::skip_parenthesized(d, n, end);
                    if (end > start + 1) {
                        return sqlite_vtab_arg_internal::trim_slice(d, start + 1, end - 1);
                    }
                }
            }
            ++i;
        }
        return SqliteStringView("", 0);
    }

    inline bool is_generated() const noexcept {
        return !generated_expression().empty() || (flags() & ColFlag_Generated) != 0;
    }

    /**
     * @brief Extracts the COLLATE sequence name if specified (e.g. "NOCASE", "BINARY", "RTRIM").
     * Returns empty SqliteStringView if no collation is specified.
     */
    inline SqliteStringView collation() const noexcept {
        const char* d = m_definition.data();
        int         n = m_definition.length();
        if (n < 7) return SqliteStringView("", 0);

        int i = 0;
        while (i <= n - 7) {
            if (sqlite_vtab_arg_internal::is_quote_open(d[i])) {
                sqlite_vtab_arg_internal::skip_quoted(d, n, i);
            } else if (sqlite3_strnicmp(d + i, "COLLATE", 7) == 0) {
                bool prev_ok = (i == 0 || !sqlite_vtab_arg_internal::is_ident_char(d[i - 1]));
                bool next_ok = (i + 7 == n || !sqlite_vtab_arg_internal::is_ident_char(d[i + 7]));
                if (prev_ok && next_ok) {
                    int start = i + 7;
                    sqlite_vtab_arg_internal::skip_spaces(d, n, start);
                    int end = start;
                    if (start < n && sqlite_vtab_arg_internal::is_quote_open(d[start])) {
                        sqlite_vtab_arg_internal::skip_quoted(d, n, end);
                    } else {
                        while (end < n && !sqlite_vtab_arg_internal::is_space(d[end]) && d[end] != ',' && d[end] != ')') ++end;
                    }
                    return SqliteStringView(d + start, end - start);
                }
                ++i;
            } else {
                ++i;
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

        int i = 0;
        while (i <= n - 7) {
            if (sqlite_vtab_arg_internal::is_quote_open(d[i])) {
                sqlite_vtab_arg_internal::skip_quoted(d, n, i);
            } else if (sqlite3_strnicmp(d + i, "DEFAULT", 7) == 0) {
                bool prev_ok = (i == 0 || !sqlite_vtab_arg_internal::is_ident_char(d[i - 1]));
                bool next_ok = (i + 7 == n || !sqlite_vtab_arg_internal::is_ident_char(d[i + 7]));
                if (prev_ok && next_ok) {
                    int start = i + 7;
                    sqlite_vtab_arg_internal::skip_spaces(d, n, start);
                    if (start >= n) return SqliteStringView("", 0);

                    int end = start;
                    if (d[start] == '\'') {
                        sqlite_vtab_arg_internal::skip_quoted(d, n, end);
                    } else if (d[start] == '(') {
                        sqlite_vtab_arg_internal::skip_parenthesized(d, n, end);
                    } else {
                        while (end < n && !sqlite_vtab_arg_internal::is_space(d[end]) && d[end] != ',' && d[end] != ')') ++end;
                    }
                    return SqliteStringView(d + start, end - start);
                }
                ++i;
            } else {
                ++i;
            }
        }
        return SqliteStringView("", 0);
    }

    inline bool has_default() const noexcept {
        return !default_value().empty();
    }

    /**
     * @brief Extracts the declared column data type (e.g. "INTEGER", "VARCHAR(255)", "DECIMAL(10, 2)").
     * Returns empty SqliteStringView if the column is untyped.
     */
    inline SqliteStringView data_type() const noexcept {
        const char* d = m_definition.data();
        int         n = m_definition.length();
        if (n == 0) return SqliteStringView("", 0);

        auto is_kw = [&](int pos, const char* kw, int kw_len) -> bool {
            if (pos + kw_len > n) return false;
            if (sqlite3_strnicmp(d + pos, kw, kw_len) != 0) return false;
            bool prev_ok = (pos == 0 || !sqlite_vtab_arg_internal::is_ident_char(d[pos - 1]));
            bool next_ok = (pos + kw_len == n || !sqlite_vtab_arg_internal::is_ident_char(d[pos + kw_len]));
            return prev_ok && next_ok;
        };

        auto is_constraint_at = [&](int pos) -> bool {
            return is_kw(pos, "PRIMARY KEY", 11) || is_kw(pos, "PRIMARY_KEY", 11) ||
                   is_kw(pos, "NOT NULL", 8)     || is_kw(pos, "NULL", 4) ||
                   is_kw(pos, "UNIQUE", 6)       || is_kw(pos, "CHECK", 5) ||
                   is_kw(pos, "DEFAULT", 7)      || is_kw(pos, "COLLATE", 7) ||
                   is_kw(pos, "GENERATED", 9)    || is_kw(pos, "AS", 2) ||
                   is_kw(pos, "HIDDEN", 6)       || is_kw(pos, "REFERENCES", 10) ||
                   is_kw(pos, "CONSTRAINT", 10)  || is_kw(pos, "AUTOINCREMENT", 13);
        };

        int i = 0;
        while (i < n) {
            sqlite_vtab_arg_internal::skip_spaces(d, n, i);
            if (i >= n) break;

            if (is_constraint_at(i)) {
                return sqlite_vtab_arg_internal::trim_slice(d, 0, i);
            }

            if (d[i] == '(') {
                sqlite_vtab_arg_internal::skip_parenthesized(d, n, i);
            } else if (sqlite_vtab_arg_internal::is_quote_open(d[i])) {
                sqlite_vtab_arg_internal::skip_quoted(d, n, i);
            } else {
                while (i < n && !sqlite_vtab_arg_internal::is_space(d[i]) && d[i] != '(' && d[i] != ',' && d[i] != ')') {
                    ++i;
                }
            }
        }

        return sqlite_vtab_arg_internal::trim_slice(d, 0, n);
    }
};

// =============================================================================
// 4. SqliteVTabConstraint — Table-Level Constraint View
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
 * @brief Zero-allocation view into a table-level constraint (composite PK, UNIQUE, CHECK, FK).
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
        sqlite_vtab_arg_internal::skip_spaces(p, len, offset);

        // Check for optional "CONSTRAINT <name>" prefix
        if (sqlite_vtab_arg_internal::ci_starts_with_kw(p + offset, len - offset, "CONSTRAINT", 10)) {
            offset += 10;
            sqlite_vtab_arg_internal::skip_spaces(p, len, offset);
            int name_start = offset;
            if (offset < len && sqlite_vtab_arg_internal::is_quote_open(p[offset])) {
                sqlite_vtab_arg_internal::skip_quoted(p, len, offset);
            } else {
                while (offset < len && !sqlite_vtab_arg_internal::is_space(p[offset]) && p[offset] != '(') ++offset;
            }
            m_name = sqlite_vtab_arg_internal::strip_quotes(SqliteStringView(p + name_start, offset - name_start));
            sqlite_vtab_arg_internal::skip_spaces(p, len, offset);
        }

        const char* rem = p + offset;
        int rem_len = len - offset;

        // Classify constraint kind
        if (sqlite_vtab_arg_internal::ci_starts_with_kw(rem, rem_len, "PRIMARY KEY", 11) ||
            sqlite_vtab_arg_internal::ci_starts_with_kw(rem, rem_len, "PRIMARY_KEY", 11)) {
            m_kind = SqliteVTabConstraintKind::PrimaryKey;
        } else if (sqlite_vtab_arg_internal::ci_starts_with_kw(rem, rem_len, "UNIQUE", 6)) {
            m_kind = SqliteVTabConstraintKind::Unique;
        } else if (sqlite_vtab_arg_internal::ci_starts_with_kw(rem, rem_len, "CHECK", 5)) {
            m_kind = SqliteVTabConstraintKind::Check;
        } else if (sqlite_vtab_arg_internal::ci_starts_with_kw(rem, rem_len, "FOREIGN KEY", 11)) {
            m_kind = SqliteVTabConstraintKind::ForeignKey;
        }

        // Extract content inside first '(' and matching ')'
        int paren_open = -1;
        int paren_close = -1;
        int paren_level = 0;
        for (int i = offset; i < len; ++i) {
            if (sqlite_vtab_arg_internal::is_quote_open(p[i])) {
                sqlite_vtab_arg_internal::skip_quoted(p, len, i);
                --i;
            } else if (p[i] == '(') {
                if (paren_level == 0) paren_open = i;
                ++paren_level;
            } else if (p[i] == ')') {
                --paren_level;
                if (paren_level == 0 && paren_open >= 0) {
                    paren_close = i;
                    break;
                }
            }
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
    inline bool             has_name()    const noexcept { return !m_name.empty(); }
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
        int i = 0;
        while (i <= len) {
            if (i < len && sqlite_vtab_arg_internal::is_quote_open(p[i])) {
                sqlite_vtab_arg_internal::skip_quoted(p, len, i);
                continue;
            }
            if (i == len || p[i] == ',') {
                SqliteStringView raw_col = sqlite_vtab_arg_internal::trim_slice(p, start, i);
                SqliteStringView col = sqlite_vtab_arg_internal::extract_base_column_name(raw_col);
                if (!col.empty()) {
                    fn(col);
                }
                start = i + 1;
            }
            ++i;
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
// 5. SqliteVTabArg — Tagged Union (Param | Column | TableConstraint | Option | Empty)
// =============================================================================

/**
 * @class SqliteVTabArg
 * @brief Tagged union representing one parsed CREATE VIRTUAL TABLE argument.
 */
class SqliteVTabArg {
public:
    enum class Kind : unsigned char {
        Empty      = 0,
        Param      = 1,  ///< Key=value argument
        Column     = 2,  ///< Column declaration
        Constraint = 3,  ///< Table-level constraint (PRIMARY KEY (a,b), etc.)
        Option     = 4,  ///< Table-level option (WITHOUT ROWID, etc.)
    };

private:
    Kind m_kind;

    union {
        SqliteVTabParam      m_param;
        SqliteVTabColumn     m_column;
        SqliteVTabConstraint m_constraint;
    };

    static inline bool is_table_constraint_prefix(const char* base, int len) noexcept {
        return sqlite_vtab_arg_internal::ci_starts_with_kw(base, len, "PRIMARY KEY", 11) ||
               sqlite_vtab_arg_internal::ci_starts_with_kw(base, len, "PRIMARY_KEY", 11) ||
               sqlite_vtab_arg_internal::ci_starts_with_kw(base, len, "UNIQUE", 6) ||
               sqlite_vtab_arg_internal::ci_starts_with_kw(base, len, "CHECK", 5) ||
               sqlite_vtab_arg_internal::ci_starts_with_kw(base, len, "FOREIGN KEY", 11) ||
               sqlite_vtab_arg_internal::ci_starts_with_kw(base, len, "CONSTRAINT", 10);
    }

public:
    inline SqliteVTabArg() noexcept : m_kind(Kind::Empty), m_param() {}

    static inline SqliteVTabArg make_param(SqliteStringView key, SqliteStringView value) noexcept {
        SqliteVTabArg a;
        a.m_kind = Kind::Param;
        a.m_param = SqliteVTabParam(key, value);
        return a;
    }

    static inline SqliteVTabArg make_column(SqliteStringView full_def) noexcept {
        SqliteVTabArg a;
        a.m_kind = Kind::Column;
        a.m_column = SqliteVTabColumn(full_def);
        return a;
    }

    static inline SqliteVTabArg make_constraint(SqliteStringView full_def) noexcept {
        SqliteVTabArg a;
        a.m_kind = Kind::Constraint;
        a.m_constraint = SqliteVTabConstraint(full_def);
        return a;
    }

    /**
     * @brief Parses a raw argv string into Param, Constraint, Option, or Column.
     */
    inline explicit SqliteVTabArg(const char* raw) noexcept
        : m_kind(Kind::Empty), m_param()
    {
        if (!raw) return;

        int total = SqliteStringUtil::sqlite_strlen(raw);
        int start = 0;
        sqlite_vtab_arg_internal::skip_spaces(raw, total, start);
        const char* base = raw + start;
        int len = total - start;
        if (len == 0) return;

        // Step 1: Detect Table Constraint (e.g. PRIMARY KEY(a, b), UNIQUE(a, b), CONSTRAINT ...)
        if (is_table_constraint_prefix(base, len)) {
            m_kind = Kind::Constraint;
            m_constraint = SqliteVTabConstraint(sqlite_vtab_arg_internal::trim_slice(base, 0, len));
            return;
        }

        // Step 2: Detect Table Options (e.g. WITHOUT ROWID)
        if (sqlite_vtab_arg_internal::ci_has_kw_sequence(base, len, "WITHOUT", "ROWID")) {
            m_kind = Kind::Option;
            return;
        }

        // Step 3: Detect Key=Value Parameter (e.g. capacity=1024, mode=strict, ttl = 60)
        int eq_pos = -1;
        int i = 0;
        while (i < len) {
            char c = base[i];
            if (sqlite_vtab_arg_internal::is_quote_open(c)) {
                sqlite_vtab_arg_internal::skip_quoted(base, len, i);
            } else if (c == '(') {
                break;
            } else if (c == '=') {
                SqliteStringView key = sqlite_vtab_arg_internal::trim_slice(base, 0, i);
                bool valid_key = true;
                for (int k = 0; k < key.length(); ++k) {
                    if (sqlite_vtab_arg_internal::is_space(key.data()[k])) {
                        valid_key = false;
                        break;
                    }
                }
                if (valid_key) {
                    eq_pos = i;
                }
                break;
            } else {
                ++i;
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

        // Step 4: Default to Column Definition (e.g. "id INTEGER PRIMARY KEY")
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
    inline bool is_option()        const noexcept { return m_kind == Kind::Option; }
    inline bool is_without_rowid() const noexcept { return m_kind == Kind::Option; }

    inline const SqliteVTabParam&      param()      const noexcept { return m_param; }
    inline       SqliteVTabParam&      param()            noexcept { return m_param; }

    inline const SqliteVTabColumn&     column()     const noexcept { return m_column; }
    inline       SqliteVTabColumn&     column()           noexcept { return m_column; }

    inline const SqliteVTabConstraint& constraint() const noexcept { return m_constraint; }
    inline       SqliteVTabConstraint& constraint()       noexcept { return m_constraint; }

    /// Checks if this is a parameter argument matching the given key (quote-stripped & case-insensitive).
    inline bool key_is(SqliteStringView k) const noexcept {
        if (m_kind != Kind::Param) return false;
        SqliteStringView k1 = sqlite_vtab_arg_internal::strip_quotes(m_param.key());
        SqliteStringView k2 = sqlite_vtab_arg_internal::strip_quotes(k);
        return k1.length() == k2.length() &&
               sqlite3_strnicmp(k1.data(), k2.data(), k1.length()) == 0;
    }
};

// =============================================================================
// 6. SqliteVTabArgs — Batch Parser for Virtual Table CREATE Arguments
// =============================================================================

/**
 * @class SqliteVTabArgs
 * @brief Comprehensive batch parser for CREATE VIRTUAL TABLE arguments.
 *
 * Provides typed parameter accessors, column iteration, column indexing,
 * primary key aggregation across both inline and table constraints, and DDL synthesis.
 */
class SqliteVTabArgs {
private:
    const char* const* m_argv;
    int                m_argc;
    int                m_start;

public:
    inline SqliteVTabArgs(int argc, const char* const* argv, int user_start = 3) noexcept
        : m_argv(argv), m_argc(argc), m_start(user_start) {}

    template <typename ConnectArgs>
    inline explicit SqliteVTabArgs(const ConnectArgs& args, int user_start = 3) noexcept
        : m_argv(args.argv()), m_argc(args.size()), m_start(user_start) {}

    // -- Parameter Presence & Typed Getters -----------------------------------

    /// Finds a parameter by name (case-insensitive & quote-stripped). Returns empty param if not found.
    inline SqliteVTabParam find_param(SqliteStringView name) const noexcept {
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.key_is(name)) return a.param();
        }
        return SqliteVTabParam();
    }

    /// Checks if a parameter with the given name exists (case-insensitive & quote-stripped).
    inline bool has(SqliteStringView name) const noexcept {
        return !find_param(name).key().empty();
    }

    /// Retrieves an integer parameter, returning `def` if not found or invalid.
    inline int get_int(SqliteStringView name, int def = 0) const {
        int v = def; find_param(name).as_int(v); return v;
    }

    /// Retrieves a 64-bit integer parameter, returning `def` if not found or invalid.
    inline long long get_long(SqliteStringView name, long long def = 0LL) const {
        long long v = def; find_param(name).as_long(v); return v;
    }

    /// Retrieves a 64-bit integer parameter (sqlite3_int64), returning `def` if not found or invalid.
    inline sqlite3_int64 get_int64(SqliteStringView name, sqlite3_int64 def = 0) const {
        sqlite3_int64 v = def; find_param(name).as_int64(v); return v;
    }

    /// Retrieves a double parameter, returning `def` if not found or invalid.
    inline double get_double(SqliteStringView name, double def = 0.0) const {
        double v = def; find_param(name).as_double(v); return v;
    }

    /// Retrieves a single-precision float parameter, returning `def` if not found or invalid.
    inline float get_float(SqliteStringView name, float def = 0.0f) const {
        float v = def; find_param(name).as_float(v); return v;
    }

    /// Retrieves an unsigned 32-bit integer parameter, returning `def` if not found or invalid.
    inline unsigned int get_uint(SqliteStringView name, unsigned int def = 0) const {
        unsigned int v = def; find_param(name).as_uint(v); return v;
    }

    /// Retrieves an enum parameter matched against valid string options, returning `def` if not found or invalid.
    inline int get_enum(SqliteStringView name, const char* const* values, int count, int def = -1) const noexcept {
        return find_param(name).as_enum(values, count, def);
    }

    /// Retrieves a size_t parameter, returning `def` if not found or invalid.
    inline size_t get_size(SqliteStringView name, size_t def = 0) const {
        size_t v = def; find_param(name).as_size(v); return v;
    }

    /// Retrieves a boolean parameter, returning `def` if not found or invalid.
    inline bool get_bool(SqliteStringView name, bool def = false) const {
        bool v = def; find_param(name).as_bool(v); return v;
    }

    /// Retrieves a string parameter, returning `def` if not found.
    inline SqliteStringView get_str(SqliteStringView name,
                                    SqliteStringView def = SqliteStringView("", 0)) const {
        SqliteVTabParam p = find_param(name);
        return p.key().empty() ? def : p.as_str();
    }

    /// Retrieves a string parameter with outer quotes stripped, returning `def` if not found.
    inline SqliteStringView get_unquoted_str(SqliteStringView name,
                                             SqliteStringView def = SqliteStringView("", 0)) const {
        SqliteVTabParam p = find_param(name);
        return p.key().empty() ? def : p.as_unquoted_str();
    }

    /// Retrieves a dynamic SQLite value parameter, returning `def` if not found.
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
        SqliteStringView target = sqlite_vtab_arg_internal::strip_quotes(col_name);
        int idx = 0;
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column()) {
                SqliteStringView name = sqlite_vtab_arg_internal::strip_quotes(a.column().name());
                if (name.length() == target.length() &&
                    sqlite3_strnicmp(name.data(), target.data(), name.length()) == 0) {
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

    /// Checks if a column with the given name exists (case-insensitive & quote-stripped).
    inline bool has_column(SqliteStringView col_name) const noexcept {
        return column_index(col_name) >= 0;
    }

    /// Finds a column by name (case-insensitive & quote-stripped). Returns empty column if not found.
    inline SqliteVTabColumn find_column(SqliteStringView col_name) const noexcept {
        int idx = column_index(col_name);
        return (idx >= 0) ? column_at(idx) : SqliteVTabColumn();
    }

    /// Checks if the table definition contains one or more HIDDEN columns.
    inline bool has_hidden_columns() const noexcept {
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column() && a.column().is_hidden()) return true;
        }
        return false;
    }

    /// Returns the number of columns declared with the HIDDEN modifier.
    inline int hidden_column_count() const noexcept {
        int count = 0;
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column() && a.column().is_hidden()) ++count;
        }
        return count;
    }

    /** Calls fn(const SqliteVTabColumn&) for HIDDEN column definitions only. */
    template <typename Fn>
    inline void for_each_hidden_column(Fn fn) const {
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column() && a.column().is_hidden()) fn(a.column());
        }
    }

    /**
     * @brief Finds the 0-based column index of the INTEGER PRIMARY KEY rowid alias column.
     * @return 0-based column index, or -1 if no rowid alias exists or table is WITHOUT ROWID.
     */
    inline int rowid_alias_column_index() const noexcept {
        if (is_without_rowid()) return -1;
        if (is_composite_primary_key()) return -1;
        int idx = 0;
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column()) {
                if (a.column().is_rowid_alias()) return idx;
                ++idx;
            }
        }
        return -1;
    }

    /**
     * @brief Returns the column name of the INTEGER PRIMARY KEY rowid alias column, or empty string view.
     */
    inline SqliteStringView rowid_alias_column_name() const noexcept {
        int idx = rowid_alias_column_index();
        return (idx >= 0) ? column_at(idx).name() : SqliteStringView("", 0);
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

    /**
     * @brief Iterates over every Primary Key column with its 0-based column index.
     *
     * Automatically aggregates:
     *   1. Inline column primary keys (e.g. "id INTEGER PRIMARY KEY")
     *   2. Table-level composite primary keys (e.g. "PRIMARY KEY (user_id, device_id)")
     *
     * @tparam Fn Callable: void(SqliteStringView pk_col_name, int col_idx).
     */
    template <typename Fn>
    inline void for_each_primary_key_indexed(Fn fn) const {
        // 1. Scan inline column primary keys
        for_each_column_indexed([&](const SqliteVTabColumn& col, int idx) {
            if (col.primary_key()) {
                fn(col.name(), idx);
            }
        });

        // 2. Scan table-level primary key constraints
        for_each_constraint([&](const SqliteVTabConstraint& c) {
            if (c.is_primary_key()) {
                c.for_each_column_name([&](SqliteStringView col_name) {
                    int c_idx = column_index(col_name);
                    if (c_idx >= 0) {
                        fn(col_name, c_idx);
                    }
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

    /// Returns true if the table defines at least one primary key column (inline or table constraint).
    inline bool has_primary_key() const noexcept {
        return primary_key_count() > 0;
    }

    /** Returns true if the given column name participates in the primary key. */
    inline bool is_primary_key_column(SqliteStringView col_name) const noexcept {
        bool match = false;
        SqliteStringView target = sqlite_vtab_arg_internal::strip_quotes(col_name);
        for_each_primary_key([&](SqliteStringView pk_col) {
            SqliteStringView pk = sqlite_vtab_arg_internal::strip_quotes(pk_col);
            if (pk.length() == target.length() &&
                sqlite3_strnicmp(pk.data(), target.data(), pk.length()) == 0) {
                match = true;
            }
        });
        return match;
    }

    // -- Raw & System Metadata Access ---------------------------------------

    /// Returns the module name (argv[0]), e.g. "memkv".
    inline SqliteStringView module_name() const noexcept {
        return (m_argc > 0 && m_argv && m_argv[0]) ? SqliteStringView(m_argv[0]) : SqliteStringView("", 0);
    }

    /// Returns the database name (argv[1]), e.g. "main" or "temp".
    inline SqliteStringView db_name() const noexcept {
        return (m_argc > 1 && m_argv && m_argv[1]) ? SqliteStringView(m_argv[1]) : SqliteStringView("", 0);
    }

    /// Returns the virtual table name (argv[2]), e.g. "my_table".
    inline SqliteStringView table_name() const noexcept {
        return (m_argc > 2 && m_argv && m_argv[2]) ? SqliteStringView(m_argv[2]) : SqliteStringView("", 0);
    }

    /// Returns true if "WITHOUT ROWID" was specified anywhere in the arguments.
    inline bool is_without_rowid() const noexcept {
        for (int i = m_start; i < m_argc; ++i) {
            const char* raw = m_argv[i];
            if (!raw) continue;
            int len = SqliteStringUtil::sqlite_strlen(raw);
            int s = 0;
            sqlite_vtab_arg_internal::skip_spaces(raw, len, s);
            if (sqlite_vtab_arg_internal::ci_has_kw_sequence(raw + s, len - s, "WITHOUT", "ROWID")) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Synthesizes a clean CREATE TABLE DDL string for sqlite3_declare_vtab(),
     * containing column definitions, optional extra columns (e.g. hidden columns for TVFs),
     * and table constraints (omitting engine parameters).
     *
     * Format: "CREATE TABLE <tbl_name>(<cols...>, <extra_cols...>, <constraints...>) [WITHOUT ROWID]"
     *
     * @param out Output buffer pointer.
     * @param out_cap Capacity of the output buffer.
     * @param custom_tbl_name Optional table name override (defaults to table_name() or "x").
     * @param extra_cols Optional array of extra column definitions (e.g. hidden argument columns).
     * @param extra_col_count Number of entries in extra_cols.
     * @return Number of characters formatted (excluding null terminator).
     */
    inline int format_declare_vtab_sql(char* out, size_t out_cap,
                                       SqliteStringView custom_tbl_name = SqliteStringView("", 0),
                                       const char* const* extra_cols = nullptr,
                                       int extra_col_count = 0) const noexcept {
        SqliteStringView tbl = custom_tbl_name.empty() ? table_name() : custom_tbl_name;
        if (tbl.empty()) tbl = SqliteStringView("x", 1);

        size_t written = 0;
        auto append_str = [&](const char* s, size_t len) {
            for (size_t i = 0; i < len; ++i) {
                if (out && written + 1 < out_cap) {
                    out[written] = s[i];
                }
                ++written;
            }
        };

        append_str("CREATE TABLE ", 13);
        append_str(tbl.data(), tbl.length());
        append_str("(", 1);

        bool first = true;
        // 1. Emit column definitions from argv (preserving any user-declared HIDDEN columns)
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_column()) {
                if (!first) append_str(", ", 2);
                first = false;
                SqliteStringView def = a.column().full_def();
                append_str(def.data(), def.length());
            }
        }

        // 2. Emit any extra programmatic columns (e.g. TVF hidden argument columns)
        if (extra_cols && extra_col_count > 0) {
            for (int i = 0; i < extra_col_count; ++i) {
                if (extra_cols[i]) {
                    if (!first) append_str(", ", 2);
                    first = false;
                    append_str(extra_cols[i], SqliteStringUtil::sqlite_strlen(extra_cols[i]));
                }
            }
        }

        // 3. Emit table constraints
        for (int i = m_start; i < m_argc; ++i) {
            SqliteVTabArg a(m_argv[i]);
            if (a.is_constraint()) {
                if (!first) append_str(", ", 2);
                first = false;
                SqliteStringView def = a.constraint().full_def();
                append_str(def.data(), def.length());
            }
        }

        append_str(")", 1);
        if (is_without_rowid()) {
            append_str(" WITHOUT ROWID", 14);
        }

        if (out && out_cap > 0) {
            size_t null_pos = (written < out_cap) ? written : (out_cap - 1);
            out[null_pos] = '\0';
        }
        return static_cast<int>(written);
    }

    /// Returns the total number of arguments (including system arguments).
    inline int argc() const noexcept { return m_argc; }
    
    /// Returns the number of user-provided arguments.
    inline int user_argc() const noexcept {
        return (m_argc > m_start) ? m_argc - m_start : 0;
    }
    
    /// Retrieves the i-th user-provided argument (0 <= i < user_argc()).
    inline SqliteVTabArg operator[](int i) const noexcept {
        if (i < 0 || i >= user_argc() || !m_argv) return SqliteVTabArg();
        int idx = m_start + i;
        return (idx >= 0 && idx < m_argc && m_argv[idx]) ? SqliteVTabArg(m_argv[idx])
                                                         : SqliteVTabArg();
    }
};

// =============================================================================
// 7. SqliteVTabParamSchema — Declarative Schema with Fluent Binding
// =============================================================================

/**
 * @class SqliteVTabParamSchema
 * @brief Pre-declares expected virtual table parameters and parses/validates them in a single pass.
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
        Float,
        UInt,
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
            float*            as_float;
            unsigned int*     as_uint;
            size_t*           as_size;
            bool*             as_bool;
            SqliteStringView* as_str;
            SqliteValueOwned* as_value_owned;
            void*             as_void;
        } out;
    };

    Slot m_slots[MAX_PARAMS];
    int  m_count;

    inline Slot* alloc_slot(SqliteStringView name, SlotType type) noexcept {
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
    inline SqliteVTabParamSchema() noexcept : m_count(0) {
        for (int i = 0; i < MAX_PARAMS; ++i) {
            m_slots[i].type        = None;
            m_slots[i].enum_values = nullptr;
            m_slots[i].enum_count  = 0;
            m_slots[i].out.as_void = nullptr;
        }
    }

    /// Binds an integer parameter to the given output pointer.
    inline SqliteVTabParamSchema& bind_int(SqliteStringView name, int* out) noexcept {
        if (Slot* s = alloc_slot(name, Int)) s->out.as_int = out;
        return *this;
    }

    /// Binds a 64-bit integer parameter to the given output pointer.
    inline SqliteVTabParamSchema& bind_long(SqliteStringView name, long long* out) noexcept {
        if (Slot* s = alloc_slot(name, Long)) s->out.as_long = out;
        return *this;
    }

    /// Binds a 64-bit integer parameter (sqlite3_int64) to the given output pointer.
    inline SqliteVTabParamSchema& bind_int64(SqliteStringView name, sqlite3_int64* out) noexcept {
        return bind_long(name, out);
    }

    /// Binds a double-precision float parameter to the given output pointer.
    inline SqliteVTabParamSchema& bind_double(SqliteStringView name, double* out) noexcept {
        if (Slot* s = alloc_slot(name, Double)) s->out.as_double = out;
        return *this;
    }

    /// Binds a single-precision float parameter to the given output pointer.
    inline SqliteVTabParamSchema& bind_float(SqliteStringView name, float* out) noexcept {
        if (Slot* s = alloc_slot(name, Float)) s->out.as_float = out;
        return *this;
    }

    /// Binds an unsigned 32-bit integer parameter to the given output pointer.
    inline SqliteVTabParamSchema& bind_uint(SqliteStringView name, unsigned int* out) noexcept {
        if (Slot* s = alloc_slot(name, UInt)) s->out.as_uint = out;
        return *this;
    }

    /// Binds a size_t parameter to the given output pointer.
    inline SqliteVTabParamSchema& bind_size(SqliteStringView name, size_t* out) noexcept {
        if (Slot* s = alloc_slot(name, Size)) s->out.as_size = out;
        return *this;
    }

    /// Binds a boolean parameter to the given output pointer.
    inline SqliteVTabParamSchema& bind_bool(SqliteStringView name, bool* out) noexcept {
        if (Slot* s = alloc_slot(name, Bool)) s->out.as_bool = out;
        return *this;
    }

    /// Binds a string parameter to the given output pointer.
    inline SqliteVTabParamSchema& bind_str(SqliteStringView name, SqliteStringView* out) noexcept {
        if (Slot* s = alloc_slot(name, Str)) s->out.as_str = out;
        return *this;
    }

    /// Binds a dynamic SQLite value parameter to the given output pointer.
    inline SqliteVTabParamSchema& bind_value(SqliteStringView name, SqliteValueOwned* out) noexcept {
        if (Slot* s = alloc_slot(name, ValueOwned)) s->out.as_value_owned = out;
        return *this;
    }

    /// Binds a string enum parameter, mapping matched values to a 0-based index.
    inline SqliteVTabParamSchema& bind_enum(SqliteStringView   name,
                                            const char* const* values,
                                            int                count,
                                            int*               out) noexcept {
        if (Slot* s = alloc_slot(name, EnumStr)) {
            s->enum_values = values;
            s->enum_count  = count;
            s->out.as_int  = out;
        }
        return *this;
    }

    static inline bool is_key_match(SqliteStringView k1, SqliteStringView k2) noexcept {
        SqliteStringView s1 = sqlite_vtab_arg_internal::strip_quotes(k1);
        SqliteStringView s2 = sqlite_vtab_arg_internal::strip_quotes(k2);
        return s1.length() == s2.length() &&
               sqlite3_strnicmp(s1.data(), s2.data(), s1.length()) == 0;
    }

    /**
     * @brief Parses the given arguments against the bound schema.
     * @return The number of parameters successfully matched and parsed.
     */
    inline int parse(const SqliteVTabArgs& args) const {
        int matched = 0;
        args.for_each_param([&](const SqliteVTabParam& p) {
            for (int i = 0; i < m_count; ++i) {
                const Slot& s = m_slots[i];
                if (s.type == None || !is_key_match(p.key(), s.name)) continue;

                bool ok = false;
                switch (s.type) {
                case Int:
                    ok = s.out.as_int    && p.as_int   (*s.out.as_int);    break;
                case Long:
                    ok = s.out.as_long   && p.as_long  (*s.out.as_long);   break;
                case Double:
                    ok = s.out.as_double && p.as_double(*s.out.as_double); break;
                case Float:
                    ok = s.out.as_float  && p.as_float (*s.out.as_float);  break;
                case UInt:
                    ok = s.out.as_uint   && p.as_uint  (*s.out.as_uint);   break;
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
                    SqliteStringView val = p.as_unquoted_str();
                    int found = -1;
                    for (int j = 0; j < s.enum_count; ++j) {
                        if (sqlite_vtab_arg_internal::ci_equals(val, s.enum_values[j])) {
                            found = j;
                            break;
                        }
                    }
                    *s.out.as_int = found;
                    ok = (found >= 0);
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

    /// Checks if a parameter name is registered in this schema.
    inline bool has_binding(SqliteStringView name) const noexcept {
        for (int i = 0; i < m_count; ++i) {
            if (m_slots[i].type != None && is_key_match(m_slots[i].name, name)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Iterates over any parameters in the arguments that are NOT recognized by this schema.
     * @tparam Fn Callable: void(const SqliteVTabParam& unknown_param).
     */
    template <typename Fn>
    inline void for_each_unknown(const SqliteVTabArgs& args, Fn fn) const {
        args.for_each_param([&](const SqliteVTabParam& p) {
            if (!has_binding(p.key())) {
                fn(p);
            }
        });
    }

    /**
     * @brief Validates arguments against this schema, returning the count of unknown parameters.
     * @param args The parsed virtual table arguments.
     * @param first_unknown_out Optional pointer to store the key of the first unrecognized parameter.
     * @return Number of unrecognized parameters (0 on success).
     */
    inline int validate(const SqliteVTabArgs& args, SqliteStringView* first_unknown_out = nullptr) const {
        int unknown_count = 0;
        for_each_unknown(args, [&](const SqliteVTabParam& p) {
            if (unknown_count == 0 && first_unknown_out) {
                *first_unknown_out = p.key();
            }
            ++unknown_count;
        });
        return unknown_count;
    }

    /// Returns the number of registered parameter bindings.
    inline int binding_count() const noexcept { return m_count; }
};

#undef SQLITE_VTAB_ARG_WARN_PUSH
#undef SQLITE_VTAB_ARG_WARN_POP

#endif // SQLITE3_VTAB_ARG_HPP
