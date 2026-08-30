#ifndef SQLITE3_ROW_HPP
#define SQLITE3_ROW_HPP

#include <sqlite3.h>
#include "sqlite3_value.hpp"
#include "sqlite3_allocator.hpp"

// Forward declaration of classes for tight integration
class SqliteStatement;
class SqliteRowOwnedWrapper;
template <size_t N, typename Enable> class SqliteValueTuple;
template <size_t N, typename Enable> class SqliteValueVec;

// ============================================================================
// Source Type Macros for SqliteRowView
// ============================================================================
#define SQLITE_ROW_SOURCE_STMT        0  /**< Backed by sqlite3_stmt* column values */
#define SQLITE_ROW_SOURCE_ARGV        1  /**< Backed by sqlite3_value** (UDF args / vtab) */
#define SQLITE_ROW_SOURCE_VIEW_ARRAY  2  /**< Backed by const SqliteValueView* array */
#define SQLITE_ROW_SOURCE_EMPTY       3  /**< Empty row view (0 columns) */

typedef uint8_t SqliteRowSourceType;

// ============================================================================
// Macro Helpers for Complete Relational Operators
// ============================================================================

#ifndef SQLITE_DERIVE_RELATIONAL_OPS
#define SQLITE_DERIVE_RELATIONAL_OPS(OtherType) \
    inline bool operator!=(const OtherType& other) const noexcept { return !(*this == other); } \
    inline bool operator<=(const OtherType& other) const noexcept { return !(other < *this); } \
    inline bool operator>(const OtherType& other)  const noexcept { return other < *this; } \
    inline bool operator>=(const OtherType& other) const noexcept { return !(*this < other); }
#endif

#ifndef SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS
#define SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(ContainerType) \
    inline bool operator==(const ContainerType& other) const noexcept { \
        if (this->size() != other.size()) return false; \
        int sz = this->size(); \
        for (int i = 0; i < sz; ++i) { \
            if (!((*this)[i] == other[i])) return false; \
        } \
        return true; \
    } \
    inline bool operator!=(const ContainerType& other) const noexcept { return !(*this == other); } \
    inline bool operator<(const ContainerType& other) const noexcept { \
        int sz1 = this->size(); \
        int sz2 = other.size(); \
        int min_sz = sz1 < sz2 ? sz1 : sz2; \
        for (int i = 0; i < min_sz; ++i) { \
            if ((*this)[i] < other[i]) return true; \
            if (other[i] < (*this)[i]) return false; \
        } \
        return sz1 < sz2; \
    } \
    inline bool operator>(const ContainerType& other) const noexcept { \
        int sz1 = this->size(); \
        int sz2 = other.size(); \
        int min_sz = sz1 < sz2 ? sz1 : sz2; \
        for (int i = 0; i < min_sz; ++i) { \
            if (other[i] < (*this)[i]) return true; \
            if ((*this)[i] < other[i]) return false; \
        } \
        return sz1 > sz2; \
    } \
    inline bool operator<=(const ContainerType& other) const noexcept { return !(*this > other); } \
    inline bool operator>=(const ContainerType& other) const noexcept { return !(*this < other); }
#endif

#ifndef SQLITE_DERIVE_SCALAR_RELATIONAL_OPS
#define SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(ScalarType) \
    inline bool operator==(const ScalarType& val) const noexcept { \
        return (this->size() == 1) ? ((*this)[0] == val) : false; \
    } \
    inline bool operator!=(const ScalarType& val) const noexcept { return !(*this == val); } \
    inline bool operator<(const ScalarType& val) const noexcept { \
        int sz = this->size(); \
        if (sz == 0) return true; \
        if ((*this)[0] < val) return true; \
        if (val < (*this)[0]) return false; \
        return sz < 1; \
    } \
    inline bool operator>(const ScalarType& val) const noexcept { \
        int sz = this->size(); \
        if (sz == 0) return false; \
        if (val < (*this)[0]) return true; \
        if ((*this)[0] < val) return false; \
        return sz > 1; \
    } \
    inline bool operator<=(const ScalarType& val) const noexcept { return !(*this > val); } \
    inline bool operator>=(const ScalarType& val) const noexcept { return !(*this < val); }
#endif

/**
 * @brief Macro helper synthesizing direct relational operators against raw C-strings (`const char*`).
 * 
 * Explicitly constructs a lightweight non-allocating `SqliteStringView(val)` to prevent compiler
 * overload resolution ambiguity between `SqliteStringView` and `SqliteString` (from `sqlite3_buffer.hpp`).
 */
#ifndef SQLITE_DERIVE_CSTR_RELATIONAL_OPS
#define SQLITE_DERIVE_CSTR_RELATIONAL_OPS \
    inline bool operator==(const char* val) const noexcept { \
        return (this->size() == 1) ? ((*this)[0] == SqliteStringView(val)) : false; \
    } \
    inline bool operator!=(const char* val) const noexcept { return !(*this == val); } \
    inline bool operator<(const char* val) const noexcept { \
        int sz = this->size(); \
        if (sz == 0) return true; \
        SqliteStringView sv(val); \
        if ((*this)[0] < sv) return true; \
        if (sv < (*this)[0]) return false; \
        return sz < 1; \
    } \
    inline bool operator>(const char* val) const noexcept { \
        int sz = this->size(); \
        if (sz == 0) return false; \
        SqliteStringView sv(val); \
        if (sv < (*this)[0]) return true; \
        if ((*this)[0] < sv) return false; \
        return sz > 1; \
    } \
    inline bool operator<=(const char* val) const noexcept { return !(*this > val); } \
    inline bool operator>=(const char* val) const noexcept { return !(*this < val); }
#endif

#ifndef SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS
#define SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteValueOwned) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteValueView) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteStringView) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteStringOwned) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteBlobView) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(SqliteBlobOwned) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(sqlite3_int64) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(long) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(int) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(unsigned int) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(unsigned long) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(unsigned long long) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(double) \
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(bool) \
    SQLITE_DERIVE_CSTR_RELATIONAL_OPS
#endif

#ifndef SQLITE_DERIVE_REVERSE_RELATIONAL_OPS
#define SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(LhsType, RhsType) \
    inline bool operator==(const LhsType& lhs, const RhsType& rhs) noexcept { return rhs == lhs; } \
    inline bool operator!=(const LhsType& lhs, const RhsType& rhs) noexcept { return !(rhs == lhs); } \
    inline bool operator<(const LhsType& lhs, const RhsType& rhs) noexcept  { return rhs > lhs; } \
    inline bool operator<=(const LhsType& lhs, const RhsType& rhs) noexcept { return rhs >= lhs; } \
    inline bool operator>(const LhsType& lhs, const RhsType& rhs) noexcept  { return rhs < lhs; } \
    inline bool operator>=(const LhsType& lhs, const RhsType& rhs) noexcept { return rhs <= lhs; }
#endif

#ifndef SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS
#define SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteValueOwned, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteValueView, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteStringView, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteStringOwned, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteBlobView, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteBlobOwned, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(sqlite3_int64, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(long, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(int, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(unsigned int, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(unsigned long, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(unsigned long long, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(double, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(bool, TargetClass) \
    inline bool operator==(const char* lhs, const TargetClass& rhs) noexcept { return rhs == lhs; } \
    inline bool operator!=(const char* lhs, const TargetClass& rhs) noexcept { return rhs != lhs; } \
    inline bool operator<(const char* lhs, const TargetClass& rhs) noexcept  { return rhs > lhs; } \
    inline bool operator<=(const char* lhs, const TargetClass& rhs) noexcept { return rhs >= lhs; } \
    inline bool operator>(const char* lhs, const TargetClass& rhs) noexcept  { return rhs < lhs; } \
    inline bool operator>=(const char* lhs, const TargetClass& rhs) noexcept { return rhs <= lhs; }
#endif

// ============================================================================
// 1. SqliteRowView: Universal Non-Owning Multi-Column Row View
// ============================================================================

/**
 * @brief Lightweight, zero-allocation non-owning view over a multi-column SQLite row or UDF arguments.
 * 
 * `SqliteRowView` provides uniform, zero-copy, bounds-checked access to multi-column
 * tabular rows and UDF argument vectors. It acts as a universal abstraction layer over:
 * - Active prepared statement rows (`sqlite3_stmt*` or `SqliteStatement&`)
 * - SQLite UDF / aggregate / vtab argument vectors (`sqlite3_value**` with `argc`)
 * - In-memory contiguous view arrays (`const SqliteValueView*`)
 * 
 * All column access operations (`operator[]`, `as_text()`, `as_int64()`, etc.) return
 * lightweight non-owning views with zero heap allocations.
 */
class SqliteRowView {
private:
    union {
        SqliteStatement*        m_stmt;
        sqlite3_value**         m_argv;
        const SqliteValueView*  m_view_array;
    };
    int                         m_col_count;
    SqliteRowSourceType         m_source; // SQLITE_ROW_SOURCE_*

public:
    /**
     * @brief Constructs an empty row view with zero columns.
     */
    inline SqliteRowView() noexcept : m_stmt(nullptr), m_col_count(0), m_source(SQLITE_ROW_SOURCE_EMPTY) {}

    /**
     * @brief Constructs a row view wrapping an active prepared statement step row.
     * 
     * @param stmt The raw SQLite statement handle (sqlite3_stmt*) positioned at SQLITE_ROW.
     */
    inline explicit SqliteRowView(sqlite3_stmt* stmt) noexcept
        : m_stmt(reinterpret_cast<SqliteStatement*>(stmt)), m_col_count(stmt ? sqlite3_column_count(stmt) : 0), m_source(SQLITE_ROW_SOURCE_STMT) {}

    /**
     * @brief Constructs a row view wrapping an active SqliteStatement pointer positioned at SQLITE_ROW.
     * 
     * @param stmt Pointer to SqliteStatement wrapper.
     */
    inline explicit SqliteRowView(SqliteStatement* stmt) noexcept
        : m_stmt(stmt), m_col_count(stmt ? sqlite3_column_count(reinterpret_cast<sqlite3_stmt*>(stmt)) : 0), m_source(SQLITE_ROW_SOURCE_STMT) {}

    /**
     * @brief Constructs a row view wrapping raw UDF / vtab argument arrays.
     * 
     * @param argc Number of arguments in the array.
     * @param argv Array of sqlite3_value* pointers.
     */
    inline SqliteRowView(int argc, sqlite3_value** argv) noexcept
        : m_argv(argv), m_col_count(argc >= 0 ? argc : 0), m_source(SQLITE_ROW_SOURCE_ARGV) {}

    /**
     * @brief Constructs a row view wrapping a raw argument vector with an optional count.
     */
    inline explicit SqliteRowView(sqlite3_value** argv, int argc = 0) noexcept
        : m_argv(argv), m_col_count(argc >= 0 ? argc : 0), m_source(SQLITE_ROW_SOURCE_ARGV) {}

    /**
     * @brief Constructs a row view wrapping a contiguous array of SqliteValueView.
     * 
     * @param array Pointer to the first SqliteValueView element.
     * @param count Number of column views in the array.
     */
    inline SqliteRowView(const SqliteValueView* array, int count) noexcept
        : m_view_array(array), m_col_count(count >= 0 ? count : 0), m_source(SQLITE_ROW_SOURCE_VIEW_ARRAY) {}

    /**
     * @brief Returns the total number of columns available in this row.
     * 
     * @return Column count as an integer.
     */
    inline int size() const noexcept { return m_col_count; }

    /**
     * @brief Alias for size() returning the total column count.
     * 
     * @return Column count as an integer.
     */
    inline int count() const noexcept { return m_col_count; }

    /**
     * @brief Alias for size() returning the total column count.
     * 
     * @return Column count as an integer.
     */
    inline int column_count() const noexcept { return m_col_count; }

    /**
     * @brief Alias for size() returning argument count.
     */
    inline int argc() const noexcept { return m_col_count; }

    /**
     * @brief Checks if the row contains zero columns.
     * 
     * @return True if empty (count == 0), false otherwise.
     */
    inline bool empty() const noexcept { return m_col_count == 0; }

    // =========================================================================
    // Element Accessors & Class-Level Getter
    // =========================================================================

    /**
     * @brief Safely accesses a column as a zero-allocation SqliteValueView.
     * 
     * If the column index is out of bounds (< 0 or >= size()), returns a SQLITE_NULL view
     * to guarantee complete segfault immunity.
     * 
     * @param col 0-indexed column index.
     * @return Non-owning SqliteValueView for the column.
     */
    inline SqliteValueView operator[](int col) const noexcept {
        if (col < 0 || col >= m_col_count) {
            return SqliteValueView(nullptr);
        }
        switch (m_source) {
            case SQLITE_ROW_SOURCE_STMT:        return SqliteValueView::from_column(raw_stmt(), col);
            case SQLITE_ROW_SOURCE_ARGV:        return (m_argv && m_argv[col]) ? SqliteValueView(m_argv[col]) : SqliteValueView(nullptr);
            case SQLITE_ROW_SOURCE_VIEW_ARRAY:  return m_view_array[col];
            default:                            return SqliteValueView(nullptr);
        }
    }

    /** @brief Bounds-safe column accessor identical to operator[]. */
    inline SqliteValueView at(int col) const noexcept { return (*this)[col]; }

    /** @brief Extracts a column value as a zero-allocation SqliteValueView. */
    inline SqliteValueView get_column(int col) const noexcept { return (*this)[col]; }

    /** @brief Alias for get_column(). */
    inline SqliteValueView column(int col) const noexcept { return (*this)[col]; }

    /**
     * @brief Returns the column name (only available when backed by a prepared statement).
     * 
     * @param col 0-indexed column index.
     * @return UTF-8 column name string, or nullptr if unavailable / out of bounds.
     */
    inline const char* column_name(int col) const noexcept {
        if (m_source == SQLITE_ROW_SOURCE_STMT && m_stmt && col >= 0 && col < m_col_count) {
            return sqlite3_column_name(raw_stmt(), col);
        }
        return nullptr;
    }

    /**
     * @brief Returns the declared column datatype in the table schema (e.g. "INTEGER", "TEXT").
     * 
     * Only available when backed by a prepared statement.
     * 
     * @param col 0-indexed column index.
     * @return Declared column type string, or nullptr if unavailable / out of bounds.
     */
    inline const char* column_decltype(int col) const noexcept {
        if (m_source == SQLITE_ROW_SOURCE_STMT && m_stmt && col >= 0 && col < m_col_count) {
            return sqlite3_column_decltype(raw_stmt(), col);
        }
        return nullptr;
    }

    // =========================================================================
    // Direct Typed Column Accessors & 64-Bit MurmurHash2 Calculation
    // =========================================================================
    SQLITE_DERIVE_ARRAY_ACCESSORS
    SQLITE_DERIVE_ARRAY_HASH

    // =========================================================================
    // Full Relational Operators (==, !=, <, <=, >, >=)
    // =========================================================================
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
    SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS

    /** @brief Returns internal source type (SQLITE_ROW_SOURCE_*). */
    inline SqliteRowSourceType source_type() const noexcept { return m_source; }
    inline sqlite3_stmt* raw_stmt() const noexcept { return reinterpret_cast<sqlite3_stmt*>(m_stmt); }
    inline SqliteStatement* statement() const noexcept { return m_stmt; }
    inline sqlite3_value** raw_argv() const noexcept { return m_argv; }
    inline sqlite3_value** argv() const noexcept { return m_argv; }
    inline sqlite3_value** data() const noexcept { return m_argv; }
    inline const SqliteValueView* raw_view_array() const noexcept { return m_view_array; }

    // Range-Based For Loop Iterator
    SQLITE_DERIVE_ARRAY_ITERATOR(SqliteRowView, SqliteValueView)
};

#ifndef SQLITE3_UDF_ARGS_DEFINED
#define SQLITE3_UDF_ARGS_DEFINED
/**
 * @brief Type alias providing unified nomenclature for scalar UDF arguments.
 */
typedef SqliteRowView SqliteUdfArgs;
#endif

// Symmetric reverse operators for SqliteRowView
SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(SqliteRowView)

// ============================================================================
// 1.5. SqliteRowUtil: Shared Row Construction Utilities
// ============================================================================
namespace SqliteRowUtil {
    /**
     * @brief Copies up to `count` columns from a SqliteRowView into a contiguous
     *        SqliteValueOwned destination buffer.
     * 
     * @param dest  Pointer to the destination SqliteValueOwned buffer.
     * @param view  The non-owning source row view.
     * @param count Number of elements to copy.
     */
    inline void copy_from_view(SqliteValueOwned* dest, const SqliteRowView& view, int count) noexcept {
        for (int i = 0; i < count; ++i) {
            dest[i] = view[i].to_owned();
        }
    }
} // namespace SqliteRowUtil

// ============================================================================
// 2. SqliteRowOwnedWrapper: 16-Byte Span over Contiguous SqliteValueOwned
// ============================================================================

/**
 * @class SqliteRowOwnedWrapper
 * @brief Zero-allocation 16-byte span (const SqliteValueOwned* + int len) over contiguous owned value buffers.
 * 
 * Fits in 2 CPU registers (rax, rdx). Wraps and compares against:
 * - Single scalar values (SqliteValueOwned, SqliteValueView)
 * - Strings (SqliteStringView, SqliteStringOwned)
 * - Transient argument vectors (SqliteUdfArgs / SqliteRowView)
 * - Tuple containers (SqliteValueTuple<N>)
 * - Dynamic vector containers (SqliteValueVec<N>)
 */
class SqliteRowOwnedWrapper {
private:
    SqliteValueOwned* m_data;
    int               m_len;

    static inline SqliteValueOwned& mutable_fallback_null() noexcept {
        static SqliteValueOwned null_val;
        return null_val;
    }

    static inline const SqliteValueOwned& fallback_null() noexcept {
        static const SqliteValueOwned null_val;
        return null_val;
    }

public:
    // =========================================================================
    // Constructors (Zero Copies, Zero Dynamic Allocations)
    // =========================================================================

    /**
     * @brief Constructs an empty row wrapper with zero columns.
     */
    inline SqliteRowOwnedWrapper() noexcept : m_data(nullptr), m_len(0) {}

    /**
     * @brief Constructs a row wrapper spanning a contiguous array of SqliteValueOwned.
     * 
     * @param data Pointer to the first SqliteValueOwned element.
     * @param len  Number of columns in the array.
     */
    inline SqliteRowOwnedWrapper(const SqliteValueOwned* data, int len) noexcept
        : m_data(const_cast<SqliteValueOwned*>(data)), m_len(data && len > 0 ? len : 0) {}

    /**
     * @brief Constructs a 1-column row wrapper spanning a single SqliteValueOwned.
     * 
     * @param val Reference to the single value.
     */
    inline explicit SqliteRowOwnedWrapper(const SqliteValueOwned& val) noexcept
        : m_data(const_cast<SqliteValueOwned*>(&val)), m_len(1) {}

    /**
     * @brief Constructs a row wrapper spanning a SqliteValueTuple<N>.
     */
    template <size_t N, typename Enable>
    inline SqliteRowOwnedWrapper(const SqliteValueTuple<N, Enable>& tuple) noexcept
        : m_data(const_cast<SqliteValueOwned*>(tuple.data())), m_len(tuple.size()) {}

    /**
     * @brief Constructs a row wrapper spanning a SqliteValueVec<N>.
     */
    template <size_t N, typename Enable>
    inline SqliteRowOwnedWrapper(const SqliteValueVec<N, Enable>& vec) noexcept
        : m_data(const_cast<SqliteValueOwned*>(vec.data())), m_len(vec.size()) {}

    // Default copy/move semantics (Trivial 16-byte register copy)
    SqliteRowOwnedWrapper(const SqliteRowOwnedWrapper&) noexcept = default;
    SqliteRowOwnedWrapper& operator=(const SqliteRowOwnedWrapper&) noexcept = default;
    SqliteRowOwnedWrapper(SqliteRowOwnedWrapper&&) noexcept = default;
    SqliteRowOwnedWrapper& operator=(SqliteRowOwnedWrapper&&) noexcept = default;

    // =========================================================================
    // Static Factory Methods
    // =========================================================================

    /** @brief Factory method creating a wrapper span from a raw pointer and size. */
    static inline SqliteRowOwnedWrapper create(const SqliteValueOwned* data, int size) noexcept {
        return SqliteRowOwnedWrapper(data, size);
    }
    /** @brief Factory method creating a 1-column wrapper span from a single value. */
    static inline SqliteRowOwnedWrapper create(const SqliteValueOwned& val) noexcept {
        return SqliteRowOwnedWrapper(&val, 1);
    }
    /** @brief Factory method creating a wrapper span from a tuple. */
    template <size_t N, typename Enable>
    static inline SqliteRowOwnedWrapper create(const SqliteValueTuple<N, Enable>& tuple) noexcept {
        return SqliteRowOwnedWrapper(tuple.data(), tuple.size());
    }
    /** @brief Factory method creating a wrapper span from a vector. */
    template <size_t N, typename Enable>
    static inline SqliteRowOwnedWrapper create(const SqliteValueVec<N, Enable>& vec) noexcept {
        return SqliteRowOwnedWrapper(vec.data(), vec.size());
    }

    // =========================================================================
    // Capacity & Element Accessors
    // =========================================================================

    /** @brief Returns the total number of columns spanned by this wrapper. */
    inline int  size()  const noexcept { return m_len; }

    /** @brief Alias for size() returning the total column count. */
    inline int  count() const noexcept { return m_len; }

    /** @brief Checks if the wrapper spans zero columns or has a null data pointer. */
    inline bool empty() const noexcept { return m_len == 0 || m_data == nullptr; }

    /** @brief Returns a mutable pointer to the underlying column array. */
    inline SqliteValueOwned* data() noexcept { return m_data; }

    /** @brief Returns a read-only pointer to the underlying column array. */
    inline const SqliteValueOwned* data() const noexcept { return m_data; }

    /**
     * @brief Mutable subscript operator with out-of-bounds safety.
     * 
     * @param index 0-indexed column position.
     * @return Mutable reference to the element (or fallback static null if invalid).
     */
    inline SqliteValueOwned& operator[](int index) noexcept {
        return (m_data && index >= 0 && index < m_len) ? m_data[index] : mutable_fallback_null();
    }

    /**
     * @brief Read-only subscript operator with out-of-bounds safety.
     * 
     * @param index 0-indexed column position.
     * @return Const reference to the element (or fallback static null if invalid).
     */
    inline const SqliteValueOwned& operator[](int index) const noexcept {
        return (m_data && index >= 0 && index < m_len) ? m_data[index] : fallback_null();
    }

    /** @brief Bounds-safe mutable element accessor identical to operator[]. */
    inline SqliteValueOwned& at(int index) noexcept {
        return (*this)[index];
    }

    /** @brief Bounds-safe read-only element accessor identical to operator[]. */
    inline const SqliteValueOwned& at(int index) const noexcept {
        return (*this)[index];
    }

    // Typed Column Extraction Accessors, Composite Hashing & Iterator
    SQLITE_DERIVE_ARRAY_ACCESSORS
    SQLITE_DERIVE_ARRAY_HASH
    SQLITE_DERIVE_ARRAY_ITERATOR(SqliteRowOwnedWrapper, const SqliteValueOwned&)

public:
    // ========================================================================
    // Full Relational Operators (==, !=, <, <=, >, >=)
    // =========================================================================
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedWrapper)
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
    SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS
};

static_assert(sizeof(SqliteRowOwnedWrapper) == 16, "SqliteRowOwnedWrapper must be exactly 16 bytes!");

// Symmetric reverse operators
SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteRowView, SqliteRowOwnedWrapper)
SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(SqliteRowOwnedWrapper)

namespace SqliteRowUtil {
    inline void copy_from_wrapper(SqliteValueOwned* dest, const SqliteRowOwnedWrapper& view, int count) noexcept {
        for (int i = 0; i < count; ++i) {
            dest[i] = view[i].clone();
        }
    }
}

// ============================================================================
// 3. Transparent Functors for Swiss Tables & B-Trees (SqliteRow*)
// ============================================================================

#ifndef SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS
#define SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS \
    inline size_t operator()(const SqliteRowOwnedWrapper& k) const noexcept { return static_cast<size_t>(k.hash()); } \
    inline size_t operator()(const SqliteRowView& r) const noexcept         { return static_cast<size_t>(r.hash()); } \
    template <size_t N, typename Enable> \
    inline size_t operator()(const SqliteValueTuple<N, Enable>& r) const noexcept { return static_cast<size_t>(r.hash()); } \
    template <size_t N, typename Enable> \
    inline size_t operator()(const SqliteValueVec<N, Enable>& r) const noexcept   { return static_cast<size_t>(r.hash()); } \
    SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH_OVERLOADS
#endif

/**
 * @struct SqliteRowHash
 * @brief Transparent 64-bit MurmurHash2 functor for row spans, containers, and primitives.
 * 
 * Enables zero-allocation heterogeneous hashing across SqliteRowOwnedWrapper spans,
 * SqliteValueTuple<N>, SqliteValueVec<N>, SqliteRowView, strings, blobs, and primitives.
 */
struct SqliteRowHash {
    using is_transparent = void;
    SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS
};

/**
 * @struct SqliteRowEqual
 * @brief Transparent equality functor for Swiss tables and hash map containers.
 */
SQLITE_DERIVE_TRANSPARENT_EQUAL(SqliteRowEqual)

/**
 * @struct SqliteRowLess
 * @brief Transparent less-than functor for B-Tree and ordered map containers.
 */
SQLITE_DERIVE_TRANSPARENT_LESS(SqliteRowLess)

// Core container integration (SqliteValueTuple, SqliteValueVec, withSqliteRowOwned)
#include "sqlite3_value_containers.hpp"

#endif // SQLITE3_ROW_HPP
