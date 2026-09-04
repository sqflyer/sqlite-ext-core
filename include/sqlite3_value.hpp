#ifndef SQLITE3_VALUE_HPP
#define SQLITE3_VALUE_HPP

#include "sqlite3ext.h"
#include "sqlite3_allocator.hpp"
#include <cstddef>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// 1. Native SQLite Affinity Constants (sqliteInt.h)
// ============================================================================
#ifndef SQLITE_AFF_NONE
    #define SQLITE_AFF_NONE     0x40  // '@' : No affinity
    #define SQLITE_AFF_BLOB     0x41  // 'A' : BLOB
    #define SQLITE_AFF_TEXT     0x42  // 'B' : TEXT
    #define SQLITE_AFF_NUMERIC  0x43  // 'C' : NUMERIC
    #define SQLITE_AFF_INTEGER  0x44  // 'D' : INTEGER
    #define SQLITE_AFF_REAL     0x45  // 'E' : REAL
    #define SQLITE_AFF_FLEXNUM  0x46  // 'F' : Flexible Numeric
    #define SQLITE_AFF_MASK     0x47

    #define sqlite3IsNumericAffinity(X) ((X) >= SQLITE_AFF_NUMERIC)
#endif

// ============================================================================
// 2. Complete SQLite Subtype Registry (8-Bit Unsigned Integer / ASCII)
// ============================================================================
#ifndef SQLITE_SUBTYPE_NONE
    #define SQLITE_SUBTYPE_NONE       0     // 0x00 : Standard untagged SQL value
    #define SQLITE_SUBTYPE_JSON       74    // 'J'  : Official SQLite JSON & JSONB (SQLite 3.45+)
    #define SQLITE_SUBTYPE_DECIMAL    68    // 'D'  : Official SQLite decimal.c extension
    #define SQLITE_SUBTYPE_UUID       85    // 'U'  : Standard 16-byte UUID binary/string
    #define SQLITE_SUBTYPE_VECTOR     86    // 'V'  : AI Embedding Vector (float32/int8)
    #define SQLITE_SUBTYPE_GEOMETRY   71    // 'G'  : Geopoly & GeoJSON spatial coordinate array
    #define SQLITE_SUBTYPE_DATETIME   84    // 'T'  : ISO-8601 & High-precision timestamp
    #define SQLITE_SUBTYPE_BOOL       66    // 'B'  : Explicit Boolean flag (0 or 1)
    #define SQLITE_SUBTYPE_COMPRESSED 90    // 'Z'  : Compressed stream (Gorilla / ZSTD)
#endif

// ============================================================================
// 3. SQLite Function Flags
// ============================================================================
#ifndef SQLITE_SUBTYPE
    #define SQLITE_SUBTYPE 0x00010000
#endif
#ifndef SQLITE_DETERMINISTIC
    #define SQLITE_DETERMINISTIC 0x00000800
#endif
#ifndef SQLITE_DIRECTONLY
    #define SQLITE_DIRECTONLY 0x00080000
#endif
#ifndef SQLITE_INNOCUOUS
    #define SQLITE_INNOCUOUS 0x00200000
#endif
#ifndef SQLITE_RESULT_SUBTYPE
    #define SQLITE_RESULT_SUBTYPE 0x00100000
#endif

namespace SqliteMemoryUtil {
    /**
     * @brief Performs a fast lexicographical memory comparison.
     * 
     * Shared by String, Blob, and Value classes to replace manual char-by-char
     * loops. Uses standard C memcmp for SIMD-accelerated performance.
     */
    inline bool memcmp_less(const void* val1, int len1, const void* val2, int len2) {
        int min_len = (len1 < len2) ? len1 : len2;
        int cmp = memcmp(val1, val2, min_len);
        if (cmp != 0) {
            return cmp < 0;
        }
        return len1 < len2;
    }
    
    /**
     * @brief Performs a fast lexicographical equality check.
     * 
     * Shared by String, Blob, and Value classes. Uses standard C memcmp 
     * for SIMD-accelerated performance with null-safety built in.
     */
    inline bool memcmp_equal(const void* val1, int len1, const void* val2, int len2) {
        if (len1 != len2) return false;
        if (len1 == 0) return true;
        if (val1 == val2) return true;
        if (!val1 || !val2) return false;
        return memcmp(val1, val2, len1) == 0;
    }
}

#include "sqlite3_hash.hpp"

/**
 * @file sqlite3_value.hpp
 * @brief Zero-dependency C++ RAII wrappers for SQLite types (String, Blob, Value).
 * 
 * Provides 'View' (non-owning) and 'Owned' (memory-managed) classes for each type,
 * with full heterogeneous map lookup support. These types implement hash, equality,
 * and less-than operators to make them instantly usable as keys in std::map or 
 * std::unordered_map.
 */

// ============================================================================
// 1. STRING TYPES
// ============================================================================

/**
 * @namespace SqliteStringUtil
 * @brief Shared hashing and comparison utilities for SQLite text strings.
 */
namespace SqliteStringUtil {
    /**
     * @brief Computes a 64-bit MurmurHash2 of a character array.
     * @param val Pointer to the string data.
     * @param len Length of the string in bytes.
     * @return 64-bit hash value.
     */
    inline unsigned long long hash(const char* val, int len) {
        return SqliteHashUtil::hash(val, len);
    }

    /**
     * @brief Computes the length of a null-terminated C-string. 
     * Safely wraps standard `<string.h>` strlen with a nullptr check.
     */
    inline int sqlite_strlen(const char* str) {
        return str ? static_cast<int>(strlen(str)) : 0;
    }
    
    /**
     * @brief Checks if two character arrays are exactly equal.
     */
    inline bool equal(const char* val1, int len1, const char* val2, int len2) {
        return SqliteMemoryUtil::memcmp_equal(val1, len1, val2, len2);
    }
    
    /**
     * @brief Performs a lexicographical less-than comparison of two character arrays.
     */
    inline bool less(const char* val1, int len1, const char* val2, int len2) {
        if (!val1 && !val2) return false;
        if (!val1) return true;
        if (!val2) return false;
        return SqliteMemoryUtil::memcmp_less(val1, len1, val2, len2);
    }
}

class SqliteStringOwned;

/**
 * @brief Zero-cost, non-owning C++ wrapper for strings.
 * 
 * Perfect for heterogeneous map lookups (e.g. querying a map of Owned strings 
 * using a temporary C-string without allocating memory).
 */
class SqliteStringView {
    const char* m_data;
    int m_size;

public:
    /** @brief Sets this object as the return result of a SQLite UDF context. */
    inline void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        sqlite3_result_text(ctx, data(), length(), dtor);
        if (subtype != SQLITE_SUBTYPE_NONE) {
            sqlite3_result_subtype(ctx, subtype);
        }
    }
    template <typename TContext>
    inline void result(TContext& ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const { result(ctx.get(), dtor, subtype); }

    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    inline int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_text(stmt, col, data(), length(), dtor);
    }
    template <typename TStatement>
    inline int bind(TStatement& stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const { return bind(stmt.get(), col, dtor); }

    /**
     * @brief Constructs a view over an existing string buffer.
     * @param data Pointer to the character array.
     * @param size Length of the string in bytes.
     */
    SqliteStringView(const char* data, int size) : m_data(data), m_size(size) {}
    
    /**
     * @brief Implicitly constructs a view from a null-terminated C-string.
     * Enables zero-allocation heterogeneous map lookups like `my_map.find("hello")`.
     */
    SqliteStringView(const char* data) : m_data(data), m_size(SqliteStringUtil::sqlite_strlen(data)) {}
    
    SqliteStringView() : m_data(nullptr), m_size(0) {}
    
    /** @brief Clones the view (shallow copy). */
    inline SqliteStringView clone() const noexcept {
        return *this;
    }

    /** @brief Duplicates the view into an owned dynamic string. */
    inline SqliteStringOwned to_owned() const;

    /** @brief Returns a pointer to the underlying string data. */
    const char* data() const noexcept { return m_data; }
    
    /** @brief Returns the length of the string in bytes. */
    int length() const noexcept { return m_size; }

    /** @brief STL-compatible size accessor. */
    size_t size() const noexcept { return static_cast<size_t>(m_size); }

    /** @brief STL-compatible empty check. */
    bool empty() const noexcept { return m_size == 0; }
    
    /** @brief Computes the MurmurHash2 of the string. */
    unsigned long long hash() const {
        return SqliteStringUtil::hash(m_data, m_size);
    }
    
    bool operator==(const SqliteStringView& other) const {
        return SqliteStringUtil::equal(m_data, m_size, other.m_data, other.m_size);
    }

    bool operator!=(const SqliteStringView& other) const {
        return !(*this == other);
    }

    bool operator<(const SqliteStringView& other) const {
        return SqliteStringUtil::less(m_data, m_size, other.m_data, other.m_size);
    }

    // Heterogeneous lookups
    bool operator==(const SqliteStringOwned& other) const;
    bool operator!=(const SqliteStringOwned& other) const;
    bool operator<(const SqliteStringOwned& other) const;
};

/**
 * @brief Zero-dependency C++ RAII wrapper for SQLite's dynamic string builder (`sqlite3_str`).
 * 
 * Takes ownership of the string memory. Memory is dynamically allocated as you append
 * to it, and automatically freed in the destructor unless `finish()` is called.
 */
class SqliteStringOwned {
    sqlite3_str* m_str;

public:
    /** @brief Sets this object as the return result of a SQLite UDF context. */
    inline void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        sqlite3_result_text(ctx, value(), length(), dtor);
        if (subtype != SQLITE_SUBTYPE_NONE) {
            sqlite3_result_subtype(ctx, subtype);
        }
    }
    template <typename TContext>
    inline void result(TContext& ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const { result(ctx.get(), dtor, subtype); }

    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    inline int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_text(stmt, col, value(), length(), dtor);
    }
    template <typename TStatement>
    inline int bind(TStatement& stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const { return bind(stmt.get(), col, dtor); }

    /**
     * @brief Creates a new string builder tied to a specific database connection.
     * @param db The SQLite database connection.
     */
    explicit SqliteStringOwned(sqlite3* db) { m_str = sqlite3_str_new(db); }
    
    /**
     * @brief Creates a new string builder tied to a UDF execution context.
     * @param ctx The SQLite execution context.
     */
    explicit SqliteStringOwned(sqlite3_context* ctx) { m_str = sqlite3_str_new(sqlite3_context_db_handle(ctx)); }
    
    /**
     * @brief Creates a new string builder not tied to any database.
     * Uses global sqlite3_malloc.
     */
    SqliteStringOwned() { m_str = sqlite3_str_new(nullptr); }

    /**
     * @brief Creates a new string and immediately copies the provided text.
     * Uses global sqlite3_malloc.
     */
    explicit SqliteStringOwned(const char* text) {
        m_str = sqlite3_str_new(nullptr);
        if (text) sqlite3_str_appendall(m_str, text);
    }

    /**
     * @brief Creates a new string and copies len bytes of the provided text.
     */
    SqliteStringOwned(const char* text, int len) {
        m_str = sqlite3_str_new(nullptr);
        if (text && len > 0) sqlite3_str_append(m_str, text, len);
    }
    
    /**
     * @brief Destructor. Automatically frees the memory if it was never finished.
     */
    ~SqliteStringOwned() {
        if (m_str) sqlite3_free(sqlite3_str_finish(m_str));
    }
    
    // Copy constructor (safe duplicate of string builder)
    SqliteStringOwned(const SqliteStringOwned& other) {
        m_str = sqlite3_str_new(nullptr);
        if (other.m_str) {
            sqlite3_str_append(m_str, sqlite3_str_value(other.m_str), sqlite3_str_length(other.m_str));
        }
    }

    SqliteStringOwned& operator=(const SqliteStringOwned& other) {
        if (this != &other) {
            if (m_str) sqlite3_free(sqlite3_str_finish(m_str));
            m_str = sqlite3_str_new(nullptr);
            if (other.m_str) {
                sqlite3_str_append(m_str, sqlite3_str_value(other.m_str), sqlite3_str_length(other.m_str));
            }
        }
        return *this;
    }
    
    // Allow moving
    SqliteStringOwned(SqliteStringOwned&& other) noexcept : m_str(other.m_str) {
        other.m_str = nullptr;
    }

    SqliteStringOwned& operator=(SqliteStringOwned&& other) noexcept {
        if (this != &other) {
            if (m_str) sqlite3_free(sqlite3_str_finish(m_str));
            m_str = other.m_str;
            other.m_str = nullptr;
        }
        return *this;
    }

    /** @brief Clones the owned string by duplicating the dynamic string builder. */
    inline SqliteStringOwned clone() const {
        return SqliteStringOwned(*this);
    }

    /** @brief Creates a zero-allocation view over this owned string. */
    inline SqliteStringView view() const noexcept {
        return SqliteStringView(value(), length());
    }

    /** @brief Appends exactly N bytes of text. */
    SqliteStringOwned& append(const char* zIn, int N) {
        if (m_str) sqlite3_str_append(m_str, zIn, N);
        return *this;
    }
    
    /** @brief Appends a null-terminated string. */
    SqliteStringOwned& appendall(const char* zIn) {
        if (m_str) sqlite3_str_appendall(m_str, zIn);
        return *this;
    }
    
    /** @brief Appends character C exactly N times. */
    SqliteStringOwned& appendchar(int N, char C) {
        if (m_str) sqlite3_str_appendchar(m_str, N, C);
        return *this;
    }
    
    /** @brief Appends formatted text (like printf). */
    SqliteStringOwned& appendf(const char* zFormat, ...) {
        if (!m_str) return *this;
        va_list args;
        va_start(args, zFormat);
        sqlite3_str_vappendf(m_str, zFormat, args);
        va_end(args);
        return *this;
    }
    
    /** @brief Appends formatted text using a va_list. */
    SqliteStringOwned& vappendf(const char* zFormat, va_list args) {
        if (m_str) sqlite3_str_vappendf(m_str, zFormat, args);
        return *this;
    }
    
    /** @brief Resets the builder to an empty state. */
    void reset() {
        if (m_str) sqlite3_str_reset(m_str);
    }
    
    /** @brief Returns the current SQLite error code (e.g. SQLITE_NOMEM). */
    int errcode() const {
        return m_str ? sqlite3_str_errcode(m_str) : SQLITE_NOMEM;
    }
    
    /** @brief Checks if the string builder holds a valid allocation without error. */
    bool is_valid() const {
        return m_str != nullptr && sqlite3_str_errcode(m_str) == SQLITE_OK;
    }

    /** @brief Explicit boolean conversion checking validity. */
    explicit operator bool() const {
        return is_valid();
    }

    /** @brief Returns the current length of the string in bytes. */
    int length() const {
        return m_str ? sqlite3_str_length(m_str) : 0;
    }
    
    /** @brief Returns a read-only pointer to the current built string. */
    const char* value() const {
        return m_str ? sqlite3_str_value(m_str) : nullptr;
    }
    
    /**
     * @brief Finishes the string and surrenders memory ownership to the caller.
     * @return The raw char* array. The caller must manually call `sqlite3_free()` on it.
     */
    char* finish() {
        if (!m_str) return nullptr;
        char* result = sqlite3_str_finish(m_str);
        m_str = nullptr; 
        return result;
    }

    /** @brief Computes the MurmurHash2 of the built string. */
    unsigned long long hash() const {
        return SqliteStringUtil::hash(value(), length());
    }
    
    bool operator==(const SqliteStringOwned& other) const {
        return SqliteStringUtil::equal(value(), length(), other.value(), other.length());
    }

    bool operator!=(const SqliteStringOwned& other) const {
        return !(*this == other);
    }

    bool operator<(const SqliteStringOwned& other) const {
        return SqliteStringUtil::less(value(), length(), other.value(), other.length());
    }

    // Heterogeneous lookups
    bool operator==(const SqliteStringView& other) const {
        return SqliteStringUtil::equal(value(), length(), other.data(), other.length());
    }

    bool operator!=(const SqliteStringView& other) const {
        return !(*this == other);
    }

    bool operator<(const SqliteStringView& other) const {
        return SqliteStringUtil::less(value(), length(), other.data(), other.length());
    }
};

// Complete heterogeneous lookups for SqliteStringView
inline SqliteStringOwned SqliteStringView::to_owned() const {
    return SqliteStringOwned(m_data, m_size);
}

inline bool SqliteStringView::operator==(const SqliteStringOwned& other) const {
    return SqliteStringUtil::equal(m_data, m_size, other.value(), other.length());
}

inline bool SqliteStringView::operator!=(const SqliteStringOwned& other) const {
    return !(*this == other);
}

inline bool SqliteStringView::operator<(const SqliteStringOwned& other) const {
    return SqliteStringUtil::less(m_data, m_size, other.value(), other.length());
}

// ============================================================================
// 2. BLOB TYPES
// ============================================================================

/**
 * @namespace SqliteBlobUtil
 * @brief Shared hashing and comparison utilities for binary blobs.
 */
namespace SqliteBlobUtil {
    /**
     * @brief Computes a 64-bit MurmurHash2 of a binary buffer.
     */
    inline unsigned long long hash(const void* val, int len) {
        return SqliteHashUtil::hash(val, len);
    }
    
    /**
     * @brief Checks if two binary buffers are exactly equal.
     */
    inline bool equal(const void* val1, int len1, const void* val2, int len2) {
        if (len1 != len2) return false;
        if (len1 == 0) return true;
        if (val1 == val2) return true;
        if (!val1 || !val2) return false;
        return memcmp(val1, val2, len1) == 0;
    }
    
    /**
     * @brief Performs a lexicographical less-than comparison of two binary buffers.
     */
    inline bool less(const void* val1, int len1, const void* val2, int len2) {
        if (!val1 && !val2) return false;
        if (!val1) return true;
        if (!val2) return false;
        return SqliteMemoryUtil::memcmp_less(val1, len1, val2, len2);
    }
}

class SqliteBlobOwned;

/**
 * @brief Zero-cost, non-owning C++ wrapper for binary blobs.
 * 
 * Perfect for reading payloads without triggering memory allocations, 
 * or querying map keys heterogeneously.
 */
class SqliteBlobView {
    const void* m_data;
    int m_size;

public:
    /** @brief Sets this object as the return result of a SQLite UDF context. */
    inline void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        sqlite3_result_blob(ctx, data(), size(), dtor);
        if (subtype != SQLITE_SUBTYPE_NONE) {
            sqlite3_result_subtype(ctx, subtype);
        }
    }
    template <typename TContext>
    inline void result(TContext& ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const { result(ctx.get(), dtor, subtype); }

    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    inline int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_blob(stmt, col, data(), size(), dtor);
    }
    template <typename TStatement>
    inline int bind(TStatement& stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const { return bind(stmt.get(), col, dtor); }

    /**
     * @brief Constructs a view over an existing binary buffer.
     * @param data Pointer to the binary payload.
     * @param size Length of the payload in bytes.
     */
    SqliteBlobView(const void* data, int size) : m_data(data), m_size(size) {}
    SqliteBlobView() : m_data(nullptr), m_size(0) {}
    
    /** @brief Clones the view (shallow copy). */
    inline SqliteBlobView clone() const noexcept {
        return *this;
    }

    /** @brief Duplicates the view into an owned heap blob. */
    inline SqliteBlobOwned to_owned() const;

    /** @brief Returns a pointer to the underlying binary data. */
    const void* data() const noexcept { return m_data; }
    
    /** @brief Returns the size of the binary payload in bytes. */
    int size() const noexcept { return m_size; }

    /** @brief Computes the MurmurHash2 of the binary payload. */
    unsigned long long hash() const {
        return SqliteBlobUtil::hash(m_data, m_size);
    }
    
    bool operator==(const SqliteBlobView& other) const {
        return SqliteBlobUtil::equal(m_data, m_size, other.m_data, other.m_size);
    }

    bool operator!=(const SqliteBlobView& other) const {
        return !(*this == other);
    }

    bool operator<(const SqliteBlobView& other) const {
        return SqliteBlobUtil::less(m_data, m_size, other.m_data, other.m_size);
    }

    // Heterogeneous lookups
    bool operator==(const SqliteBlobOwned& other) const;
    bool operator!=(const SqliteBlobOwned& other) const;
    bool operator<(const SqliteBlobOwned& other) const;
};

/**
 * @brief Zero-dependency C++ RAII wrapper for raw binary blobs.
 * 
 * Takes ownership of binary data by copying it using `sqlite3_malloc`.
 * Perfect for permanent storage as a Map Key.
 */
class SqliteBlobOwned {
    void* m_data;
    int m_size;

public:
    /** @brief Sets this object as the return result of a SQLite UDF context. */
    inline void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        sqlite3_result_blob(ctx, data(), size(), dtor);
        if (subtype != SQLITE_SUBTYPE_NONE) {
            sqlite3_result_subtype(ctx, subtype);
        }
    }
    template <typename TContext>
    inline void result(TContext& ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const { result(ctx.get(), dtor, subtype); }

    /** @brief Relinquishes ownership of the raw malloc'd buffer (Zero-copy transfer). */
    void* release() noexcept {
        void* temp = m_data;
        m_data = nullptr;
        m_size = 0;
        return temp;
    }

    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    inline int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_blob(stmt, col, data(), size(), dtor);
    }
    template <typename TStatement>
    inline int bind(TStatement& stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const { return bind(stmt.get(), col, dtor); }

    /** @brief Constructs an empty binary blob. */
    SqliteBlobOwned() : m_data(nullptr), m_size(0) {}

    /**
     * @brief Allocates memory and copies the provided binary data.
     * @param data Pointer to the binary payload to copy.
     * @param size Length of the payload in bytes.
     */
    SqliteBlobOwned(const void* data, int size) : m_data(nullptr), m_size(0) {
        if (size > 0) {
            m_data = sqlite3_malloc(size);
            if (m_data) {
                m_size = size;
                if (data) memcpy(m_data, data, size);
            }
        }
    }
    
    /** @brief Destructor. Automatically frees the allocated memory. */
    ~SqliteBlobOwned() {
        if (m_data) sqlite3_free(m_data);
    }
    
    // Disallow implicit copying to prevent accidental heavy allocations
    SqliteBlobOwned(const SqliteBlobOwned&) = delete;
    SqliteBlobOwned& operator=(const SqliteBlobOwned&) = delete;
    
    /** @brief Clones the owned blob via deep memory allocation. */
    inline SqliteBlobOwned clone() const {
        return SqliteBlobOwned(m_data, m_size);
    }

    /** @brief Creates a zero-allocation view over this owned blob. */
    inline SqliteBlobView view() const noexcept {
        return SqliteBlobView(m_data, m_size);
    }

    // Allow moving
    SqliteBlobOwned(SqliteBlobOwned&& other) noexcept : m_data(other.m_data), m_size(other.m_size) {
        other.m_data = nullptr;
        other.m_size = 0;
    }

    SqliteBlobOwned& operator=(SqliteBlobOwned&& other) noexcept {
        if (this != &other) {
            if (m_data) sqlite3_free(m_data);
            m_data = other.m_data;
            m_size = other.m_size;
            other.m_data = nullptr;
            other.m_size = 0;
        }
        return *this;
    }

    /** @brief Returns a read-only pointer to the owned binary data. */
    const void* data() const noexcept { return m_data; }
    
    /** @brief Returns the size of the owned binary payload in bytes. */
    int size() const noexcept { return m_size; }

    /** @brief Checks if the owned blob holds a valid allocation (or is cleanly empty). */
    bool is_valid() const noexcept {
        return m_size == 0 || m_data != nullptr;
    }

    /** @brief Explicit boolean conversion checking validity. */
    explicit operator bool() const noexcept {
        return is_valid();
    }

    /** @brief Computes the MurmurHash2 of the owned payload. */
    unsigned long long hash() const {
        return SqliteBlobUtil::hash(m_data, m_size);
    }
    
    bool operator==(const SqliteBlobOwned& other) const {
        return SqliteBlobUtil::equal(m_data, m_size, other.m_data, other.m_size);
    }

    bool operator!=(const SqliteBlobOwned& other) const {
        return !(*this == other);
    }

    bool operator<(const SqliteBlobOwned& other) const {
        return SqliteBlobUtil::less(m_data, m_size, other.m_data, other.m_size);
    }

    // Heterogeneous lookups
    bool operator==(const SqliteBlobView& other) const {
        return SqliteBlobUtil::equal(m_data, m_size, other.data(), other.size());
    }

    bool operator!=(const SqliteBlobView& other) const {
        return !(*this == other);
    }

    bool operator<(const SqliteBlobView& other) const {
        return SqliteBlobUtil::less(m_data, m_size, other.data(), other.size());
    }
};

// Complete heterogeneous lookups for SqliteBlobView
inline SqliteBlobOwned SqliteBlobView::to_owned() const {
    return SqliteBlobOwned(m_data, m_size);
}

inline bool SqliteBlobView::operator==(const SqliteBlobOwned& other) const {
    return SqliteBlobUtil::equal(m_data, m_size, other.data(), other.size());
}

inline bool SqliteBlobView::operator!=(const SqliteBlobOwned& other) const {
    return !(*this == other);
}

inline bool SqliteBlobView::operator<(const SqliteBlobOwned& other) const {
    return SqliteBlobUtil::less(m_data, m_size, other.data(), other.size());
}

// ============================================================================
// 3. VALUE TYPES
// ============================================================================

/**
 * @namespace SqliteValueUtil
 * @brief Shared hashing and comparison utilities for SQLite's dynamic `sqlite3_value`.
 */
namespace SqliteValueUtil {
    /**
     * @brief Safely extracts the type ID of a sqlite3_value.
     */
    inline int type(const sqlite3_value* val) {
        return val ? sqlite3_value_type(const_cast<sqlite3_value*>(val)) : SQLITE_NULL;
    }

    /**
     * @brief Computes a 64-bit polymorphic hash of a sqlite3_value.
     * Hashes the type ID first to guarantee no collisions between Ints, Floats, and Strings.
     */
    inline unsigned long long hash(const sqlite3_value* val) {
        if (!val) return SqliteHashUtil::DEFAULT_SEED;
        
        sqlite3_value* mut_val = const_cast<sqlite3_value*>(val);
        int t = sqlite3_value_type(mut_val);
        unsigned long long h = SqliteHashUtil::DEFAULT_SEED;
        
        switch (t) {
            case SQLITE_INTEGER: {
                sqlite3_int64 i_val = sqlite3_value_int64(mut_val);
                h = SqliteHashUtil::mix(h, &i_val, sizeof(i_val));
                break;
            }
            case SQLITE_FLOAT: {
                double d_val = sqlite3_value_double(mut_val);
                if (d_val == 0.0) d_val = 0.0; // Forces -0.0 to +0.0
                h = SqliteHashUtil::mix(h, &d_val, sizeof(d_val));
                break;
            }
            case SQLITE_TEXT: {
                h = SqliteHashUtil::mix(h, sqlite3_value_text(mut_val), sqlite3_value_bytes(mut_val));
                break;
            }
            case SQLITE_BLOB: {
                h = SqliteHashUtil::mix(h, sqlite3_value_blob(mut_val), sqlite3_value_bytes(mut_val));
                break;
            }
            case SQLITE_NULL:
            default:
                break;
        }
        return h;
    }

    /**
     * @brief Performs a strict polymorphic equality check between two sqlite3_values.
     * Will return false if types differ (e.g. Integer 5 != Float 5.0).
     */
    inline bool equal(const sqlite3_value* v1, const sqlite3_value* v2) {
        int t1 = type(v1);
        int t2 = type(v2);
        if (t1 != t2) return false;
        
        sqlite3_value* mut_v1 = const_cast<sqlite3_value*>(v1);
        sqlite3_value* mut_v2 = const_cast<sqlite3_value*>(v2);

        switch (t1) {
            case SQLITE_INTEGER: 
                return sqlite3_value_int64(mut_v1) == sqlite3_value_int64(mut_v2);
            case SQLITE_FLOAT: {
                double d1 = sqlite3_value_double(mut_v1);
                double d2 = sqlite3_value_double(mut_v2);
                // Explicitly handle NaN == NaN for map key stability
                if (d1 != d1 && d2 != d2) return true; 
                return d1 == d2;
            }
            case SQLITE_TEXT: {
                int bytes1 = sqlite3_value_bytes(mut_v1);
                int bytes2 = sqlite3_value_bytes(mut_v2);
                if (bytes1 != bytes2) return false;
                const char* txt1 = reinterpret_cast<const char*>(sqlite3_value_text(mut_v1));
                const char* txt2 = reinterpret_cast<const char*>(sqlite3_value_text(mut_v2));
                if (!txt1 || !txt2) return txt1 == txt2;
                return memcmp(txt1, txt2, bytes1) == 0;
            }
            case SQLITE_BLOB: {
                int bytes1 = sqlite3_value_bytes(mut_v1);
                int bytes2 = sqlite3_value_bytes(mut_v2);
                if (bytes1 != bytes2) return false;
                const char* blob1 = reinterpret_cast<const char*>(sqlite3_value_blob(mut_v1));
                const char* blob2 = reinterpret_cast<const char*>(sqlite3_value_blob(mut_v2));
                if (!blob1 || !blob2) return blob1 == blob2;
                return memcmp(blob1, blob2, bytes1) == 0;
            }
            case SQLITE_NULL: 
                return true;
        }
        return false;
    }

    /**
     * @brief Performs a polymorphic less-than comparison of two sqlite3_values.
     */
    inline bool less(const sqlite3_value* v1, const sqlite3_value* v2) {
        auto type_rank = [](int t) -> int {
            switch (t) {
                case SQLITE_NULL:    return 0; // NULL is smallest
                case SQLITE_INTEGER: 
                case SQLITE_FLOAT:   return 1; // Both are Numeric class
                case SQLITE_TEXT:    return 2;
                case SQLITE_BLOB:    return 3;
                default:             return 0;
            }
        };

        int t1 = type(v1);
        int t2 = type(v2);
        int r1 = type_rank(t1);
        int r2 = type_rank(t2);
        
        if (r1 != r2) return r1 < r2;
        
        sqlite3_value* mut_v1 = const_cast<sqlite3_value*>(v1);
        sqlite3_value* mut_v2 = const_cast<sqlite3_value*>(v2);

        // Both are Numeric: compare as double, or as int64 if both are integers
        if (r1 == 1) {
            if (t1 == SQLITE_INTEGER && t2 == SQLITE_INTEGER) {
                return sqlite3_value_int64(mut_v1) < sqlite3_value_int64(mut_v2);
            }
            double d1 = sqlite3_value_double(mut_v1);
            double d2 = sqlite3_value_double(mut_v2);
            
            // 1. NaN sorting stability (Sort NaNs first)
            bool isnan1 = (d1 != d1);
            bool isnan2 = (d2 != d2);
            if (isnan1 && !isnan2) return true;  
            if (!isnan1 && isnan2) return false; 
            
            // 2. Standard numeric comparison
            if (d1 != d2) return d1 < d2;
            
            // 3. TIE-BREAKER: Maintain strict typing in std::map!
            return t1 < t2;
        }

        switch (t1) {
            case SQLITE_TEXT: 
            case SQLITE_BLOB: {
                int len1 = sqlite3_value_bytes(mut_v1);
                int len2 = sqlite3_value_bytes(mut_v2);
                const char* data1 = (t1 == SQLITE_TEXT) ? reinterpret_cast<const char*>(sqlite3_value_text(mut_v1)) : reinterpret_cast<const char*>(sqlite3_value_blob(mut_v1));
                const char* data2 = (t2 == SQLITE_TEXT) ? reinterpret_cast<const char*>(sqlite3_value_text(mut_v2)) : reinterpret_cast<const char*>(sqlite3_value_blob(mut_v2));
                
                if (!data1 && !data2) return false;
                if (!data1) return true;
                if (!data2) return false;
                return SqliteMemoryUtil::memcmp_less(data1, len1, data2, len2);
            }
            case SQLITE_NULL: 
                return false;
        }
        return false;
    }
}

class SqliteValueOwned;

/**
 * @brief Zero-cost, non-owning C++ wrapper for `sqlite3_value`.
 * 
 * Perfect for reading values supplied by SQLite (`argv`) without triggering 
 * any memory allocations. Can be heterogeneously compared against `SqliteValueOwned`.
 */
class SqliteValueView {
    const sqlite3_value* m_val;

public:
    /** @brief Sets this object as the return result of a SQLite UDF context. */
    inline void result(sqlite3_context* ctx) const {
        sqlite3_result_value(ctx, const_cast<sqlite3_value*>(m_val));
        uint8_t sub = subtype();
        if (sub != SQLITE_SUBTYPE_NONE) {
            sqlite3_result_subtype(ctx, sub);
        }
    }
    template <typename TContext>
    inline void result(TContext& ctx) const { result(ctx.get()); }

    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    inline int bind(sqlite3_stmt* stmt, int col) const {
        if (!m_val) return sqlite3_bind_null(stmt, col);
        return sqlite3_bind_value(stmt, col, m_val);
    }
    template <typename TStatement>
    inline int bind(TStatement& stmt, int col) const { return bind(stmt.get(), col); }

    /**
     * @brief Constructs a view over a temporary `sqlite3_value`.
     * @param val The transient value pointer passed from SQLite.
     */
    explicit SqliteValueView(const sqlite3_value* val) : m_val(val) {}
    
    /** @brief Constructs a view over a prepared statement column value. */
    static inline SqliteValueView from_column(sqlite3_stmt* stmt, int col) noexcept {
        return SqliteValueView(sqlite3_column_value(stmt, col));
    }
    template <typename TStatement>
    static inline SqliteValueView from_column(TStatement& stmt, int col) noexcept {
        return from_column(stmt.get(), col);
    }

    // Copyable and Movable since it's just a pointer wrapper
    SqliteValueView(const SqliteValueView&) = default;
    SqliteValueView& operator=(const SqliteValueView&) = default;

    /** @brief Clones the view (shallow copy). */
    inline SqliteValueView clone() const noexcept {
        return *this;
    }

    /** @brief Duplicates transient view into an owned 16-byte value with inline SBO optimization. */
    inline SqliteValueOwned to_owned() const;

    /** @brief Returns the underlying `sqlite3_value` pointer. */
    const sqlite3_value* get() const noexcept { return m_val; }
    
    /** @brief Returns the SQLite datatype (e.g. SQLITE_INTEGER). */
    int type() const noexcept { return SqliteValueUtil::type(m_val); }

    /** @brief Storage class type predicates. */
    inline bool is_null()    const noexcept { return type() == SQLITE_NULL; }
    inline bool is_integer() const noexcept { return type() == SQLITE_INTEGER; }
    inline bool is_float()   const noexcept { return type() == SQLITE_FLOAT; }
    inline bool is_text()    const noexcept { return type() == SQLITE_TEXT; }
    inline bool is_blob()    const noexcept { return type() == SQLITE_BLOB; }
    inline bool is_numeric() const noexcept { return sqlite3IsNumericAffinity(affinity()); }

    /** @brief Returns the Native SQLite affinity character derived from the value's type. */
    inline char affinity() const noexcept {
        switch (type()) {
            case SQLITE_INTEGER: return SQLITE_AFF_INTEGER;
            case SQLITE_FLOAT:   return SQLITE_AFF_REAL;
            case SQLITE_TEXT:    return SQLITE_AFF_TEXT;
            case SQLITE_BLOB:    return SQLITE_AFF_BLOB;
            case SQLITE_NULL:
            default:             return SQLITE_AFF_NONE;
        }
    }

    /** @brief Returns the 8-bit SQLite subtype (e.g. SQLITE_SUBTYPE_JSON, SQLITE_SUBTYPE_UUID). */
    inline uint8_t subtype() const noexcept {
        return m_val ? static_cast<uint8_t>(sqlite3_value_subtype(const_cast<sqlite3_value*>(m_val))) : SQLITE_SUBTYPE_NONE;
    }

    /** @brief Subtype query predicates. */
    inline bool is_json()       const noexcept { return subtype() == SQLITE_SUBTYPE_JSON; }
    inline bool is_decimal()    const noexcept { return subtype() == SQLITE_SUBTYPE_DECIMAL; }
    inline bool is_uuid()       const noexcept { return subtype() == SQLITE_SUBTYPE_UUID; }
    inline bool is_vector()     const noexcept { return subtype() == SQLITE_SUBTYPE_VECTOR; }
    inline bool is_geometry()   const noexcept { return subtype() == SQLITE_SUBTYPE_GEOMETRY; }
    inline bool is_datetime()   const noexcept { return subtype() == SQLITE_SUBTYPE_DATETIME; }
    inline bool is_bool()       const noexcept { return subtype() == SQLITE_SUBTYPE_BOOL; }
    inline bool is_compressed() const noexcept { return subtype() == SQLITE_SUBTYPE_COMPRESSED; }
    
    /** @brief Internal helper to access integer value for heterogeneous lookups. */
    sqlite3_int64 as_int64() const { return m_val ? sqlite3_value_int64(const_cast<sqlite3_value*>(m_val)) : 0; }

    /** @brief Internal helper to access integer value as 32-bit signed int. */
    inline int as_int() const noexcept { return static_cast<int>(as_int64()); }

    /** @brief Internal helper to access double value for heterogeneous lookups. */
    double as_double() const { return m_val ? sqlite3_value_double(const_cast<sqlite3_value*>(m_val)) : 0.0; }

    /** @brief Evaluates integer value as boolean. */
    inline bool as_bool() const noexcept { return as_int64() != 0; }

    /** @brief Checks if the view wraps a non-null sqlite3_value pointer. */
    inline bool is_valid() const noexcept { return m_val != nullptr; }

    /** @brief Explicit boolean conversion: returns true if valid and not NULL. */
    explicit operator bool() const noexcept { return is_valid() && !is_null(); }

    /** @brief Access string data as a zero-allocation SqliteStringView. */
    SqliteStringView as_text() const {
        if (!m_val) return SqliteStringView(nullptr, 0);
        const char* text = reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(val_mut())));
        return SqliteStringView(text, text ? sqlite3_value_bytes(const_cast<sqlite3_value*>(val_mut())) : 0);
    }

    /** @brief Access binary data as a zero-allocation SqliteBlobView. */
    SqliteBlobView as_blob() const {
        if (!m_val) return SqliteBlobView(nullptr, 0);
        const void* blob = sqlite3_value_blob(const_cast<sqlite3_value*>(val_mut()));
        return SqliteBlobView(blob, blob ? sqlite3_value_bytes(const_cast<sqlite3_value*>(val_mut())) : 0);
    }
    
private:
    inline sqlite3_value* val_mut() const noexcept { return const_cast<sqlite3_value*>(m_val); }

public:
    /** @brief Computes the polymorphic hash. */
    unsigned long long hash() const {
        return SqliteValueUtil::hash(m_val);
    }
    
    bool operator==(const SqliteValueView& other) const {
        return SqliteValueUtil::equal(m_val, other.m_val);
    }

    bool operator!=(const SqliteValueView& other) const {
        return !(*this == other);
    }

    bool operator<(const SqliteValueView& other) const {
        return SqliteValueUtil::less(m_val, other.m_val);
    }

    // Heterogeneous lookups
    bool operator==(const SqliteValueOwned& other) const;
    bool operator!=(const SqliteValueOwned& other) const;
    bool operator<(const SqliteValueOwned& other) const;
};

// ============================================================================
// DUAL-REPRESENTATION 16-BYTE MEMORY MODEL ARCHITECTURE
// ============================================================================
//
// SqliteValueOwned uses a tagged union of two 16-byte structs to achieve:
//
// 1. EXACT 16-BYTE FOOTPRINT (128 BITS):
//    Fits comfortably across two 64-bit CPU registers or a single 128-bit
//    SIMD register, optimizing cache line density (4 values per 64B cache line).
//
// 2. ZERO-ALLOCATION SMALL BUFFER OPTIMIZATION (SBO):
//    - Short strings (<= 14 bytes) and blobs (<= 14 bytes) are stored
//      directly inline without ever invoking sqlite3_malloc.
//    - Primitives (64-bit int, 64-bit double, Null) are stored inline in registers.
//    - Large text/blobs (> 14 bytes) store a raw byte buffer (pData) on the heap.
//
// 3. ZERO-BRANCH SHARED METADATA TAIL (OFFSETS 14 & 15):
//    Both Struct 1 and Struct 2 align their control metadata at the exact same offsets:
//    - Offset 14 (subtype): Direct access to the 8-bit SQLite subtype ('J', 'D', 'U', etc.)
//      without branching on type or heap allocation flags. Enables inline JSON/decimals.
//    - Offset 15 (tag): Bit-packed control byte encoding type, heap flag, and inline length.
//
// ============================================================================
// BITFIELD SPECIFICATION: THE CONTROL TAG BYTE (OFFSET 15)
// ============================================================================
//
// The single byte at Offset 15 acts as a high-density, multi-purpose control
// register shared by both representation structs:
//
//   Bit:     7       6       5       4       3       2       1       0
//        ┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
//        │      DATA TYPE        │ HEAP  │     INLINE PAYLOAD LENGTH     │
//        │  (0x01 .. 0x05)       │ FLAG  │         (0 .. 14)             │
//        └───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
//
// 1. DATA TYPE (Bits 7..5, Mask: 0xE0, Shift: >> 5):
//    Stores the 3-bit SQLite storage class enum:
//    - 001 (1) : SQLITE_INTEGER
//    - 010 (2) : SQLITE_FLOAT
//    - 011 (3) : SQLITE_TEXT
//    - 100 (4) : SQLITE_BLOB
//    - 101 (5) : SQLITE_NULL
//    Extracted via: `type() = static_cast<int>(raw >> 5);` (1 CPU instruction, branchless)
//
// 2. HEAP ALLOCATION FLAG (Bit 4, Mask: 0x10):
//    Indicates whether the payload is heap-managed or stored in-situ:
//    - 0 : In-situ / Inline representation (Struct 2 `buf` or primitive `iValue`/`dValue`)
//    - 1 : Heap representation (Struct 1 `payload.pData` points to dynamically allocated buffer)
//    Extracted via: `is_heap() = (raw & 0x10) != 0;`
//
// 3. INLINE PAYLOAD LENGTH (Bits 3..0, Mask: 0x0F):
//    Encodes the byte length of the inline buffer (0 to 14 bytes):
//    - For SQLITE_TEXT: exact string length (0..14 chars)
//    - For SQLITE_BLOB: exact binary blob size (0..14 raw bytes)
//    - For primitives (INTEGER/FLOAT/NULL): unused (set to 0)
//    Extracted via: `len() = raw & 0x0F;`
//
// ============================================================================

/**
 * @brief 1-Byte bitfield control tag shared by all 16-byte value representations.
 */
struct SqliteOwnedValueTag {
    uint8_t raw;

    /** @brief Packs type, heap flag, and inline length into the single tag byte. */
    inline void set(uint8_t type, bool is_heap, uint8_t len = 0) noexcept {
        raw = static_cast<uint8_t>(
            ((type & 0x07) << 5) |
            ((is_heap ? 1 : 0) << 4) |
            (len & 0x0F)
        );
    }

    /** @brief Returns the SQLite storage class datatype (e.g. SQLITE_INTEGER..SQLITE_NULL). */
    inline int type() const noexcept {
        return static_cast<int>(raw >> 5);
    }

    /** @brief Checks if the value holds a heap-allocated buffer. */
    inline bool is_heap() const noexcept {
        return (raw & 0x10) != 0;
    }

    /** @brief Returns the byte length of inline text or blob payload (0..14). */
    inline uint8_t len() const noexcept {
        return static_cast<uint8_t>(raw & 0x0F);
    }

    /** @brief Resets/clears the tag byte to 0x00 (uninitialized / empty). */
    inline void clear() noexcept {
        raw = 0;
    }

    /**
     * @brief Checks if the tag represents an active, initialized SQLite value (raw >= 0x20).
     * 
     * ### Bitfield Encoding & 0x20 Threshold Mechanics:
     * SQLite data types (SQLITE_INTEGER=1, SQLITE_FLOAT=2, SQLITE_TEXT=3, SQLITE_BLOB=4, SQLITE_NULL=5)
     * are encoded in the high 3 bits (bits 5..7) via `type << 5`:
     * - `SQLITE_INTEGER` (1): `0b001_00000 = 0x20` (32)
     * - `SQLITE_FLOAT`   (2): `0b010_00000 = 0x40` (64)
     * - `SQLITE_TEXT`    (3): `0b011_00000 = 0x60` (96)
     * - `SQLITE_BLOB`    (4): `0b100_00000 = 0x80` (128)
     * - `SQLITE_NULL`    (5): `0b101_00000 = 0xA0` (160)
     * 
     * Every valid, initialized SQLite value has a type in [1..5], guaranteeing its tag is `raw >= 0x20`.
     * Uninitialized slots, cleared elements, and row container markers have `type == 0` (`raw == 0x00 < 0x20`).
     * 
     * @return True if tag represents an active SQLite value (type != 0).
     */
    inline bool is_active() const noexcept {
        return raw >= 0x20;
    }

    /**
     * @brief Checks if raw tag byte represents an active, initialized SQLite value (tag_byte >= 0x20).
     * @param tag_byte Raw 8-bit tag byte.
     * @return True if tag_byte >= 0x20.
     */
    static inline bool is_active(uint8_t tag_byte) noexcept {
        return tag_byte >= 0x20;
    }

    /**
     * @brief Checks if the tag indicates a heap-allocated container buffer (raw == 0x00 && ptr != nullptr).
     * @param ptr The container's heap pointer.
     * @return True if tag is 0x00 (empty/discriminator) and ptr is non-null.
     */
    inline bool is_heap_container(const void* ptr) const noexcept {
        return (raw == 0) & (ptr != nullptr);
    }

    /**
     * @brief Checks if a container at the given base pointer is in heap mode (byte 15 == 0x00 && heap_ptr != nullptr).
     * @param container_this Pointer to the container (e.g. SqliteValueVec).
     * @param heap_ptr The container's heap buffer pointer.
     * @return True if byte 15 is 0x00 and heap_ptr != nullptr.
     */
    static inline bool is_container_heap(const void* container_this, const void* heap_ptr) noexcept {
        if (!container_this || !heap_ptr) return false;
        const uint8_t* raw_bytes = static_cast<const uint8_t*>(container_this);
        return raw_bytes[15] == 0x00;
    }
};
static_assert(sizeof(SqliteOwnedValueTag) == 1, "SqliteOwnedValueTag must be exactly 1 byte!");

/**
 * @brief 1-Byte bitfield sub-tag control byte (Offset 14) shared by all 16-byte value representations.
 * 
 * ### Bit Allocation:
 * - Bit 7 (0x80)     : Immutability flag (1 = immutable, 0 = mutable)
 * - Bits 6..0 (0x7F) : 7-bit SQLite subtype (0..127, covering standard SQLite ASCII subtypes 'B','D','G','J','T','U','V','Z', etc.)
 */
struct SqliteOwnedValueSubTag {
    uint8_t raw;

    /** @brief Sets subtype and optional immutability flag. */
    inline void set(uint8_t sub, bool is_imm = false) noexcept {
        raw = static_cast<uint8_t>((sub & 0x7F) | (is_imm ? 0x80 : 0x00));
    }

    /** @brief Returns the 7-bit SQLite subtype (0..127). */
    inline uint8_t subtype() const noexcept {
        return static_cast<uint8_t>(raw & 0x7F);
    }

    /** @brief Sets the 7-bit SQLite subtype while preserving the immutability flag. */
    inline void set_subtype(uint8_t sub) noexcept {
        raw = static_cast<uint8_t>((raw & 0x80) | (sub & 0x7F));
    }

    /** @brief Checks if the value is marked immutable. */
    inline bool is_immutable() const noexcept {
        return (raw & 0x80) != 0;
    }

    /** @brief Marks the value as immutable. */
    inline void mark_immutable() noexcept {
        raw |= 0x80;
    }

    /** @brief Unmarks the value as immutable (makes it mutable). */
    inline void unmark_immutable() noexcept {
        raw &= 0x7F;
    }

    /** @brief Sets or clears the immutability flag. */
    inline void set_immutable(bool imm = true) noexcept {
        if (imm) mark_immutable();
        else unmark_immutable();
    }

    /** @brief Resets the subtag to 0x00 (SQLITE_SUBTYPE_NONE and mutable). */
    inline void clear() noexcept {
        raw = 0;
    }
};
static_assert(sizeof(SqliteOwnedValueSubTag) == 1, "SqliteOwnedValueSubTag must be exactly 1 byte!");

/**
 * @brief Representation 1: Numbers, Nulls, and Large Heap-Allocated Payloads (16 Bytes).
 * 
 * Layout:
 * - Bytes  0..7  (Offset 0..7)  : 8-byte aligned primitive union (iValue, dValue, pData)
 * - Bytes  8..11 (Offset 8..11) : 4-byte heap payload length (heap_len)
 * - Byte   12    (Offset 12)    : 1-byte Native SQLite Affinity character ('@', 'A'..'F')
 * - Byte   13    (Offset 13)    : 1-byte reserved for future ABI extensions (reserved)
 * - Byte   14    (Offset 14)    : 1-byte Sub-Tag Byte: Subtype + Immutability (SHARED WITH INLINE STRUCT)
 * - Byte   15    (Offset 15)    : 1-byte Control Tag (SHARED WITH INLINE STRUCT)
 */
struct SqliteTypeRep {
    union {
        sqlite3_int64  iValue;   // 8 bytes (Offset 0..7: 64-bit integer)
        double         dValue;   // 8 bytes (Offset 0..7: IEEE-754 double)
        char*          pData;    // 8 bytes (Offset 0..7: Heap buffer for Text/Blob)
    } payload;
    
    int32_t                 heap_len; // 4 bytes (Offset 8..11: Byte length for heap text/blob)
    char                    affinity; // 1 byte  (Offset 12: Native SQLite affinity '@', 'A'..'F')
    uint8_t                 reserved; // 1 byte  (Offset 13: Reserved for future ABI extensions)
    SqliteOwnedValueSubTag  subtag;   // 1 byte  (Offset 14: Shared Sub-Tag: Subtype + Immutability)
    SqliteOwnedValueTag     tag;      // 1 byte  (Offset 15: Bit-packed Type + Heap Flag + Length)
};
static_assert(sizeof(SqliteTypeRep) == 16, "SqliteTypeRep must be exactly 16 bytes!");

/**
 * @brief Representation 2: Inline Buffer for Strings & Binary Blobs (16 Bytes).
 * 
 * Layout:
 * - Bytes  0..13 (Offset 0..13) : 14-byte inline buffer (14 text chars OR 14 raw blob bytes)
 * - Byte   14    (Offset 14)    : 1-byte Sub-Tag Byte: Subtype + Immutability (SHARED WITH TYPE STRUCT)
 * - Byte   15    (Offset 15)    : 1-byte Control Tag (SHARED WITH TYPE STRUCT)
 */
struct InlineBufferRep {
    char                    buf[14];  // 14 bytes (Offset 0..13: 14 chars OR 14 raw blob bytes)
    SqliteOwnedValueSubTag  subtag;   // 1 byte   (Offset 14: Shared Sub-Tag: Subtype + Immutability)
    SqliteOwnedValueTag     tag;      // 1 byte   (Offset 15: Bit-packed Type + Heap Flag + Length)
};
static_assert(sizeof(InlineBufferRep) == 16, "InlineBufferRep must be exactly 16 bytes!");

/**
 * @brief Heavy, memory-managed polymorphic C++ RAII wrapper for `sqlite3_value`.
 * 
 * Implements a dual-representation 16-byte value layout that transparently handles
 * primitives, inline short strings/blobs (<= 14 bytes), and heap-managed buffers.
 * Features full support for SQLite data affinities and the complete SQLite subtype registry.
 */
class SqliteValueOwned {
private:
    static const int MAX_INLINE_BUF_LEN = 14;

    union {
        SqliteTypeRep   m_sqlite;  // Struct 1 (Primitives & Heap values)
        InlineBufferRep m_inline;  // Struct 2 (Inline Strings & Blobs)
        uint64_t        m_align;   // Forces 8-byte alignment
    };

    /**
     * @brief Safely releases heap memory if currently owning an allocated buffer.
     */
    inline void free_heap() noexcept {
        if (is_heap_allocated() && m_sqlite.payload.pData) {
            sqlite3_free(m_sqlite.payload.pData);
        }
    }

    /**
     * @brief Packs type, heap flag, and inline length into the single shared tag byte.
     * @param type The SQLite storage class (SQLITE_INTEGER..SQLITE_NULL).
     * @param is_heap True if payload is a heap-allocated buffer.
     * @param len Inline payload length (0..14 bytes).
     */
    inline void set_tag(uint8_t type, bool is_heap, uint8_t len = 0) noexcept {
        m_sqlite.tag.set(type, is_heap, len);
    }

    /** @brief Initializes state as SQLITE_NULL. */
    inline void init_null(bool is_imm = false) noexcept {
        m_sqlite.payload.pData = nullptr;
        m_sqlite.heap_len = 0;
        m_sqlite.affinity = SQLITE_AFF_NONE;
        m_sqlite.reserved = 0;
        m_sqlite.subtag.set(SQLITE_SUBTYPE_NONE, is_imm);
        set_tag(SQLITE_NULL, false, 0);
    }

    /** @brief Initializes 64-bit integer from sqlite3_value. */
    inline void init_integer(const sqlite3_value* val, uint8_t sub, bool is_imm = false) noexcept {
        init_integer(sqlite3_value_int64(const_cast<sqlite3_value*>(val)), sub, SQLITE_AFF_INTEGER, is_imm);
    }

    /** @brief Initializes 64-bit integer directly. */
    inline void init_integer(sqlite3_int64 i, uint8_t sub = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_INTEGER, bool is_imm = false) noexcept {
        m_sqlite.payload.iValue = i;
        m_sqlite.heap_len = 0;
        m_sqlite.affinity = aff;
        m_sqlite.reserved = 0;
        m_sqlite.subtag.set(sub, is_imm);
        set_tag(SQLITE_INTEGER, false, 0);
    }

    /** @brief Initializes double-precision float from sqlite3_value. */
    inline void init_float(const sqlite3_value* val, uint8_t sub, bool is_imm = false) noexcept {
        init_float(sqlite3_value_double(const_cast<sqlite3_value*>(val)), sub, SQLITE_AFF_REAL, is_imm);
    }

    /** @brief Initializes double-precision float directly. */
    inline void init_float(double d, uint8_t sub = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_REAL, bool is_imm = false) noexcept {
        m_sqlite.payload.dValue = d;
        m_sqlite.heap_len = 0;
        m_sqlite.affinity = aff;
        m_sqlite.reserved = 0;
        m_sqlite.subtag.set(sub, is_imm);
        set_tag(SQLITE_FLOAT, false, 0);
    }

    /**
     * @brief Unified buffer initializer for Strings, Blobs, and Custom buffer payloads.
     * 
     * Handles both SBO (<= 14 bytes) and exact heap allocations without extra '\0' overhead.
     *
     * @param data Pointer to raw byte payload.
     * @param len Length in bytes (or -1 for auto-calculating null-terminated text strings).
     * @param type SQLite storage class (SQLITE_TEXT or SQLITE_BLOB).
     * @param aff SQLite native affinity character ('B' for TEXT, 'A' for BLOB).
     * @param sub SQLite subtype (SQLITE_SUBTYPE_NONE, SQLITE_SUBTYPE_JSON, etc.).
     * @param is_imm Optional flag marking the value immutable.
     */
    inline void init_buffer(const void* data, int len, uint8_t type, char aff, uint8_t sub = SQLITE_SUBTYPE_NONE, bool is_imm = false) {
        if (!data) {
            init_null(is_imm);
            return;
        }

        int n = (len >= 0) ? len : ((type == SQLITE_TEXT) ? SqliteStringUtil::sqlite_strlen(static_cast<const char*>(data)) : 0);
        if (n < 0) {
            init_null(is_imm);
            return;
        }

        if (n <= MAX_INLINE_BUF_LEN) {
            if (n > 0) {
                memcpy(m_inline.buf, data, n);
            }
            m_inline.subtag.set(sub, is_imm);
            set_tag(type, false, static_cast<uint8_t>(n));
        } else {
            char* buf = static_cast<char*>(sqlite3_malloc64(static_cast<sqlite3_uint64>(n)));
            if (buf) {
                memcpy(buf, data, n);
            }
            m_sqlite.payload.pData = buf;
            m_sqlite.heap_len = n;
            m_sqlite.affinity = aff;
            m_sqlite.reserved = 0;
            m_sqlite.subtag.set(sub, is_imm);
            set_tag(type, true, 0);
        }
    }

    /** @brief Initializes text string with SBO (<= 14 chars) or exact-sized heap duplication from sqlite3_value. */
    inline void init_text(const sqlite3_value* val, uint8_t sub, bool is_imm = false) {
        const char* text = reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(val)));
        int len = text ? sqlite3_value_bytes(const_cast<sqlite3_value*>(val)) : 0;
        init_buffer(text, len, SQLITE_TEXT, SQLITE_AFF_TEXT, sub, is_imm);
    }

    /** @brief Initializes text string with SBO (<= 14 chars) or exact-sized heap duplication directly. */
    inline void init_text(const char* text, int len = -1, uint8_t sub = SQLITE_SUBTYPE_NONE, bool is_imm = false) {
        init_buffer(text, len, SQLITE_TEXT, SQLITE_AFF_TEXT, sub, is_imm);
    }

    /** @brief Initializes binary blob with SBO (<= 14 bytes) or exact-sized heap duplication from sqlite3_value. */
    inline void init_blob(const sqlite3_value* val, uint8_t sub, bool is_imm = false) {
        const void* blob = sqlite3_value_blob(const_cast<sqlite3_value*>(val));
        int len = blob ? sqlite3_value_bytes(const_cast<sqlite3_value*>(val)) : 0;
        init_buffer(blob, len, SQLITE_BLOB, SQLITE_AFF_BLOB, sub, is_imm);
    }

    /** @brief Initializes binary blob with SBO (<= 14 bytes) or exact-sized heap duplication directly. */
    inline void init_blob(const void* data, int len, uint8_t sub = SQLITE_SUBTYPE_NONE, bool is_imm = false) {
        init_buffer(data, len, SQLITE_BLOB, SQLITE_AFF_BLOB, sub, is_imm);
    }

    /** 
     * @brief Internal helper to deep-copy another SqliteValueOwned instance.
     * Performs a 16-byte burst copy followed by conditional heap payload duplication.
     */
    inline void copy_from(const SqliteValueOwned& other) {
        memcpy(static_cast<void*>(this), &other, sizeof(SqliteValueOwned));
        if (other.is_heap_allocated()) {
            if (other.m_sqlite.payload.pData && other.m_sqlite.heap_len > 0) {
                size_t alloc_sz = static_cast<size_t>(other.m_sqlite.heap_len);
                char* buf = static_cast<char*>(sqlite3_malloc64(static_cast<sqlite3_uint64>(alloc_sz)));
                if (buf) {
                    memcpy(buf, other.m_sqlite.payload.pData, alloc_sz);
                }
                m_sqlite.payload.pData = buf;
            } else {
                m_sqlite.payload.pData = nullptr;
            }
        }
    }

    /** 
     * @brief Internal helper to move another SqliteValueOwned instance.
     * Takes ownership of the 16-byte state and resets the source to SQLITE_NULL.
     * If the source instance is marked immutable, deep-copies instead to prevent mutating it.
     */
    inline void move_from(SqliteValueOwned&& other) noexcept {
        if (other.is_immutable()) {
            copy_from(other);
            return;
        }
        memcpy(static_cast<void*>(this), &other, sizeof(SqliteValueOwned));
        other.init_null();
    }

public:
    /**
     * @brief Default constructor creating a NULL value with no subtype.
     */
    SqliteValueOwned() noexcept {
        init_null(false);
    }

    /**
     * @brief Explicit nullptr constructor creating a NULL value with optional immutability.
     */
    explicit SqliteValueOwned(std::nullptr_t, bool is_immutable = false) noexcept {
        init_null(is_immutable);
    }

    /** 
     * @brief Sets this object as the return result of a SQLite UDF context. 
     */
    inline void result(sqlite3_context* ctx) const {
        switch (type()) {
            case SQLITE_TEXT: {
                SqliteStringView sv = as_text();
                sqlite3_result_text(ctx, sv.data(), sv.length(), SQLITE_TRANSIENT);
                break;
            }
            case SQLITE_BLOB: {
                SqliteBlobView bv = as_blob();
                sqlite3_result_blob(ctx, bv.data(), bv.size(), SQLITE_TRANSIENT);
                break;
            }
            case SQLITE_INTEGER:
                sqlite3_result_int64(ctx, m_sqlite.payload.iValue);
                break;
            case SQLITE_FLOAT:
                sqlite3_result_double(ctx, m_sqlite.payload.dValue);
                break;
            case SQLITE_NULL:
            default:
                sqlite3_result_null(ctx);
                break;
        }
        if (subtype() != SQLITE_SUBTYPE_NONE) {
            sqlite3_result_subtype(ctx, subtype());
        }
    }
    template <typename TContext>
    inline void result(TContext& ctx) const { result(ctx.get()); }

    /** 
     * @brief Binds this object to a prepared SQLite statement at the given 1-based column index. 
     */
    inline int bind(sqlite3_stmt* stmt, int col) const {
        switch (type()) {
            case SQLITE_TEXT: {
                SqliteStringView sv = as_text();
                return sqlite3_bind_text(stmt, col, sv.data(), sv.length(), SQLITE_TRANSIENT);
            }
            case SQLITE_BLOB: {
                SqliteBlobView bv = as_blob();
                return sqlite3_bind_blob(stmt, col, bv.data(), bv.size(), SQLITE_TRANSIENT);
            }
            case SQLITE_INTEGER:
                return sqlite3_bind_int64(stmt, col, m_sqlite.payload.iValue);
            case SQLITE_FLOAT:
                return sqlite3_bind_double(stmt, col, m_sqlite.payload.dValue);
            case SQLITE_NULL:
            default:
                return sqlite3_bind_null(stmt, col);
        }
    }
    template <typename TStatement>
    inline int bind(TStatement& stmt, int col) const { return bind(stmt.get(), col); }

    /**
     * @brief Constructs an owned 16-byte value by copying/inlining from an existing `sqlite3_value`.
     */
    explicit SqliteValueOwned(const sqlite3_value* val, bool is_immutable = false) {
        if (!val) {
            init_null(is_immutable);
            return;
        }

        uint8_t sub = static_cast<uint8_t>(sqlite3_value_subtype(const_cast<sqlite3_value*>(val)));
        int t = sqlite3_value_type(const_cast<sqlite3_value*>(val));

        switch (t) {
            case SQLITE_INTEGER:
                init_integer(val, sub, is_immutable);
                break;
            case SQLITE_FLOAT:
                init_float(val, sub, is_immutable);
                break;
            case SQLITE_TEXT:
                init_text(val, sub, is_immutable);
                break;
            case SQLITE_BLOB:
                init_blob(val, sub, is_immutable);
                break;
            case SQLITE_NULL:
            default:
                init_null(is_immutable);
                m_sqlite.subtag.set(sub, is_immutable);
                break;
        }
    }
    
    /** @brief Zero-allocation constructor for storing a 64-bit integer inline with optional subtype and affinity. */
    explicit SqliteValueOwned(sqlite3_int64 val, uint8_t subtype = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_INTEGER, bool is_immutable = false) noexcept {
        init_integer(val, subtype, aff, is_immutable);
    }

    /** @brief Zero-allocation constructor for storing a standard long integer. */
    explicit SqliteValueOwned(long val, uint8_t subtype = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_INTEGER, bool is_immutable = false) noexcept {
        init_integer(static_cast<sqlite3_int64>(val), subtype, aff, is_immutable);
    }
    
    /** @brief Zero-allocation constructor for storing a standard int. */
    explicit SqliteValueOwned(int val, uint8_t subtype = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_INTEGER, bool is_immutable = false) noexcept {
        init_integer(static_cast<sqlite3_int64>(val), subtype, aff, is_immutable);
    }

    /** @brief Zero-allocation constructor for storing an unsigned int. */
    explicit SqliteValueOwned(unsigned int val, uint8_t subtype = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_INTEGER, bool is_immutable = false) noexcept {
        init_integer(static_cast<sqlite3_int64>(val), subtype, aff, is_immutable);
    }

    /** @brief Zero-allocation constructor for storing an unsigned long. */
    explicit SqliteValueOwned(unsigned long val, uint8_t subtype = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_INTEGER, bool is_immutable = false) noexcept {
        init_integer(static_cast<sqlite3_int64>(val), subtype, aff, is_immutable);
    }

    /** @brief Zero-allocation constructor for storing an unsigned long long. */
    explicit SqliteValueOwned(unsigned long long val, uint8_t subtype = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_INTEGER, bool is_immutable = false) noexcept {
        init_integer(static_cast<sqlite3_int64>(val), subtype, aff, is_immutable);
    }
    
    /** @brief Zero-allocation constructor for storing a double-precision float inline with optional subtype and affinity. */
    explicit SqliteValueOwned(double val, uint8_t subtype = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_REAL, bool is_immutable = false) noexcept {
        init_float(val, subtype, aff, is_immutable);
    }

    /** @brief Zero-allocation constructor for explicit boolean values. */
    explicit SqliteValueOwned(bool val, uint8_t subtype = SQLITE_SUBTYPE_BOOL, char aff = SQLITE_AFF_INTEGER, bool is_immutable = false) noexcept {
        init_integer(val ? 1LL : 0LL, subtype, aff, is_immutable);
    }

    /** @brief Constructs an owned string value from a null-terminated C-string. */
    explicit SqliteValueOwned(const char* text, uint8_t subtype = SQLITE_SUBTYPE_NONE, bool is_immutable = false) {
        init_text(text, -1, subtype, is_immutable);
    }

    /** @brief Constructs an owned string value from a SqliteStringView. */
    explicit SqliteValueOwned(const SqliteStringView& text, uint8_t subtype = SQLITE_SUBTYPE_NONE, bool is_immutable = false) {
        init_text(text.data(), text.length(), subtype, is_immutable);
    }

    /** @brief Constructs an owned blob value from a SqliteBlobView. */
    explicit SqliteValueOwned(const SqliteBlobView& blob, uint8_t subtype = SQLITE_SUBTYPE_NONE, bool is_immutable = false) {
        init_blob(blob.data(), blob.size(), subtype, is_immutable);
    }

    // ========================================================================
    // STATIC FACTORY CONSTRUCTORS FOR SUBTYPES & INLINE PAYLOADS
    // ========================================================================

    /** @brief Constructs a boolean SqliteValueOwned tagged with SQLITE_SUBTYPE_BOOL. */
    static SqliteValueOwned from_bool(bool val, bool is_immutable = false) noexcept {
        return SqliteValueOwned(val, SQLITE_SUBTYPE_BOOL, SQLITE_AFF_INTEGER, is_immutable);
    }

    /** @brief Constructs a datetime integer timestamp tagged with SQLITE_SUBTYPE_DATETIME. */
    static SqliteValueOwned from_datetime(sqlite3_int64 epoch_ms, bool is_immutable = false) noexcept {
        return SqliteValueOwned(epoch_ms, SQLITE_SUBTYPE_DATETIME, SQLITE_AFF_INTEGER, is_immutable);
    }

    /** @brief Constructs an inline or heap-backed string value. */
    static SqliteValueOwned from_text(const char* text, int len = -1, uint8_t subtype = SQLITE_SUBTYPE_NONE, bool is_immutable = false) {
        SqliteValueOwned val;
        val.init_text(text, len, subtype, is_immutable);
        return val;
    }

    /** @brief Constructs an inline or heap-backed blob value. */
    static SqliteValueOwned from_blob(const void* data, int len, uint8_t subtype = SQLITE_SUBTYPE_NONE, bool is_immutable = false) {
        SqliteValueOwned val;
        val.init_blob(data, len, subtype, is_immutable);
        return val;
    }

    /** @brief Constructs a JSON text value with SQLITE_SUBTYPE_JSON ('J'). */
    static SqliteValueOwned from_json(const char* json_str, int len = -1, bool is_immutable = false) {
        return from_text(json_str, len, SQLITE_SUBTYPE_JSON, is_immutable);
    }

    /** @brief Constructs a JSONB binary value with SQLITE_SUBTYPE_JSON ('J'). */
    static SqliteValueOwned from_jsonb(const void* blob, int len, bool is_immutable = false) {
        return from_blob(blob, len, SQLITE_SUBTYPE_JSON, is_immutable);
    }

    /** @brief Constructs an exact Decimal text value with SQLITE_SUBTYPE_DECIMAL ('D'). */
    static SqliteValueOwned from_decimal(const char* decimal_str, int len = -1, bool is_immutable = false) {
        return from_text(decimal_str, len, SQLITE_SUBTYPE_DECIMAL, is_immutable);
    }

    /** @brief Constructs a UUID text or blob value with SQLITE_SUBTYPE_UUID ('U'). */
    static SqliteValueOwned from_uuid(const char* uuid_str, int len = -1, bool is_immutable = false) {
        return from_text(uuid_str, len, SQLITE_SUBTYPE_UUID, is_immutable);
    }

    /** @brief Constructs a UUID binary value with SQLITE_SUBTYPE_UUID ('U'). */
    static SqliteValueOwned from_uuid(const void* uuid_16_bytes, bool is_immutable = false) {
        return from_blob(uuid_16_bytes, 16, SQLITE_SUBTYPE_UUID, is_immutable);
    }

    /** @brief Constructs a binary AI Vector embedding with SQLITE_SUBTYPE_VECTOR ('V'). */
    static SqliteValueOwned from_vector(const void* float_data, int byte_len, bool is_immutable = false) {
        return from_blob(float_data, byte_len, SQLITE_SUBTYPE_VECTOR, is_immutable);
    }

    /** @brief Constructs a binary Geometry blob with SQLITE_SUBTYPE_GEOMETRY ('G'). */
    static SqliteValueOwned from_geometry(const void* geom_data, int byte_len, bool is_immutable = false) {
        return from_blob(geom_data, byte_len, SQLITE_SUBTYPE_GEOMETRY, is_immutable);
    }

    /** @brief Constructs a Compressed binary blob with SQLITE_SUBTYPE_COMPRESSED ('Z'). */
    static SqliteValueOwned from_compressed(const void* compressed_data, int byte_len, bool is_immutable = false) {
        return from_blob(compressed_data, byte_len, SQLITE_SUBTYPE_COMPRESSED, is_immutable);
    }

    /**
     * @brief Parses an unquoted or quoted string literal with automatic SQL type inference.
     *
     * Inferred types and conversions:
     *   - "null" / ""                         → SQLITE_NULL
     *   - "true", "false", "yes", "no", "on"  → SQLITE_INTEGER (tagged with SQLITE_SUBTYPE_BOOL)
     *   - Quoted strings ('text' or "text")    → SQLITE_TEXT (quotes stripped)
     *   - Integer numbers ("1024", "-42")     → SQLITE_INTEGER (as 64-bit int)
     *   - Floating-point ("3.1415", "1e-4")   → SQLITE_FLOAT (as double)
     *   - Other unquoted text ("strict", "k") → SQLITE_TEXT
     *
     * @param str The SqliteStringView representing the literal string token.
     * @return SqliteValueOwned initialized with the inferred SQL type.
     */
    static inline SqliteValueOwned from_literal(SqliteStringView str, bool is_immutable = false) {
        if (str.empty()) return SqliteValueOwned(nullptr, is_immutable);

        const char* p = str.data();
        int len = str.length();

        // 1. Check for NULL
        if (len == 4 && sqlite3_strnicmp(p, "null", 4) == 0) {
            return SqliteValueOwned(nullptr, is_immutable);
        }

        // 2. Check for Booleans
        if ((len == 4 && sqlite3_strnicmp(p, "true", 4) == 0) ||
            (len == 3 && sqlite3_strnicmp(p, "yes", 3) == 0)  ||
            (len == 2 && sqlite3_strnicmp(p, "on", 2) == 0)) {
            return SqliteValueOwned::from_bool(true, is_immutable);
        }
        if ((len == 5 && sqlite3_strnicmp(p, "false", 5) == 0) ||
            (len == 2 && sqlite3_strnicmp(p, "no", 2) == 0)    ||
            (len == 3 && sqlite3_strnicmp(p, "off", 3) == 0)) {
            return SqliteValueOwned::from_bool(false, is_immutable);
        }

        // 3. Check for Quoted String Literal ('text' or "text")
        if (len >= 2 && ((p[0] == '\'' && p[len - 1] == '\'') || (p[0] == '"' && p[len - 1] == '"'))) {
            return SqliteValueOwned::from_text(p + 1, len - 2, SQLITE_SUBTYPE_NONE, is_immutable);
        }

        // 4. Try parsing as Integer or Float
        bool has_dot = false;
        bool has_e = false;
        bool is_num = (len > 0 && (p[0] == '-' || p[0] == '+' || (p[0] >= '0' && p[0] <= '9')));
        if (is_num) {
            for (int i = 1; i < len; ++i) {
                if (p[i] == '.') {
                    has_dot = true;
                } else if (p[i] == 'e' || p[i] == 'E') {
                    has_e = true;
                } else if ((p[i] == '-' || p[i] == '+') && (p[i - 1] == 'e' || p[i - 1] == 'E')) {
                    // Valid exponent sign
                } else if (p[i] < '0' || p[i] > '9') {
                    is_num = false;
                    break;
                }
            }
        }

        if (is_num && !has_dot && !has_e) {
            long long i_val = 0;
            if (sscanf(p, "%lld", &i_val) == 1) {
                return SqliteValueOwned(i_val, SQLITE_SUBTYPE_NONE, SQLITE_AFF_INTEGER, is_immutable);
            }
        }

        if (is_num && (has_dot || has_e)) {
            double d_val = 0.0;
            if (sscanf(p, "%lf", &d_val) == 1) {
                return SqliteValueOwned(d_val, SQLITE_SUBTYPE_NONE, SQLITE_AFF_REAL, is_immutable);
            }
        }

        // 5. Default to Text
        return SqliteValueOwned::from_text(p, len, SQLITE_SUBTYPE_NONE, is_immutable);
    }

    /**
     * @brief Parses a null-terminated or sized C-string literal with automatic SQL type inference.
     * @param str Pointer to UTF-8 C-string.
     * @param len Number of bytes (or -1 to auto-calculate length).
     * @return SqliteValueOwned initialized with the inferred SQL type.
     */
    static inline SqliteValueOwned from_literal(const char* str, int len = -1) {
        if (!str) return SqliteValueOwned(nullptr, false);
        int l = (len < 0) ? SqliteStringUtil::sqlite_strlen(str) : len;
        return from_literal(SqliteStringView(str, l), false);
    }

    /**
     * @brief Parses a sized C-string literal with automatic SQL type inference and immutability.
     * @param str Pointer to UTF-8 C-string.
     * @param len Number of bytes (or -1 to auto-calculate length).
     * @param is_immutable Flag to mark the parsed value immutable.
     * @return SqliteValueOwned initialized with the inferred SQL type.
     */
    static inline SqliteValueOwned from_literal(const char* str, int len, bool is_immutable) {
        if (!str) return SqliteValueOwned(nullptr, is_immutable);
        int l = (len < 0) ? SqliteStringUtil::sqlite_strlen(str) : len;
        return from_literal(SqliteStringView(str, l), is_immutable);
    }

    /** 
     * @brief Destructor. Automatically frees owned heap memory if active.
     */
    ~SqliteValueOwned() {
        free_heap();
    }

    /**
     * @brief Returns a canonical static SQLITE_NULL instance (16 bytes, tag = 0xA0).
     * 
     * ### 16-Byte Canonical SQL NULL Layout:
     * - Offset 0..7:   `pData = nullptr` (64-bit zeroed pointer)
     * - Offset 8..11:  `heap_len = 0` (32-bit zeroed length)
     * - Offset 12:     `affinity = SQLITE_AFF_NONE`
     * - Offset 13:     `reserved = 0`
     * - Offset 14:     `subtype = SQLITE_SUBTYPE_NONE` (0x00)
     * - Offset 15:     `tag = 0xA0` (type = SQLITE_NULL = 5, heap = false, len = 0)
     * 
     * Because `tag.raw == 0xA0 >= 0x20`, this instance is recognized as an active SQLite value
     * (`is_active() == true` and `is_null() == true`), distinguishing it from empty/uninitialized
     * memory slots (`tag.raw == 0x00 < 0x20`).
     * 
     * @return Const reference to process-wide static SQLITE_NULL instance.
     */
    static inline const SqliteValueOwned& static_null() noexcept {
        static const SqliteValueOwned kNullInstance;
        return kNullInstance;
    }

    /**
     * @brief Returns a pointer to a canonical static array of 8 SQLITE_NULL instances (128 bytes).
     * 
     * ### SIMD Vectorized Initialization (`null_array`):
     * Container initializers and constructors use `static_null_array()` as a pre-populated template
     * source for single-burst `memcpy` operations.
     * 
     * @return Const pointer to contiguous array of 8 canonical SQLITE_NULL values.
     */
    static inline const SqliteValueOwned* static_null_array() noexcept {
        static const SqliteValueOwned kNullArray[8] = {
            SqliteValueOwned(), SqliteValueOwned(), SqliteValueOwned(), SqliteValueOwned(),
            SqliteValueOwned(), SqliteValueOwned(), SqliteValueOwned(), SqliteValueOwned()
        };
        return kNullArray;
    }

    /**
     * @brief Resets this value to SQLITE_NULL, releasing heap memory if owned.
     */
    inline void set_null() noexcept {
        if (is_immutable()) return;
        free_heap();
        init_null(false);
    }

    /** @brief Clears the current value, releasing heap memory, and resets to SQLITE_NULL. */
    inline void clear() noexcept { set_null(); }
    
    /** @brief Resets the current value, releasing heap memory, and resets to SQLITE_NULL. */
    inline void reset() noexcept { set_null(); }

    // ========================================================================
    // COPY & MOVE CONSTRUCTORS / OPERATORS
    // ========================================================================

    // Copy Constructor
    SqliteValueOwned(const SqliteValueOwned& other) {
        copy_from(other);
    }

    // Copy Constructor with explicit immutability override
    SqliteValueOwned(const SqliteValueOwned& other, bool is_immutable) {
        copy_from(other);
        m_sqlite.subtag.set_immutable(is_immutable);
    }

    // Copy Assignment Operator
    SqliteValueOwned& operator=(const SqliteValueOwned& other) {
        if (is_immutable()) return *this;
        if (this != &other) {
            free_heap();
            copy_from(other);
        }
        return *this;
    }

    // Move Constructor
    SqliteValueOwned(SqliteValueOwned&& other) noexcept {
        move_from(sqlite_move(other));
    }

    // Move Assignment Operator
    SqliteValueOwned& operator=(SqliteValueOwned&& other) noexcept {
        if (is_immutable()) return *this;
        if (this != &other) {
            free_heap();
            move_from(sqlite_move(other));
        }
        return *this;
    }

    /** @brief Sets value to an integer, freeing any prior heap allocation. */
    inline void set_integer(sqlite3_int64 i, uint8_t sub = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_INTEGER) noexcept {
        if (is_immutable()) return;
        free_heap();
        init_integer(i, sub, aff);
    }

    /** @brief Sets value to a double, freeing any prior heap allocation. */
    inline void set_float(double d, uint8_t sub = SQLITE_SUBTYPE_NONE, char aff = SQLITE_AFF_REAL) noexcept {
        if (is_immutable()) return;
        free_heap();
        init_float(d, sub, aff);
    }

    /** @brief Sets value to a string, freeing any prior heap allocation. */
    inline void set_text(const char* text, int len = -1, uint8_t sub = SQLITE_SUBTYPE_NONE) {
        if (is_immutable()) return;
        free_heap();
        init_text(text, len, sub);
    }

    /** @brief Sets value to a binary blob, freeing any prior heap allocation. */
    inline void set_blob(const void* data, int len, uint8_t sub = SQLITE_SUBTYPE_NONE) {
        if (is_immutable()) return;
        free_heap();
        init_blob(data, len, sub);
    }

    /** @brief Replaces current value with a 64-bit integer. */
    SqliteValueOwned& operator=(sqlite3_int64 i) noexcept {
        if (is_immutable()) return *this;
        free_heap();
        init_integer(i);
        return *this;
    }

    /** @brief Replaces current value with a signed int. */
    SqliteValueOwned& operator=(int i) noexcept {
        return *this = static_cast<sqlite3_int64>(i);
    }

    /** @brief Replaces current value with a signed long. */
    SqliteValueOwned& operator=(long l) noexcept {
        return *this = static_cast<sqlite3_int64>(l);
    }

    /** @brief Replaces current value with an unsigned int. */
    SqliteValueOwned& operator=(unsigned int u) noexcept {
        return *this = static_cast<sqlite3_int64>(u);
    }

    /** @brief Replaces current value with an unsigned long. */
    SqliteValueOwned& operator=(unsigned long u) noexcept {
        return *this = static_cast<sqlite3_int64>(u);
    }

    /** @brief Replaces current value with an unsigned long long. */
    SqliteValueOwned& operator=(unsigned long long u) noexcept {
        return *this = static_cast<sqlite3_int64>(u);
    }

    /** @brief Replaces current value with a boolean (INTEGER 0 or 1). */
    SqliteValueOwned& operator=(bool b) noexcept {
        return *this = static_cast<sqlite3_int64>(b ? 1 : 0);
    }

    /** @brief Replaces current value with a double-precision float. */
    SqliteValueOwned& operator=(double d) noexcept {
        if (is_immutable()) return *this;
        free_heap();
        init_float(d);
        return *this;
    }

    /** @brief Replaces current value with a single-precision float. */
    SqliteValueOwned& operator=(float f) noexcept {
        return *this = static_cast<double>(f);
    }

    /** @brief Replaces current value with a text view / string. */
    SqliteValueOwned& operator=(const SqliteStringView& str) {
        if (is_immutable()) return *this;
        free_heap();
        init_text(str.data(), str.length());
        return *this;
    }

    /** @brief Replaces current value with a null-terminated C-string. */
    SqliteValueOwned& operator=(const char* str) {
        if (is_immutable()) return *this;
        if (!str) {
            set_null();
            return *this;
        }
        free_heap();
        init_text(str, SqliteStringUtil::sqlite_strlen(str));
        return *this;
    }

    /** @brief Replaces current value with a binary blob view. */
    SqliteValueOwned& operator=(const SqliteBlobView& blob) {
        if (is_immutable()) return *this;
        free_heap();
        init_blob(blob.data(), blob.size());
        return *this;
    }

    /** @brief Creates an owned duplicate/clone of this value. */
    inline SqliteValueOwned clone() const {
        return SqliteValueOwned(*this);
    }
    
    /** @brief Returns the SQLite datatype (e.g. SQLITE_INTEGER). */
    inline int type() const noexcept {
        return m_sqlite.tag.type();
    }

    /** @brief Checks if the value is allocated on the heap. */
    inline bool is_heap_allocated() const noexcept {
        return m_sqlite.tag.is_heap();
    }

    /** @brief Returns the length of inline text or blob payload (0..14). */
    inline uint8_t inline_length() const noexcept {
        return m_sqlite.tag.len();
    }

    /** @brief Returns the underlying 1-byte control tag. */
    inline SqliteOwnedValueTag tag() const noexcept {
        return m_sqlite.tag;
    }

    /** @brief Returns the underlying 1-byte subtag. */
    inline SqliteOwnedValueSubTag subtag() const noexcept {
        return m_sqlite.subtag;
    }

    /** @brief Checks if this value is an active, initialized SQLite value. */
    inline bool is_active() const noexcept {
        return m_sqlite.tag.is_active();
    }

    /** @brief Returns the 7-bit SQLite subtype (zero-branch shared offset 14). */
    inline uint8_t subtype() const noexcept {
        return m_sqlite.subtag.subtype();
    }

    /** @brief Sets the 7-bit SQLite subtype (zero-branch shared offset 14). */
    inline void set_subtype(uint8_t sub) noexcept {
        if (is_immutable()) return;
        m_sqlite.subtag.set_subtype(sub);
    }

    /** @brief Checks if this value is marked immutable. */
    inline bool is_immutable() const noexcept {
        return m_sqlite.subtag.is_immutable();
    }

    /** @brief Marks this value as immutable. */
    inline void mark_immutable() noexcept {
        m_sqlite.subtag.mark_immutable();
    }

    /** @brief Unmarks this value as immutable (restores mutability). */
    inline void unmark_immutable() noexcept {
        m_sqlite.subtag.unmark_immutable();
    }

    /** @brief Sets or clears the immutability flag. */
    inline void set_immutable(bool imm = true) noexcept {
        m_sqlite.subtag.set_immutable(imm);
    }

    /** @brief In-place converts this value to immutable and returns reference. */
    inline SqliteValueOwned& make_immutable() noexcept {
        mark_immutable();
        return *this;
    }

    /** @brief Returns an immutable clone of this value. */
    inline SqliteValueOwned as_immutable() const {
        return SqliteValueOwned(*this, true);
    }

    /** @brief Returns a mutable clone of this value. */
    inline SqliteValueOwned clone_mutable() const {
        return SqliteValueOwned(*this, false);
    }

    /** @brief Returns the SQLite native affinity character. */
    inline char affinity() const noexcept {
        if (is_heap_allocated() || type() == SQLITE_INTEGER || type() == SQLITE_FLOAT) {
            return m_sqlite.affinity;
        }
        switch (type()) {
            case SQLITE_TEXT: return SQLITE_AFF_TEXT;
            case SQLITE_BLOB: return SQLITE_AFF_BLOB;
            default:          return SQLITE_AFF_NONE;
        }
    }

    /** @brief Sets the SQLite native affinity character. */
    inline void set_affinity(char aff) noexcept {
        if (is_immutable()) return;
        m_sqlite.affinity = aff;
    }

    /** @brief Storage class type predicates. */
    inline bool is_null()    const noexcept { return type() == SQLITE_NULL; }
    inline bool is_integer() const noexcept { return type() == SQLITE_INTEGER; }
    inline bool is_float()   const noexcept { return type() == SQLITE_FLOAT; }
    inline bool is_text()    const noexcept { return type() == SQLITE_TEXT; }
    inline bool is_blob()    const noexcept { return type() == SQLITE_BLOB; }
    inline bool is_numeric() const noexcept { return sqlite3IsNumericAffinity(affinity()); }
    inline bool as_bool()    const noexcept { return as_int64() != 0; }

    /** @brief Subtype query predicates. */
    inline bool is_json()       const noexcept { return subtype() == SQLITE_SUBTYPE_JSON; }
    inline bool is_decimal()    const noexcept { return subtype() == SQLITE_SUBTYPE_DECIMAL; }
    inline bool is_uuid()       const noexcept { return subtype() == SQLITE_SUBTYPE_UUID; }
    inline bool is_vector()     const noexcept { return subtype() == SQLITE_SUBTYPE_VECTOR; }
    inline bool is_geometry()   const noexcept { return subtype() == SQLITE_SUBTYPE_GEOMETRY; }
    inline bool is_datetime()   const noexcept { return subtype() == SQLITE_SUBTYPE_DATETIME; }
    inline bool is_bool()       const noexcept { return subtype() == SQLITE_SUBTYPE_BOOL; }
    inline bool is_compressed() const noexcept { return subtype() == SQLITE_SUBTYPE_COMPRESSED; }

    /** @brief Checks if the value holds a valid state (or SBO primitive/null). */
    bool is_valid() const noexcept {
        if (is_heap_allocated()) {
            return m_sqlite.payload.pData != nullptr;
        }
        return true;
    }

    /** @brief Explicit boolean conversion checking validity. */
    explicit operator bool() const noexcept {
        return is_valid() && !is_null();
    }

    /** @brief Internal helper to access heap-allocated pointers for heterogeneous lookups. */
    const void* heap_value() const noexcept {
        return is_heap_allocated() ? m_sqlite.payload.pData : nullptr;
    }

    /** @brief Internal helper to access SBO integer for heterogeneous lookups. */
    sqlite3_int64 as_int64() const noexcept { return m_sqlite.payload.iValue; }

    /** @brief Access SBO integer as 32-bit signed int. */
    inline int as_int() const noexcept { return static_cast<int>(as_int64()); }

    /** @brief Internal helper to access SBO double for heterogeneous lookups. */
    double as_double() const noexcept { return m_sqlite.payload.dValue; }
    
    /** @brief Access string data as a zero-allocation SqliteStringView. */
    SqliteStringView as_text() const {
        if (type() != SQLITE_TEXT) return SqliteStringView(nullptr, 0);
        if (!is_heap_allocated()) {
            return SqliteStringView(m_inline.buf, inline_length());
        }
        return SqliteStringView(m_sqlite.payload.pData, m_sqlite.heap_len);
    }

    /** @brief Access binary data as a zero-allocation SqliteBlobView. */
    SqliteBlobView as_blob() const {
        if (type() != SQLITE_BLOB) return SqliteBlobView(nullptr, 0);
        if (!is_heap_allocated()) {
            return SqliteBlobView(m_inline.buf, inline_length());
        }
        return SqliteBlobView(m_sqlite.payload.pData, m_sqlite.heap_len);
    }

    /** 
     * @brief Computes a polymorphic 64-bit MurmurHash2 of the value.
     */
    unsigned long long hash() const {
        switch (type()) {
            case SQLITE_INTEGER:
                return SqliteHashUtil::mix(SqliteHashUtil::DEFAULT_SEED, &m_sqlite.payload.iValue, sizeof(m_sqlite.payload.iValue));
            case SQLITE_FLOAT: {
                double d = m_sqlite.payload.dValue == 0.0 ? 0.0 : m_sqlite.payload.dValue;
                return SqliteHashUtil::mix(SqliteHashUtil::DEFAULT_SEED, &d, sizeof(d));
            }
            case SQLITE_TEXT: {
                SqliteStringView sv = as_text();
                return SqliteHashUtil::hash(sv.data(), sv.length());
            }
            case SQLITE_BLOB: {
                SqliteBlobView bv = as_blob();
                return SqliteHashUtil::hash(bv.data(), bv.size());
            }
            case SQLITE_NULL:
            default:
                return SqliteHashUtil::DEFAULT_SEED;
        }
    }
    
    /**
     * @brief Performs a strict polymorphic equality check against another owned value.
     */
    bool operator==(const SqliteValueOwned& other) const {
        int t1 = type();
        int t2 = other.type();
        if (t1 != t2) return false;
        switch (t1) {
            case SQLITE_INTEGER:
                return m_sqlite.payload.iValue == other.m_sqlite.payload.iValue;
            case SQLITE_FLOAT:
                if (m_sqlite.payload.dValue != m_sqlite.payload.dValue && other.m_sqlite.payload.dValue != other.m_sqlite.payload.dValue) return true;
                return m_sqlite.payload.dValue == other.m_sqlite.payload.dValue;
            case SQLITE_TEXT:
                return as_text() == other.as_text();
            case SQLITE_BLOB:
                return as_blob() == other.as_blob();
            case SQLITE_NULL:
            default:
                return true;
        }
    }

    /** @brief Performs a strict polymorphic inequality check against another owned value. */
    bool operator!=(const SqliteValueOwned& other) const {
        return !(*this == other);
    }

    /**
     * @brief Performs a polymorphic less-than comparison.
     */
    bool operator<(const SqliteValueOwned& other) const {
        auto type_rank = [](int t) -> int {
            switch (t) {
                case SQLITE_NULL:    return 0;
                case SQLITE_INTEGER: 
                case SQLITE_FLOAT:   return 1;
                case SQLITE_TEXT:    return 2;
                case SQLITE_BLOB:    return 3;
                default:             return 0;
            }
        };

        int t1 = type();
        int t2 = other.type();
        int r1 = type_rank(t1);
        int r2 = type_rank(t2);
        if (r1 != r2) return r1 < r2;

        if (r1 == 1) { // Numeric
            if (t1 == SQLITE_INTEGER && t2 == SQLITE_INTEGER) {
                return m_sqlite.payload.iValue < other.m_sqlite.payload.iValue;
            }
            double d1 = (t1 == SQLITE_INTEGER) ? (double)m_sqlite.payload.iValue : m_sqlite.payload.dValue;
            double d2 = (t2 == SQLITE_INTEGER) ? (double)other.m_sqlite.payload.iValue : other.m_sqlite.payload.dValue;
            
            bool isnan1 = (d1 != d1);
            bool isnan2 = (d2 != d2);
            if (isnan1 && !isnan2) return true;  
            if (!isnan1 && isnan2) return false; 
            
            if (d1 != d2) {
                return d1 < d2;
            }
            
            return t1 < t2;
        }

        if (t1 == SQLITE_TEXT) return as_text() < other.as_text();
        if (t1 == SQLITE_BLOB) return as_blob() < other.as_blob();
        return false;
    }

    // Heterogeneous lookups for SqliteValueView
    bool operator==(const SqliteValueView& other) const;
    bool operator!=(const SqliteValueView& other) const;
    bool operator<(const SqliteValueView& other) const;
};

static_assert(sizeof(SqliteValueOwned) == 16, "SqliteValueOwned must be exactly 16 bytes!");

inline SqliteValueOwned SqliteValueView::to_owned() const {
    return SqliteValueOwned(m_val);
}

// Complete heterogeneous lookups for SqliteValueOwned
inline bool SqliteValueOwned::operator==(const SqliteValueView& other) const {
    int o_type = other.type();
    int m_type = type();
    if (m_type != o_type) return false;
    if (m_type == SQLITE_INTEGER) return m_sqlite.payload.iValue == sqlite3_value_int64(const_cast<sqlite3_value*>(other.get()));
    if (m_type == SQLITE_FLOAT) {
        double d2 = sqlite3_value_double(const_cast<sqlite3_value*>(other.get()));
        if (m_sqlite.payload.dValue != m_sqlite.payload.dValue && d2 != d2) return true;
        return m_sqlite.payload.dValue == d2;
    }
    if (m_type == SQLITE_TEXT) return as_text() == other.as_text();
    if (m_type == SQLITE_BLOB) return as_blob() == other.as_blob();
    return true;
}

inline bool SqliteValueOwned::operator!=(const SqliteValueView& other) const {
    return !(*this == other);
}

inline bool SqliteValueOwned::operator<(const SqliteValueView& other) const {
    auto type_rank = [](int t) -> int {
        switch (t) {
            case SQLITE_NULL:    return 0;
            case SQLITE_INTEGER: 
            case SQLITE_FLOAT:   return 1;
            case SQLITE_TEXT:    return 2;
            case SQLITE_BLOB:    return 3;
            default:             return 0;
        }
    };

    int m_type = type();
    int o_type = other.type();
    int r1 = type_rank(m_type);
    int r2 = type_rank(o_type);
    if (r1 != r2) return r1 < r2;

    if (r1 == 1) {
        if (m_type == SQLITE_INTEGER && o_type == SQLITE_INTEGER) {
            return m_sqlite.payload.iValue < sqlite3_value_int64(const_cast<sqlite3_value*>(other.get()));
        }
        double d1 = (m_type == SQLITE_INTEGER) ? (double)m_sqlite.payload.iValue : m_sqlite.payload.dValue;
        double d2 = (o_type == SQLITE_INTEGER) ? (double)sqlite3_value_int64(const_cast<sqlite3_value*>(other.get())) : sqlite3_value_double(const_cast<sqlite3_value*>(other.get()));
        
        bool isnan1 = (d1 != d1);
        bool isnan2 = (d2 != d2);
        if (isnan1 && !isnan2) return true;  
        if (!isnan1 && isnan2) return false; 
        
        if (d1 != d2) return d1 < d2;
        return m_type < o_type;
    }

    if (m_type == SQLITE_TEXT) return as_text() < other.as_text();
    if (m_type == SQLITE_BLOB) return as_blob() < other.as_blob();
    return false;
}

// Complete heterogeneous lookups for SqliteValueView
inline bool SqliteValueView::operator==(const SqliteValueOwned& other) const {
    return other == *this;
}

inline bool SqliteValueView::operator!=(const SqliteValueOwned& other) const {
    return !(*this == other);
}

inline bool SqliteValueView::operator<(const SqliteValueOwned& other) const {
    auto type_rank = [](int t) -> int {
        switch (t) {
            case SQLITE_NULL:    return 0;
            case SQLITE_INTEGER: 
            case SQLITE_FLOAT:   return 1;
            case SQLITE_TEXT:    return 2;
            case SQLITE_BLOB:    return 3;
            default:             return 0;
        }
    };

    int t1 = type();
    int t2 = other.type();
    int r1 = type_rank(t1);
    int r2 = type_rank(t2);
    
    if (r1 != r2) return r1 < r2;

    if (r1 == 1) {
        if (t1 == SQLITE_INTEGER && t2 == SQLITE_INTEGER) {
            return sqlite3_value_int64(const_cast<sqlite3_value*>(get())) < other.as_int64();
        }
        double d1 = (t1 == SQLITE_INTEGER) ? (double)sqlite3_value_int64(const_cast<sqlite3_value*>(get())) : sqlite3_value_double(const_cast<sqlite3_value*>(get()));
        double d2 = (t2 == SQLITE_INTEGER) ? (double)other.as_int64() : other.as_double();
        
        bool isnan1 = (d1 != d1);
        bool isnan2 = (d2 != d2);
        if (isnan1 && !isnan2) return true;  
        if (!isnan1 && isnan2) return false; 
        
        if (d1 != d2) return d1 < d2;
        return t1 < t2;
    }

    if (t1 == SQLITE_TEXT) return as_text() < other.as_text();
    if (t1 == SQLITE_BLOB) return as_blob() < other.as_blob();
    return false;
}

// ============================================================================
// HETEROGENEOUS LOOKUPS: VALUES VS STRINGS/BLOBS
// ============================================================================

#define SQLITE_DEF_VAL_STR_OPS(VAL_TYPE, STR_TYPE, STR_DATA, STR_LEN) \
    inline bool operator==(const VAL_TYPE& val, const STR_TYPE& str) { \
        if (val.type() != SQLITE_TEXT) return false; \
        SqliteStringView sv = val.as_text(); \
        return SqliteStringUtil::equal(sv.data(), sv.length(), str.STR_DATA(), str.STR_LEN()); \
    } \
    inline bool operator==(const STR_TYPE& str, const VAL_TYPE& val) { return val == str; } \
    inline bool operator!=(const VAL_TYPE& val, const STR_TYPE& str) { return !(val == str); } \
    inline bool operator!=(const STR_TYPE& str, const VAL_TYPE& val) { return !(val == str); } \
    inline bool operator<(const VAL_TYPE& val, const STR_TYPE& str) { \
        int r1 = (val.type() == SQLITE_NULL) ? 0 : (val.type() == SQLITE_INTEGER || val.type() == SQLITE_FLOAT) ? 1 : (val.type() == SQLITE_TEXT) ? 2 : 3; \
        if (r1 != 2) return r1 < 2; \
        SqliteStringView sv = val.as_text(); \
        return SqliteStringUtil::less(sv.data(), sv.length(), str.STR_DATA(), str.STR_LEN()); \
    } \
    inline bool operator<(const STR_TYPE& str, const VAL_TYPE& val) { \
        int r2 = (val.type() == SQLITE_NULL) ? 0 : (val.type() == SQLITE_INTEGER || val.type() == SQLITE_FLOAT) ? 1 : (val.type() == SQLITE_TEXT) ? 2 : 3; \
        if (2 != r2) return 2 < r2; \
        SqliteStringView sv = val.as_text(); \
        return SqliteStringUtil::less(str.STR_DATA(), str.STR_LEN(), sv.data(), sv.length()); \
    } \
    inline bool operator>(const VAL_TYPE& val, const STR_TYPE& str) { return str < val; } \
    inline bool operator>(const STR_TYPE& str, const VAL_TYPE& val) { return val < str; } \
    inline bool operator<=(const VAL_TYPE& val, const STR_TYPE& str) { return !(str < val); } \
    inline bool operator<=(const STR_TYPE& str, const VAL_TYPE& val) { return !(val < str); } \
    inline bool operator>=(const VAL_TYPE& val, const STR_TYPE& str) { return !(val < str); } \
    inline bool operator>=(const STR_TYPE& str, const VAL_TYPE& val) { return !(str < val); }

SQLITE_DEF_VAL_STR_OPS(SqliteValueOwned, SqliteStringView, data, length)
SQLITE_DEF_VAL_STR_OPS(SqliteValueOwned, SqliteStringOwned, value, length)
SQLITE_DEF_VAL_STR_OPS(SqliteValueView, SqliteStringView, data, length)
SQLITE_DEF_VAL_STR_OPS(SqliteValueView, SqliteStringOwned, value, length)

#define SQLITE_DEF_VAL_BLOB_OPS(VAL_TYPE, BLOB_TYPE, BLOB_DATA, BLOB_LEN) \
    inline bool operator==(const VAL_TYPE& val, const BLOB_TYPE& blob) { \
        if (val.type() != SQLITE_BLOB) return false; \
        SqliteBlobView bv = val.as_blob(); \
        return SqliteBlobUtil::equal(bv.data(), bv.size(), blob.BLOB_DATA(), blob.BLOB_LEN()); \
    } \
    inline bool operator==(const BLOB_TYPE& blob, const VAL_TYPE& val) { return val == blob; } \
    inline bool operator!=(const VAL_TYPE& val, const BLOB_TYPE& blob) { return !(val == blob); } \
    inline bool operator!=(const BLOB_TYPE& blob, const VAL_TYPE& val) { return !(val == blob); } \
    inline bool operator<(const VAL_TYPE& val, const BLOB_TYPE& blob) { \
        int r1 = (val.type() == SQLITE_NULL) ? 0 : (val.type() == SQLITE_INTEGER || val.type() == SQLITE_FLOAT) ? 1 : (val.type() == SQLITE_TEXT) ? 2 : 3; \
        if (r1 != 3) return r1 < 3; \
        SqliteBlobView bv = val.as_blob(); \
        return SqliteBlobUtil::less(bv.data(), bv.size(), blob.BLOB_DATA(), blob.BLOB_LEN()); \
    } \
    inline bool operator<(const BLOB_TYPE& blob, const VAL_TYPE& val) { \
        int r2 = (val.type() == SQLITE_NULL) ? 0 : (val.type() == SQLITE_INTEGER || val.type() == SQLITE_FLOAT) ? 1 : (val.type() == SQLITE_TEXT) ? 2 : 3; \
        if (3 != r2) return 3 < r2; \
        SqliteBlobView bv = val.as_blob(); \
        return SqliteBlobUtil::less(blob.BLOB_DATA(), blob.BLOB_LEN(), bv.data(), bv.size()); \
    } \
    inline bool operator>(const VAL_TYPE& val, const BLOB_TYPE& blob) { return blob < val; } \
    inline bool operator>(const BLOB_TYPE& blob, const VAL_TYPE& val) { return val < blob; } \
    inline bool operator<=(const VAL_TYPE& val, const BLOB_TYPE& blob) { return !(blob < val); } \
    inline bool operator<=(const BLOB_TYPE& blob, const VAL_TYPE& val) { return !(val < blob); } \
    inline bool operator>=(const VAL_TYPE& val, const BLOB_TYPE& blob) { return !(val < blob); } \
    inline bool operator>=(const BLOB_TYPE& blob, const VAL_TYPE& val) { return !(blob < val); }

SQLITE_DEF_VAL_BLOB_OPS(SqliteValueOwned, SqliteBlobView, data, size)
SQLITE_DEF_VAL_BLOB_OPS(SqliteValueOwned, SqliteBlobOwned, data, size)
SQLITE_DEF_VAL_BLOB_OPS(SqliteValueView, SqliteBlobView, data, size)
SQLITE_DEF_VAL_BLOB_OPS(SqliteValueView, SqliteBlobOwned, data, size)

// ============================================================================
// HETEROGENEOUS LOOKUPS: VALUES VS PRIMITIVES
// ============================================================================

#define SQLITE_DEF_VAL_PRIM_OPS(VAL_TYPE) \
    inline bool operator==(const VAL_TYPE& val, sqlite3_int64 num) { return val.type() == SQLITE_INTEGER && val.as_int64() == num; } \
    inline bool operator==(sqlite3_int64 num, const VAL_TYPE& val) { return val == num; } \
    inline bool operator!=(const VAL_TYPE& val, sqlite3_int64 num) { return !(val == num); } \
    inline bool operator!=(sqlite3_int64 num, const VAL_TYPE& val) { return !(val == num); } \
    inline bool operator<(const VAL_TYPE& val, sqlite3_int64 num) { \
        int t = val.type(); \
        int r1 = (t == SQLITE_NULL) ? 0 : (t == SQLITE_INTEGER || t == SQLITE_FLOAT) ? 1 : (t == SQLITE_TEXT) ? 2 : 3; \
        if (r1 != 1) return r1 < 1; \
        if (t == SQLITE_INTEGER) return val.as_int64() < num; \
        double d1 = val.as_double(), d2 = static_cast<double>(num); \
        if (d1 != d1) return true; \
        if (d1 != d2) return d1 < d2; \
        return SQLITE_FLOAT < SQLITE_INTEGER; \
    } \
    inline bool operator<(sqlite3_int64 num, const VAL_TYPE& val) { \
        int t = val.type(); \
        int r2 = (t == SQLITE_NULL) ? 0 : (t == SQLITE_INTEGER || t == SQLITE_FLOAT) ? 1 : (t == SQLITE_TEXT) ? 2 : 3; \
        if (1 != r2) return 1 < r2; \
        if (t == SQLITE_INTEGER) return num < val.as_int64(); \
        double d1 = static_cast<double>(num), d2 = val.as_double(); \
        if (d2 != d2) return false; \
        if (d1 != d2) return d1 < d2; \
        return SQLITE_INTEGER < SQLITE_FLOAT; \
    } \
    inline bool operator>(const VAL_TYPE& val, sqlite3_int64 num) { return num < val; } \
    inline bool operator>(sqlite3_int64 num, const VAL_TYPE& val) { return val < num; } \
    inline bool operator<=(const VAL_TYPE& val, sqlite3_int64 num) { return !(num < val); } \
    inline bool operator<=(sqlite3_int64 num, const VAL_TYPE& val) { return !(val < num); } \
    inline bool operator>=(const VAL_TYPE& val, sqlite3_int64 num) { return !(val < num); } \
    inline bool operator>=(sqlite3_int64 num, const VAL_TYPE& val) { return !(num < val); } \
    inline bool operator==(const VAL_TYPE& val, int num) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator==(int num, const VAL_TYPE& val) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(const VAL_TYPE& val, int num) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(int num, const VAL_TYPE& val) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator<(const VAL_TYPE& val, int num) { return val < static_cast<sqlite3_int64>(num); } \
    inline bool operator<(int num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) < val; } \
    inline bool operator>(const VAL_TYPE& val, int num) { return val > static_cast<sqlite3_int64>(num); } \
    inline bool operator>(int num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) > val; } \
    inline bool operator<=(const VAL_TYPE& val, int num) { return val <= static_cast<sqlite3_int64>(num); } \
    inline bool operator<=(int num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) <= val; } \
    inline bool operator>=(const VAL_TYPE& val, int num) { return val >= static_cast<sqlite3_int64>(num); } \
    inline bool operator>=(int num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) >= val; } \
    inline bool operator==(const VAL_TYPE& val, long num) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator==(long num, const VAL_TYPE& val) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(const VAL_TYPE& val, long num) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(long num, const VAL_TYPE& val) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator<(const VAL_TYPE& val, long num) { return val < static_cast<sqlite3_int64>(num); } \
    inline bool operator<(long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) < val; } \
    inline bool operator>(const VAL_TYPE& val, long num) { return val > static_cast<sqlite3_int64>(num); } \
    inline bool operator>(long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) > val; } \
    inline bool operator<=(const VAL_TYPE& val, long num) { return val <= static_cast<sqlite3_int64>(num); } \
    inline bool operator<=(long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) <= val; } \
    inline bool operator>=(const VAL_TYPE& val, long num) { return val >= static_cast<sqlite3_int64>(num); } \
    inline bool operator>=(long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) >= val; } \
    inline bool operator==(const VAL_TYPE& val, unsigned int num) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator==(unsigned int num, const VAL_TYPE& val) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(const VAL_TYPE& val, unsigned int num) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(unsigned int num, const VAL_TYPE& val) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator<(const VAL_TYPE& val, unsigned int num) { return val < static_cast<sqlite3_int64>(num); } \
    inline bool operator<(unsigned int num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) < val; } \
    inline bool operator>(const VAL_TYPE& val, unsigned int num) { return val > static_cast<sqlite3_int64>(num); } \
    inline bool operator>(unsigned int num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) > val; } \
    inline bool operator<=(const VAL_TYPE& val, unsigned int num) { return val <= static_cast<sqlite3_int64>(num); } \
    inline bool operator<=(unsigned int num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) <= val; } \
    inline bool operator>=(const VAL_TYPE& val, unsigned int num) { return val >= static_cast<sqlite3_int64>(num); } \
    inline bool operator>=(unsigned int num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) >= val; } \
    inline bool operator==(const VAL_TYPE& val, unsigned long num) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator==(unsigned long num, const VAL_TYPE& val) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(const VAL_TYPE& val, unsigned long num) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(unsigned long num, const VAL_TYPE& val) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator<(const VAL_TYPE& val, unsigned long num) { return val < static_cast<sqlite3_int64>(num); } \
    inline bool operator<(unsigned long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) < val; } \
    inline bool operator>(const VAL_TYPE& val, unsigned long num) { return val > static_cast<sqlite3_int64>(num); } \
    inline bool operator>(unsigned long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) > val; } \
    inline bool operator<=(const VAL_TYPE& val, unsigned long num) { return val <= static_cast<sqlite3_int64>(num); } \
    inline bool operator<=(unsigned long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) <= val; } \
    inline bool operator>=(const VAL_TYPE& val, unsigned long num) { return val >= static_cast<sqlite3_int64>(num); } \
    inline bool operator>=(unsigned long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) >= val; } \
    inline bool operator==(const VAL_TYPE& val, unsigned long long num) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator==(unsigned long long num, const VAL_TYPE& val) { return val == static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(const VAL_TYPE& val, unsigned long long num) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator!=(unsigned long long num, const VAL_TYPE& val) { return val != static_cast<sqlite3_int64>(num); } \
    inline bool operator<(const VAL_TYPE& val, unsigned long long num) { return val < static_cast<sqlite3_int64>(num); } \
    inline bool operator<(unsigned long long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) < val; } \
    inline bool operator>(const VAL_TYPE& val, unsigned long long num) { return val > static_cast<sqlite3_int64>(num); } \
    inline bool operator>(unsigned long long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) > val; } \
    inline bool operator<=(const VAL_TYPE& val, unsigned long long num) { return val <= static_cast<sqlite3_int64>(num); } \
    inline bool operator<=(unsigned long long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) <= val; } \
    inline bool operator>=(const VAL_TYPE& val, unsigned long long num) { return val >= static_cast<sqlite3_int64>(num); } \
    inline bool operator>=(unsigned long long num, const VAL_TYPE& val) { return static_cast<sqlite3_int64>(num) >= val; } \
    inline bool operator==(const VAL_TYPE& val, double num) { \
        if (val.type() != SQLITE_FLOAT) return false; \
        double d = val.as_double(); \
        if (d != d && num != num) return true; \
        return d == num; \
    } \
    inline bool operator==(double num, const VAL_TYPE& val) { return val == num; } \
    inline bool operator!=(const VAL_TYPE& val, double num) { return !(val == num); } \
    inline bool operator!=(double num, const VAL_TYPE& val) { return !(val == num); } \
    inline bool operator<(const VAL_TYPE& val, double num) { \
        int t = val.type(); \
        int r1 = (t == SQLITE_NULL) ? 0 : (t == SQLITE_INTEGER || t == SQLITE_FLOAT) ? 1 : (t == SQLITE_TEXT) ? 2 : 3; \
        if (r1 != 1) return r1 < 1; \
        double d1 = (t == SQLITE_INTEGER) ? static_cast<double>(val.as_int64()) : val.as_double(); \
        if (d1 != d1 && num != num) return false; \
        if (d1 != d1) return true; \
        if (num != num) return false; \
        if (d1 != num) return d1 < num; \
        if (t == SQLITE_INTEGER) return SQLITE_INTEGER < SQLITE_FLOAT; \
        return false; \
    } \
    inline bool operator<(double num, const VAL_TYPE& val) { \
        int t = val.type(); \
        int r2 = (t == SQLITE_NULL) ? 0 : (t == SQLITE_INTEGER || t == SQLITE_FLOAT) ? 1 : (t == SQLITE_TEXT) ? 2 : 3; \
        if (1 != r2) return 1 < r2; \
        double d2 = (t == SQLITE_INTEGER) ? static_cast<double>(val.as_int64()) : val.as_double(); \
        if (num != num && d2 != d2) return false; \
        if (num != num) return true; \
        if (d2 != d2) return false; \
        if (num != d2) return num < d2; \
        if (t == SQLITE_INTEGER) return SQLITE_FLOAT < SQLITE_INTEGER; \
        return false; \
    } \
    inline bool operator>(const VAL_TYPE& val, double num) { return num < val; } \
    inline bool operator>(double num, const VAL_TYPE& val) { return val < num; } \
    inline bool operator<=(const VAL_TYPE& val, double num) { return !(num < val); } \
    inline bool operator<=(double num, const VAL_TYPE& val) { return !(val < num); } \
    inline bool operator>=(const VAL_TYPE& val, double num) { return !(val < num); } \
    inline bool operator>=(double num, const VAL_TYPE& val) { return !(num < val); }

SQLITE_DEF_VAL_PRIM_OPS(SqliteValueOwned)
SQLITE_DEF_VAL_PRIM_OPS(SqliteValueView)

// ============================================================================
// TRANSPARENT HASH MAP FUNCTORS (C++20 Heterogeneous Lookups)
// ============================================================================

#ifndef SQLITE_DERIVE_TRANSPARENT_EQUAL
#define SQLITE_DERIVE_TRANSPARENT_EQUAL(FunctorName) \
    struct FunctorName { \
        using is_transparent = void; \
        template <typename T, typename U> \
        inline bool operator()(const T& a, const U& b) const noexcept { \
            return a == b; \
        } \
    };
#endif

#ifndef SQLITE_DERIVE_TRANSPARENT_LESS
#define SQLITE_DERIVE_TRANSPARENT_LESS(FunctorName) \
    struct FunctorName { \
        using is_transparent = void; \
        template <typename T, typename U> \
        inline bool operator()(const T& a, const U& b) const noexcept { \
            return a < b; \
        } \
    };
#endif

#ifndef SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH_OVERLOADS
#define SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH_OVERLOADS \
    inline size_t operator()(const SqliteValueOwned& val) const noexcept  { return static_cast<size_t>(val.hash()); } \
    inline size_t operator()(const SqliteValueView& val) const noexcept   { return static_cast<size_t>(val.hash()); } \
    inline size_t operator()(const SqliteStringView& str) const noexcept  { return static_cast<size_t>(str.hash()); } \
    inline size_t operator()(const SqliteStringOwned& str) const noexcept { return static_cast<size_t>(str.hash()); } \
    inline size_t operator()(const SqliteBlobView& blob) const noexcept   { return static_cast<size_t>(blob.hash()); } \
    inline size_t operator()(const SqliteBlobOwned& blob) const noexcept  { return static_cast<size_t>(blob.hash()); } \
    inline size_t operator()(const char* str) const noexcept { \
        return static_cast<size_t>(SqliteStringUtil::hash(str, SqliteStringUtil::sqlite_strlen(str))); \
    } \
    inline size_t operator()(sqlite3_int64 i) const noexcept { \
        return static_cast<size_t>(SqliteHashUtil::hash(&i, sizeof(i))); \
    } \
    inline size_t operator()(int i) const noexcept { \
        sqlite3_int64 val = i; \
        return static_cast<size_t>(SqliteHashUtil::hash(&val, sizeof(val))); \
    } \
    inline size_t operator()(double d) const noexcept { \
        return static_cast<size_t>(SqliteHashUtil::hash(&d, sizeof(d))); \
    } \
    inline size_t operator()(float f) const noexcept { \
        double d = f; \
        return static_cast<size_t>(SqliteHashUtil::hash(&d, sizeof(d))); \
    } \
    inline size_t operator()(bool b) const noexcept { \
        sqlite3_int64 val = b ? 1 : 0; \
        return static_cast<size_t>(SqliteHashUtil::hash(&val, sizeof(val))); \
    } \
    inline size_t operator()(uint32_t u) const noexcept { \
        sqlite3_int64 val = u; \
        return static_cast<size_t>(SqliteHashUtil::hash(&val, sizeof(val))); \
    } \
    inline size_t operator()(uint64_t u) const noexcept { \
        return static_cast<size_t>(SqliteHashUtil::hash(&u, sizeof(u))); \
    }
#endif

/**
 * @brief Transparent Hash functor for std::unordered_map.
 * Enables zero-allocation heterogeneous lookups across Strings, Blobs, and Primitives.
 */
struct SqliteValueHash {
    using is_transparent = void;
    SQLITE_DERIVE_TRANSPARENT_SCALAR_HASH_OVERLOADS
};

/**
 * @brief Transparent Equality functor for std::unordered_map.
 */
SQLITE_DERIVE_TRANSPARENT_EQUAL(SqliteValueEqual)

/**
 * @brief Transparent Less-Than functor for std::map / B-Trees.
 */
SQLITE_DERIVE_TRANSPARENT_LESS(SqliteValueLess)

#endif // SQLITE3_VALUE_HPP
