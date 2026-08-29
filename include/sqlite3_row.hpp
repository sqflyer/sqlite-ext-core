#ifndef SQLITE3_ROW_HPP
#define SQLITE3_ROW_HPP

#include <sqlite3.h>
#include "sqlite3_value.hpp"
#include "sqlite3_allocator.hpp"

// Forward declaration of classes for tight integration
class SqliteStatement;
class SqliteRowDynamic;
class SqliteRowOwnedWrapper;

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
    SQLITE_DERIVE_SCALAR_RELATIONAL_OPS(int) \
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
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteValueViewArray, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteValueOwned, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteValueView, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteStringView, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteStringOwned, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteBlobView, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteBlobOwned, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(sqlite3_int64, TargetClass) \
    SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(int, TargetClass) \
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
 * @brief Lightweight, zero-allocation non-owning view over a multi-column SQLite row.
 * 
 * `SqliteRowView` provides uniform, zero-copy, bounds-checked access to multi-column
 * tabular rows. It acts as a universal abstraction layer over:
 * - Active prepared statement rows (`sqlite3_stmt*` or `SqliteStatement&`)
 * - SQLite UDF / aggregate argument vectors (`SqliteUdfArgs` or `sqlite3_value**`)
 * - In-memory contiguous view arrays (`const SqliteValueView*`)
 * 
 * All column access operations (`operator[]`, `as_text()`, `as_int64()`, etc.) return
 * lightweight non-owning views with zero heap allocations.
 */
class SqliteRowView {
private:
    union {
        SqliteStatement*        m_stmt;
        SqliteValueViewArray    m_argv;
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
        : m_argv(argc, argv), m_col_count(argc >= 0 ? argc : 0), m_source(SQLITE_ROW_SOURCE_ARGV) {}

    /**
     * @brief Constructs a row view wrapping SqliteValueViewArray / SqliteUdfArgs from a UDF callback.
     * 
     * @param args The SqliteValueViewArray instance received in the callback.
     */
    inline SqliteRowView(const SqliteValueViewArray& args) noexcept
        : m_argv(args), m_col_count(args.size()), m_source(SQLITE_ROW_SOURCE_ARGV) {}

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
    inline int column_count() const noexcept { return m_col_count; }

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
            case SQLITE_ROW_SOURCE_ARGV:        return m_argv[col];
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
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteValueViewArray)
    SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS

    /** @brief Returns internal source type (SQLITE_ROW_SOURCE_*). */
    inline SqliteRowSourceType source_type() const noexcept { return m_source; }
    inline sqlite3_stmt* raw_stmt() const noexcept { return reinterpret_cast<sqlite3_stmt*>(m_stmt); }
    inline SqliteStatement* statement() const noexcept { return m_stmt; }
    inline sqlite3_value** raw_argv() const noexcept { return m_argv.argv(); }
    inline SqliteValueViewArray argv() const noexcept { return m_argv; }
    inline const SqliteValueView* raw_view_array() const noexcept { return m_view_array; }

    /**
     * @brief Materializes this transient non-owning row into an owned SqliteRowDynamic snapshot.
     * 
     * Deep copies all column strings, blobs, and subtypes into managed memory.
     * 
     * @return SqliteRowDynamic containing owned snapshots of all columns.
     */
    inline SqliteRowDynamic to_owned() const;

    // Range-Based For Loop Iterator
    SQLITE_DERIVE_ARRAY_ITERATOR(SqliteRowView, SqliteValueView)
};

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
// 2. SqliteRowStatic<size_t N>: Pure Stack-Allocated Row (0 Mallocs)
// ============================================================================

/**
 * @brief Fixed-size, compile-time stack-allocated row container storing exactly N columns.
 * 
 * Inherits from `SqliteValueOwnedStaticArray<N>` with added tabular row methods.
 * Footprint: Exactly N * 16 Bytes on the stack or in-situ within a data structure.
 * Guaranteed 0 heap allocations for maximum cache density and execution speed.
 * 
 * Perfect for virtual table engines (e.g. memkv_lru, memkv_map) with fixed-column schemas.
 * 
 * @tparam N The number of columns in the row (must be > 0).
 */
template <size_t N>
class SqliteRowStatic : public SqliteValueOwnedStaticArray<N> {
public:
    /**
     * @brief Initializes all N columns to SQLITE_NULL.
     */
    inline SqliteRowStatic() noexcept : SqliteValueOwnedStaticArray<N>() {}

    /**
     * @brief Copies and materializes up to N columns from a non-owning row view.
     * 
     * @param view The SqliteRowView to snapshot.
     */
    inline explicit SqliteRowStatic(const SqliteRowView& view) noexcept {
        int limit = static_cast<int>(N);
        if (view.size() < limit) limit = view.size();
        SqliteRowUtil::copy_from_view(this->m_values, view, limit);
    }

    /**
     * @brief Copies and materializes up to N columns from a row wrapper span.
     * 
     * @param view The SqliteRowOwnedWrapper to snapshot.
     */
    inline explicit SqliteRowStatic(const SqliteRowOwnedWrapper& view) noexcept;

    /**
     * @brief Alias for size() returning the compile-time column count.
     * 
     * @return Integer column count.
     */
    inline int column_count() const noexcept { return static_cast<int>(N); }

    /**
     * @brief Converts this stack row into a zero-allocation 16-byte SqliteRowOwnedWrapper span.
     * 
     * @return SqliteRowOwnedWrapper span wrapping the stack array.
     */
    inline SqliteRowOwnedWrapper view() const noexcept;
};

// ============================================================================
// 3. SqliteRowDynamic: Runtime-Sized Heap Row Container (sqlite3_malloc64)
// ============================================================================

/**
 * @brief Dynamic, heap-allocated row container for queries with runtime column counts.
 * 
 * Inherits from `SqliteValueOwnedDynamicArray` with added tabular row methods.
 * Allocates a contiguous buffer of SqliteValueOwned using SQLite's native memory
 * allocator (sqlite3_malloc64 / sqlite3_free). Integrates zero-dependency move semantics
 * and placement new/delete via sqlite3_allocator.hpp.
 */
class SqliteRowDynamic : public SqliteValueOwnedDynamicArray {
public:
    /**
     * @brief Constructs an empty dynamic row.
     */
    inline SqliteRowDynamic() noexcept : SqliteValueOwnedDynamicArray() {}

    /**
     * @brief Constructs a dynamic row with pre-allocated column count, initialized to SQLITE_NULL.
     * 
     * @param count Number of columns to allocate.
     */
    inline explicit SqliteRowDynamic(int count) : SqliteValueOwnedDynamicArray(count) {}

    /**
     * @brief Constructs and materializes a dynamic row snapshot from a row view.
     * 
     * @param view Non-owning row view to copy.
     */
    inline SqliteRowDynamic(const SqliteRowView& view) : SqliteValueOwnedDynamicArray() {
        int sz = view.size();
        if (sz > 0) {
            this->resize(sz);
            SqliteRowUtil::copy_from_view(this->m_values, view, sz);
        }
    }

    /**
     * @brief Constructs and materializes a dynamic row snapshot from a row wrapper span.
     * 
     * @param view Non-owning row wrapper to copy.
     */
    inline SqliteRowDynamic(const SqliteRowOwnedWrapper& view);

    /** @brief Alias for size() returning the number of columns. */
    inline int column_count() const noexcept { return this->size(); }

    /**
     * @brief Converts this heap row into a zero-allocation 16-byte SqliteRowOwnedWrapper span.
     * 
     * @return SqliteRowOwnedWrapper span wrapping the heap array.
     */
    inline SqliteRowOwnedWrapper view() const noexcept;

    // Relational Operators
    inline bool operator==(const SqliteRowDynamic& other) const noexcept;
    inline bool operator<(const SqliteRowDynamic& other) const noexcept;
    SQLITE_DERIVE_RELATIONAL_OPS(SqliteRowDynamic)
};

// ============================================================================
// 4. SqliteRowOwned<size_t N>: Unified Row Template
// ============================================================================

/**
 * @brief Unified Row template alias.
 * - SqliteRowOwned<N> (N > 0): Stack-allocated SqliteRowStatic<N> (0 heap allocations).
 * - SqliteRowOwned<0>: Dynamic heap-allocated SqliteRowDynamic (runtime sizing).
 */
template <size_t N = 0>
class SqliteRowOwned : public SqliteRowStatic<N> {
public:
    using SqliteRowStatic<N>::SqliteRowStatic;
};

/**
 * @brief Specialization of SqliteRowOwned for N=0 mapping to SqliteRowDynamic.
 */
template <>
class SqliteRowOwned<0> : public SqliteRowDynamic {
public:
    using SqliteRowDynamic::SqliteRowDynamic;
};

// ============================================================================
// 5. Inlined Implementations
// ============================================================================

inline SqliteRowDynamic SqliteRowView::to_owned() const {
    return SqliteRowDynamic(*this);
}

// ============================================================================
// 6. SqliteRowOwnedWrapper: 16-Byte Span over Contiguous SqliteValueOwned
// ============================================================================

/**
 * @class SqliteRowOwnedWrapper
 * @brief Zero-allocation 16-byte span (const SqliteValueOwned* + int len) over contiguous owned value buffers.
 * 
 * Fits in 2 CPU registers (rax, rdx). Wraps and compares against:
 * - Single scalar values (SqliteValueOwned, SqliteValueView)
 * - Strings (SqliteStringView, SqliteStringOwned)
 * - Blobs (SqliteBlobView, SqliteBlobOwned)
 * - Transient argument vectors (SqliteValueViewArray / SqliteUdfArgs)
 * - Static arrays (SqliteValueOwnedStaticArray<N>)
 * - Dynamic rows (SqliteRowDynamic)
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
     * @brief Constructs a row wrapper spanning a compile-time static array.
     * 
     * @tparam N Number of columns in the static array.
     * @param static_arr The static array instance to span.
     */
    template <size_t N>
    inline SqliteRowOwnedWrapper(const SqliteValueOwnedStaticArray<N>& static_arr) noexcept
        : m_data(const_cast<SqliteValueOwned*>(static_arr.data())), m_len(static_arr.size()) {}

    /**
     * @brief Constructs a row wrapper spanning a dynamic heap row.
     * 
     * @param dynamic_row The SqliteRowDynamic instance to span.
     */
    inline SqliteRowOwnedWrapper(const SqliteRowDynamic& dynamic_row) noexcept
        : m_data(const_cast<SqliteValueOwned*>(dynamic_row.data())), m_len(dynamic_row.size()) {}

    /**
     * @brief Constructs a row wrapper spanning a static row.
     * 
     * @tparam N Number of columns in the static row.
     * @param static_row The SqliteRowStatic<N> instance to span.
     */
    template <size_t N>
    inline SqliteRowOwnedWrapper(const SqliteRowStatic<N>& static_row) noexcept
        : m_data(const_cast<SqliteValueOwned*>(static_row.data())), m_len(static_row.column_count()) {}

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
    /** @brief Factory method creating a wrapper span from a static array. */
    template <size_t N>
    static inline SqliteRowOwnedWrapper create(const SqliteValueOwnedStaticArray<N>& arr) noexcept {
        return SqliteRowOwnedWrapper(arr.data(), arr.size());
    }
    /** @brief Factory method creating a wrapper span from a dynamic row. */
    static inline SqliteRowOwnedWrapper create(const SqliteRowDynamic& row) noexcept {
        return SqliteRowOwnedWrapper(row.data(), row.size());
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
    // ========================================================================
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedWrapper)
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteValueViewArray)
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

template <size_t N>
inline SqliteRowStatic<N>::SqliteRowStatic(const SqliteRowOwnedWrapper& view) noexcept {
    int limit = static_cast<int>(N);
    if (view.size() < limit) limit = view.size();
    SqliteRowUtil::copy_from_wrapper(this->m_values, view, limit);
}

template <size_t N>
inline SqliteRowOwnedWrapper SqliteRowStatic<N>::view() const noexcept {
    return SqliteRowOwnedWrapper(this->data(), static_cast<int>(N));
}

inline SqliteRowDynamic::SqliteRowDynamic(const SqliteRowOwnedWrapper& view) : SqliteValueOwnedDynamicArray() {
    int sz = view.size();
    if (sz > 0) {
        this->resize(sz);
        SqliteRowUtil::copy_from_wrapper(this->m_values, view, sz);
    }
}

inline SqliteRowOwnedWrapper SqliteRowDynamic::view() const noexcept {
    return SqliteRowOwnedWrapper(this->data(), this->size());
}

inline bool SqliteRowDynamic::operator==(const SqliteRowDynamic& other) const noexcept {
    return view() == other.view();
}

inline bool SqliteRowDynamic::operator<(const SqliteRowDynamic& other) const noexcept {
    return view() < other.view();
}

// ============================================================================
// 7. withSqliteRowOwned: Stack-Allocated Row Scope Dispatcher (1..8 cols)
// ============================================================================

/**
 * @brief Zero-heap stack allocation dispatcher for small dynamic row materialization.
 * 
 * Invokes a user lambda with a stack-allocated buffer for sizes 1..8 (0 heap allocations).
 * Falls back to dynamic heap allocation for sizes > 8.
 * 
 * @tparam Callable Lambda/Functor signature: `auto(SqliteRowOwnedWrapper row_wrapper)`
 * @param size Requested number of columns (1..8 for stack, >8 uses heap fallback).
 * @param fn Visitor callback receiving the mutable wrapper span.
 * @return Return value of the user callback function.
 */
template <typename Callable>
inline auto withSqliteRowOwned(int size, Callable&& fn) -> decltype(fn(SqliteRowOwnedWrapper())) {
    switch (size) {
        case 1: {
            SqliteValueOwnedStaticArray<1> arr;
            return fn(SqliteRowOwnedWrapper(arr.data(), 1));
        }
        case 2: {
            SqliteValueOwnedStaticArray<2> arr;
            return fn(SqliteRowOwnedWrapper(arr.data(), 2));
        }
        case 3: {
            SqliteValueOwnedStaticArray<3> arr;
            return fn(SqliteRowOwnedWrapper(arr.data(), 3));
        }
        case 4: {
            SqliteValueOwnedStaticArray<4> arr;
            return fn(SqliteRowOwnedWrapper(arr.data(), 4));
        }
        case 5: {
            SqliteValueOwnedStaticArray<5> arr;
            return fn(SqliteRowOwnedWrapper(arr.data(), 5));
        }
        case 6: {
            SqliteValueOwnedStaticArray<6> arr;
            return fn(SqliteRowOwnedWrapper(arr.data(), 6));
        }
        case 7: {
            SqliteValueOwnedStaticArray<7> arr;
            return fn(SqliteRowOwnedWrapper(arr.data(), 7));
        }
        case 8: {
            SqliteValueOwnedStaticArray<8> arr;
            return fn(SqliteRowOwnedWrapper(arr.data(), 8));
        }
        default: {
            if (size <= 0) {
                return fn(SqliteRowOwnedWrapper(nullptr, 0));
            }
            SqliteRowDynamic arr(size);
            return fn(SqliteRowOwnedWrapper(arr.data(), size));
        }
    }
}

// ============================================================================
// 8. Transparent Functors for Swiss Tables & B-Trees (SqliteRow*)
// ============================================================================

#ifndef SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS
#define SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS \
    inline size_t operator()(const SqliteRowOwnedWrapper& k) const noexcept { return static_cast<size_t>(k.hash()); } \
    inline size_t operator()(const SqliteRowView& r) const noexcept         { return static_cast<size_t>(r.hash()); } \
    inline size_t operator()(const SqliteValueViewArray& v) const noexcept  { return static_cast<size_t>(v.hash()); } \
    inline size_t operator()(const SqliteRowDynamic& r) const noexcept      { return static_cast<size_t>(SqliteRowOwnedWrapper(r).hash()); } \
    template <size_t N> \
    inline size_t operator()(const SqliteRowStatic<N>& r) const noexcept    { return static_cast<size_t>(SqliteRowOwnedWrapper(r).hash()); } \
    SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH_OVERLOADS
#endif

/**
 * @struct SqliteRowHash
 * @brief Transparent 64-bit MurmurHash2 functor for row spans, containers, and primitives.
 * 
 * Enables zero-allocation heterogeneous hashing across SqliteRowOwnedWrapper spans,
 * SqliteRowDynamic, SqliteRowStatic<N>, SqliteValueViewArray, strings, blobs, and primitives.
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

#endif // SQLITE3_ROW_HPP
