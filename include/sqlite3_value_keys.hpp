#ifndef SQLITE3_VALUE_KEYS_HPP
#define SQLITE3_VALUE_KEYS_HPP

#include "sqlite3ext.h"
#include <stdarg.h>
#include <string.h>

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
}

/**
 * @file sqlite3_value_keys.hpp
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
     * @brief Computes a 64-bit FNV-1a hash of a character array.
     * @param val Pointer to the string data.
     * @param len Length of the string in bytes.
     * @return 64-bit hash value.
     */
    inline unsigned long long hash(const char* val, int len) {
        static const unsigned long long FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;
        static const unsigned long long FNV_PRIME = 0x100000001b3ULL;

        if (!val || len == 0) {
            return FNV_OFFSET_BASIS;
        }

        unsigned long long h = FNV_OFFSET_BASIS;
        for (int i = 0; i < len; ++i) {
            h ^= (unsigned char)val[i];
            h *= FNV_PRIME;
        }
        return h;
    }
    
    /**
     * @brief Checks if two character arrays are exactly equal.
     */
    inline bool equal(const char* val1, int len1, const char* val2, int len2) {
        if (len1 != len2) {
            return false;
        }
        if (len1 == 0) {
            return true;
        }
        if (val1 == val2) {
            return true;
        }
        if (!val1 || !val2) {
            return false;
        }

        return memcmp(val1, val2, len1) == 0;
    }
    
    /**
     * @brief Performs a lexicographical less-than comparison of two character arrays.
     */
    inline bool less(const char* val1, int len1, const char* val2, int len2) {
        if (!val1 && !val2) {
            return false;
        }
        if (!val1) {
            return true;
        }
        if (!val2) {
            return false;
        }

        return SqliteMemoryUtil::memcmp_less(val1, len1, val2, len2);
    }
}

class SqliteStringOwned;

/**
 * @brief Zero-cost, non-owning C++ wrapper for strings.
 * 
 * Perfect for heterogenous map lookups (e.g. querying a map of Owned strings 
 * using a temporary C-string without allocating memory).
 */
class SqliteStringView {
    const char* m_data;
    int m_size;

public:
    /** @brief Sets this object as the return result of a SQLite UDF context. */
    void result(sqlite3_context* ctx) const {
        sqlite3_result_text(ctx, data(), length(), SQLITE_TRANSIENT);
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col) const {
        return sqlite3_bind_text(stmt, col, data(), length(), SQLITE_TRANSIENT);
    }
    /**
     * @brief Constructs a view over an existing string buffer.
     * @param data Pointer to the character array.
     * @param size Length of the string in bytes.
     */
    SqliteStringView(const char* data, int size) : m_data(data), m_size(size) {}
    SqliteStringView() : m_data(nullptr), m_size(0) {}
    
    /** @brief Returns a pointer to the underlying string data. */
    const char* data() const {
        return m_data;
    }
    
    /** @brief Returns the length of the string in bytes. */
    int length() const {
        return m_size;
    }
    
    /** @brief Computes the FNV-1a hash of the string. */
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
    void result(sqlite3_context* ctx) const {
        sqlite3_result_text(ctx, value(), length(), SQLITE_TRANSIENT);
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col) const {
        return sqlite3_bind_text(stmt, col, value(), length(), SQLITE_TRANSIENT);
    }
    /**
     * @brief Creates a new string builder tied to a specific database connection.
     * @param db The SQLite database connection.
     */
    explicit SqliteStringOwned(sqlite3* db) {
        m_str = sqlite3_str_new(db);
    }
    
    /**
     * @brief Creates a new string builder tied to a UDF execution context.
     * @param ctx The SQLite execution context.
     */
    explicit SqliteStringOwned(sqlite3_context* ctx) {
        m_str = sqlite3_str_new(sqlite3_context_db_handle(ctx));
    }
    
    /**
     * @brief Creates a new string builder not tied to any database.
     * Uses global sqlite3_malloc.
     */
    SqliteStringOwned() {
        m_str = sqlite3_str_new(nullptr);
    }

    /**
     * @brief Creates a new string and immediately copies the provided text.
     * Uses global sqlite3_malloc.
     */
    explicit SqliteStringOwned(const char* text) {
        m_str = sqlite3_str_new(nullptr);
        if (text) {
            sqlite3_str_appendall(m_str, text);
        }
    }
    
    /**
     * @brief Destructor. Automatically frees the memory if it was never finished.
     */
    ~SqliteStringOwned() {
        if (m_str) {
            sqlite3_free(sqlite3_str_finish(m_str));
        }
    }
    
    // Disallow copying to prevent double-free
    SqliteStringOwned(const SqliteStringOwned&) = delete;
    SqliteStringOwned& operator=(const SqliteStringOwned&) = delete;
    
    // Allow moving
    SqliteStringOwned(SqliteStringOwned&& other) noexcept : m_str(other.m_str) {
        other.m_str = nullptr;
    }

    SqliteStringOwned& operator=(SqliteStringOwned&& other) noexcept {
        if (this != &other) {
            if (m_str) {
                sqlite3_free(sqlite3_str_finish(m_str));
            }
            m_str = other.m_str;
            other.m_str = nullptr;
        }
        return *this;
    }

    /** @brief Appends exactly N bytes of text. */
    SqliteStringOwned& append(const char* zIn, int N) {
        if (m_str) {
            sqlite3_str_append(m_str, zIn, N);
        }
        return *this;
    }
    
    /** @brief Appends a null-terminated string. */
    SqliteStringOwned& appendall(const char* zIn) {
        if (m_str) {
            sqlite3_str_appendall(m_str, zIn);
        }
        return *this;
    }
    
    /** @brief Appends character C exactly N times. */
    SqliteStringOwned& appendchar(int N, char C) {
        if (m_str) {
            sqlite3_str_appendchar(m_str, N, C);
        }
        return *this;
    }
    
    /** @brief Appends formatted text (like printf). */
    SqliteStringOwned& appendf(const char* zFormat, ...) {
        if (!m_str) {
            return *this;
        }
        va_list args;
        va_start(args, zFormat);
        sqlite3_str_vappendf(m_str, zFormat, args);
        va_end(args);
        return *this;
    }
    
    /** @brief Appends formatted text using a va_list. */
    SqliteStringOwned& vappendf(const char* zFormat, va_list args) {
        if (m_str) {
            sqlite3_str_vappendf(m_str, zFormat, args);
        }
        return *this;
    }
    
    /** @brief Resets the builder to an empty state. */
    void reset() {
        if (m_str) {
            sqlite3_str_reset(m_str);
        }
    }
    
    /** @brief Returns the current SQLite error code (e.g. SQLITE_NOMEM). */
    int errcode() const {
        return m_str ? sqlite3_str_errcode(m_str) : SQLITE_NOMEM;
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
        if (!m_str) {
            return nullptr;
        }
        char* result = sqlite3_str_finish(m_str);
        m_str = nullptr; 
        return result;
    }

    /** @brief Computes the FNV-1a hash of the built string. */
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
     * @brief Computes a 64-bit FNV-1a hash of a binary buffer.
     */
    inline unsigned long long hash(const void* val, int len) {
        static const unsigned long long FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;
        static const unsigned long long FNV_PRIME = 0x100000001b3ULL;

        if (!val || len == 0) {
            return FNV_OFFSET_BASIS;
        }

        unsigned long long h = FNV_OFFSET_BASIS;
        const char* ptr = static_cast<const char*>(val);
        for (int i = 0; i < len; ++i) {
            h ^= (unsigned char)ptr[i];
            h *= FNV_PRIME;
        }
        return h;
    }
    
    /**
     * @brief Checks if two binary buffers are exactly equal.
     */
    inline bool equal(const void* val1, int len1, const void* val2, int len2) {
        if (len1 != len2) {
            return false;
        }
        if (len1 == 0) {
            return true;
        }
        if (val1 == val2) {
            return true;
        }
        if (!val1 || !val2) {
            return false;
        }

        return memcmp(val1, val2, len1) == 0;
    }
    
    /**
     * @brief Performs a lexicographical less-than comparison of two binary buffers.
     */
    inline bool less(const void* val1, int len1, const void* val2, int len2) {
        if (!val1 && !val2) {
            return false;
        }
        if (!val1) {
            return true;
        }
        if (!val2) {
            return false;
        }

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
    void result(sqlite3_context* ctx) const {
        sqlite3_result_blob(ctx, data(), size(), SQLITE_TRANSIENT);
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col) const {
        return sqlite3_bind_blob(stmt, col, data(), size(), SQLITE_TRANSIENT);
    }
    /**
     * @brief Constructs a view over an existing binary buffer.
     * @param data Pointer to the binary payload.
     * @param size Length of the payload in bytes.
     */
    SqliteBlobView(const void* data, int size) : m_data(data), m_size(size) {}
    SqliteBlobView() : m_data(nullptr), m_size(0) {}
    
    /** @brief Returns a pointer to the underlying binary data. */
    const void* data() const {
        return m_data;
    }
    
    /** @brief Returns the size of the binary payload in bytes. */
    int size() const {
        return m_size;
    }

    /** @brief Computes the FNV-1a hash of the binary payload. */
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
    void result(sqlite3_context* ctx) const {
        sqlite3_result_blob(ctx, data(), size(), SQLITE_TRANSIENT);
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col) const {
        return sqlite3_bind_blob(stmt, col, data(), size(), SQLITE_TRANSIENT);
    }
    /**
     * @brief Allocates memory and copies the provided binary data.
     * @param data Pointer to the binary payload to copy.
     * @param size Length of the payload in bytes.
     */
    SqliteBlobOwned(const void* data, int size) {
        m_size = size;
        if (size > 0 && data) {
            m_data = sqlite3_malloc(size);
            if (m_data) {
                const char* src = static_cast<const char*>(data);
                char* dst = static_cast<char*>(m_data);
                for (int i = 0; i < size; ++i) {
                    dst[i] = src[i];
                }
            } else {
                m_size = 0;
            }
        } else {
            m_data = nullptr;
        }
    }
    
    /** @brief Destructor. Automatically frees the allocated memory. */
    ~SqliteBlobOwned() {
        if (m_data) {
            sqlite3_free(m_data);
        }
    }
    
    // Disallow copying to prevent double-free
    SqliteBlobOwned(const SqliteBlobOwned&) = delete;
    SqliteBlobOwned& operator=(const SqliteBlobOwned&) = delete;
    
    // Allow moving
    SqliteBlobOwned(SqliteBlobOwned&& other) noexcept : m_data(other.m_data), m_size(other.m_size) {
        other.m_data = nullptr;
        other.m_size = 0;
    }

    SqliteBlobOwned& operator=(SqliteBlobOwned&& other) noexcept {
        if (this != &other) {
            if (m_data) {
                sqlite3_free(m_data);
            }
            m_data = other.m_data;
            m_size = other.m_size;
            other.m_data = nullptr;
            other.m_size = 0;
        }
        return *this;
    }

    /** @brief Returns a read-only pointer to the owned binary data. */
    const void* data() const {
        return m_data;
    }
    
    /** @brief Returns the size of the owned binary payload in bytes. */
    int size() const {
        return m_size;
    }

    /** @brief Computes the FNV-1a hash of the owned payload. */
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
        static const unsigned long long FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;
        static const unsigned long long FNV_PRIME = 0x100000001b3ULL;

        if (!val) {
            return FNV_OFFSET_BASIS;
        }
        
        sqlite3_value* mut_val = const_cast<sqlite3_value*>(val);
        int t = sqlite3_value_type(mut_val);
        unsigned long long h = FNV_OFFSET_BASIS;
        
        h ^= (unsigned char)t;
        h *= FNV_PRIME;
        
        switch (t) {
            case SQLITE_INTEGER: {
                sqlite3_int64 i_val = sqlite3_value_int64(mut_val);
                const char* ptr = reinterpret_cast<const char*>(&i_val);
                for (unsigned int i = 0; i < sizeof(sqlite3_int64); ++i) {
                    h ^= (unsigned char)ptr[i];
                    h *= FNV_PRIME;
                }
                break;
            }
            case SQLITE_FLOAT: {
                double d_val = sqlite3_value_double(mut_val);
                const char* ptr = reinterpret_cast<const char*>(&d_val);
                for (unsigned int i = 0; i < sizeof(double); ++i) {
                    h ^= (unsigned char)ptr[i];
                    h *= FNV_PRIME;
                }
                break;
            }
            case SQLITE_TEXT: {
                const char* txt = reinterpret_cast<const char*>(sqlite3_value_text(mut_val));
                int bytes = sqlite3_value_bytes(mut_val);
                if (txt) {
                    for (int i = 0; i < bytes; ++i) {
                        h ^= (unsigned char)txt[i];
                        h *= FNV_PRIME;
                    }
                }
                break;
            }
            case SQLITE_BLOB: {
                const char* blob = reinterpret_cast<const char*>(sqlite3_value_blob(mut_val));
                int bytes = sqlite3_value_bytes(mut_val);
                if (blob) {
                    for (int i = 0; i < bytes; ++i) {
                        h ^= (unsigned char)blob[i];
                        h *= FNV_PRIME;
                    }
                }
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
        if (t1 != t2) {
            return false;
        }
        
        sqlite3_value* mut_v1 = const_cast<sqlite3_value*>(v1);
        sqlite3_value* mut_v2 = const_cast<sqlite3_value*>(v2);

        switch (t1) {
            case SQLITE_INTEGER: 
                return sqlite3_value_int64(mut_v1) == sqlite3_value_int64(mut_v2);
            case SQLITE_FLOAT: 
                return sqlite3_value_double(mut_v1) == sqlite3_value_double(mut_v2);
            case SQLITE_TEXT: {
                int bytes1 = sqlite3_value_bytes(mut_v1);
                int bytes2 = sqlite3_value_bytes(mut_v2);
                if (bytes1 != bytes2) {
                    return false;
                }
                const char* txt1 = reinterpret_cast<const char*>(sqlite3_value_text(mut_v1));
                const char* txt2 = reinterpret_cast<const char*>(sqlite3_value_text(mut_v2));
                if (!txt1 || !txt2) {
                    return txt1 == txt2;
                }
                for (int i = 0; i < bytes1; ++i) {
                    if (txt1[i] != txt2[i]) {
                        return false;
                    }
                }
                return true;
            }
            case SQLITE_BLOB: {
                int bytes1 = sqlite3_value_bytes(mut_v1);
                int bytes2 = sqlite3_value_bytes(mut_v2);
                if (bytes1 != bytes2) {
                    return false;
                }
                const char* blob1 = reinterpret_cast<const char*>(sqlite3_value_blob(mut_v1));
                const char* blob2 = reinterpret_cast<const char*>(sqlite3_value_blob(mut_v2));
                if (!blob1 || !blob2) {
                    return blob1 == blob2;
                }
                for (int i = 0; i < bytes1; ++i) {
                    if (blob1[i] != blob2[i]) {
                        return false;
                    }
                }
                return true;
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
        
        if (r1 != r2) {
            return r1 < r2;
        }
        
        sqlite3_value* mut_v1 = const_cast<sqlite3_value*>(v1);
        sqlite3_value* mut_v2 = const_cast<sqlite3_value*>(v2);

        // Both are Numeric: compare as double, or as int64 if both are integers
        if (r1 == 1) {
            if (t1 == SQLITE_INTEGER && t2 == SQLITE_INTEGER) {
                return sqlite3_value_int64(mut_v1) < sqlite3_value_int64(mut_v2);
            }
            double d1 = sqlite3_value_double(mut_v1);
            double d2 = sqlite3_value_double(mut_v2);
            return d1 < d2;
        }

        switch (t1) {
            case SQLITE_TEXT: 
            case SQLITE_BLOB: {
                int len1 = sqlite3_value_bytes(mut_v1);
                int len2 = sqlite3_value_bytes(mut_v2);
                const char* data1 = (t1 == SQLITE_TEXT) ? reinterpret_cast<const char*>(sqlite3_value_text(mut_v1)) : reinterpret_cast<const char*>(sqlite3_value_blob(mut_v1));
                const char* data2 = (t2 == SQLITE_TEXT) ? reinterpret_cast<const char*>(sqlite3_value_text(mut_v2)) : reinterpret_cast<const char*>(sqlite3_value_blob(mut_v2));
                
                if (!data1 && !data2) {
                    return false;
                }
                if (!data1) {
                    return true;
                }
                if (!data2) {
                    return false;
                }
                
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
    void result(sqlite3_context* ctx) const {
        sqlite3_result_value(ctx, const_cast<sqlite3_value*>(get()));
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col) const {
        return sqlite3_bind_value(stmt, col, get());
    }
    /**
     * @brief Constructs a view over a temporary `sqlite3_value`.
     * @param val The transient value pointer passed from SQLite.
     */
    explicit SqliteValueView(const sqlite3_value* val) : m_val(val) {}
    
    // Copyable and Movable since it's just a pointer wrapper
    SqliteValueView(const SqliteValueView&) = default;
    SqliteValueView& operator=(const SqliteValueView&) = default;

    /** @brief Returns the underlying `sqlite3_value` pointer. */
    const sqlite3_value* get() const {
        return m_val;
    }
    
    /** @brief Returns the SQLite datatype (e.g. SQLITE_INTEGER). */
    int type() const {
        return SqliteValueUtil::type(m_val);
    }
    
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

/**
 * @brief Heavy, memory-managed polymorphic C++ RAII wrapper for `sqlite3_value`.
 * 
 * Safely duplicates a `sqlite3_value` object using `sqlite3_value_dup`.
 * Extremely powerful as a variant map key that can handle Ints, Strings, Blobs, 
 * and Reals dynamically without ever leaking memory.
 */
class SqliteValueOwned {
    sqlite3_value* m_val;

public:
    /** @brief Sets this object as the return result of a SQLite UDF context. */
    void result(sqlite3_context* ctx) const {
        sqlite3_result_value(ctx, const_cast<sqlite3_value*>(get()));
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col) const {
        return sqlite3_bind_value(stmt, col, get());
    }
    /**
     * @brief Duplicates the temporary value into owned memory.
     * @param val The transient value to copy.
     */
    explicit SqliteValueOwned(const sqlite3_value* val) {
        m_val = sqlite3_value_dup(val);
    }
    
    /** @brief Destructor. Automatically calls `sqlite3_value_free`. */
    ~SqliteValueOwned() {
        if (m_val) {
            sqlite3_value_free(m_val);
        }
    }

    // Disallow copying to prevent double-free
    SqliteValueOwned(const SqliteValueOwned&) = delete;
    SqliteValueOwned& operator=(const SqliteValueOwned&) = delete;

    // Allow moving
    SqliteValueOwned(SqliteValueOwned&& other) noexcept : m_val(other.m_val) {
        other.m_val = nullptr;
    }

    SqliteValueOwned& operator=(SqliteValueOwned&& other) noexcept {
        if (this != &other) {
            if (m_val) {
                sqlite3_value_free(m_val);
            }
            m_val = other.m_val;
            other.m_val = nullptr;
        }
        return *this;
    }

    /** @brief Returns a read-only pointer to the underlying owned value. */
    const sqlite3_value* get() const {
        return m_val;
    }
    
    /** @brief Returns the SQLite datatype (e.g. SQLITE_INTEGER). */
    int type() const {
        return SqliteValueUtil::type(m_val);
    }

    /** @brief Computes the polymorphic hash. */
    unsigned long long hash() const {
        return SqliteValueUtil::hash(m_val);
    }
    
    bool operator==(const SqliteValueOwned& other) const {
        return SqliteValueUtil::equal(m_val, other.m_val);
    }

    bool operator!=(const SqliteValueOwned& other) const {
        return !(*this == other);
    }

    bool operator<(const SqliteValueOwned& other) const {
        return SqliteValueUtil::less(m_val, other.m_val);
    }

    // Heterogeneous lookups
    bool operator==(const SqliteValueView& other) const {
        return SqliteValueUtil::equal(m_val, other.get());
    }

    bool operator!=(const SqliteValueView& other) const {
        return !(*this == other);
    }

    bool operator<(const SqliteValueView& other) const {
        return SqliteValueUtil::less(m_val, other.get());
    }
};

// Complete heterogeneous lookups for SqliteValueView
inline bool SqliteValueView::operator==(const SqliteValueOwned& other) const {
    return SqliteValueUtil::equal(m_val, other.get());
}

inline bool SqliteValueView::operator!=(const SqliteValueOwned& other) const {
    return !(*this == other);
}

inline bool SqliteValueView::operator<(const SqliteValueOwned& other) const {
    return SqliteValueUtil::less(m_val, other.get());
}

#endif // SQLITE3_VALUE_KEYS_HPP
