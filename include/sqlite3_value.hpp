#ifndef SQLITE3_VALUE_HPP
#define SQLITE3_VALUE_HPP

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

namespace SqliteHashUtil {
    /**
     * @brief Shared FNV-1a hash utility for fast, inline hashing.
     */
    static const unsigned long long FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;
    static const unsigned long long FNV_PRIME = 0x100000001b3ULL;

    inline unsigned long long mix(unsigned long long h, const void* ptr, int len) {
        if (!ptr || len <= 0) return h;
        const unsigned char* p = static_cast<const unsigned char*>(ptr);
        for (int i = 0; i < len; ++i) {
            h ^= p[i];
            h *= FNV_PRIME;
        }
        return h;
    }

    inline unsigned long long hash(const void* ptr, int len) {
        return mix(FNV_OFFSET_BASIS, ptr, len);
    }
}

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
     * @brief Computes a 64-bit FNV-1a hash of a character array.
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
    void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        sqlite3_result_text(ctx, data(), length(), dtor);
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_text(stmt, col, data(), length(), dtor);
    }
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
    void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        sqlite3_result_text(ctx, value(), length(), dtor);
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_text(stmt, col, value(), length(), dtor);
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
        return SqliteHashUtil::hash(val, len);
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
    void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        sqlite3_result_blob(ctx, data(), size(), dtor);
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_blob(stmt, col, data(), size(), dtor);
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
    void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        sqlite3_result_blob(ctx, data(), size(), dtor);
    }
    /** @brief Binds this object to a prepared SQLite statement at the given 1-based column index. */
    int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_blob(stmt, col, data(), size(), dtor);
    }
    /**
     * @brief Constructs an empty binary blob.
     */
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
                if (data) {
                    memcpy(m_data, data, size);
                }
            }
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
        if (!val) {
            return SqliteHashUtil::FNV_OFFSET_BASIS;
        }
        
        sqlite3_value* mut_val = const_cast<sqlite3_value*>(val);
        int t = sqlite3_value_type(mut_val);
        
        unsigned long long h = SqliteHashUtil::FNV_OFFSET_BASIS;
        
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
        if (t1 != t2) {
            return false;
        }
        
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
            
            // 1. NaN sorting stability (Sort NaNs first)
            bool isnan1 = (d1 != d1);
            bool isnan2 = (d2 != d2);
            if (isnan1 && !isnan2) return true;  
            if (!isnan1 && isnan2) return false; 
            
            // 2. Standard numeric comparison
            if (d1 != d2) {
                return d1 < d2;
            }
            
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
    
    /** @brief Internal helper to access integer value for heterogeneous lookups. */
    sqlite3_int64 as_int64() const { return m_val ? sqlite3_value_int64(const_cast<sqlite3_value*>(m_val)) : 0; }
    /** @brief Internal helper to access double value for heterogeneous lookups. */
    double as_double() const { return m_val ? sqlite3_value_double(const_cast<sqlite3_value*>(m_val)) : 0.0; }
    
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
    /**
     * @brief The SQLite data type of this value (e.g., SQLITE_INTEGER, SQLITE_TEXT).
     */
    int m_type;

    /**
     * @brief Small Buffer Optimization (SBO) Union.
     * 
     * Instead of always making a heap allocation to copy a `sqlite3_value`, 
     * this union stores small primitive types (Integers and Floats) directly inline.
     * `sqlite3_value_dup()` and the `pValue` pointer are only used for 
     * dynamically sized types (Text and Blobs).
     */
    union {
        sqlite3_int64 iValue;  // Active when m_type == SQLITE_INTEGER
        double dValue;         // Active when m_type == SQLITE_FLOAT
        sqlite3_value* pValue; // Active when m_type == SQLITE_TEXT or SQLITE_BLOB
    } m_data;

public:
    /** 
     * @brief Sets this object as the return result of a SQLite UDF context. 
     * 
     * Dynamically switches on the internal SBO type (`m_type`) to call the most efficient 
     * underlying SQLite C-API function (e.g. `sqlite3_result_int64` vs `sqlite3_result_value`).
     */
    void result(sqlite3_context* ctx) const {
        switch (m_type) {
            case SQLITE_INTEGER: sqlite3_result_int64(ctx, m_data.iValue); break;
            case SQLITE_FLOAT: sqlite3_result_double(ctx, m_data.dValue); break;
            case SQLITE_TEXT: 
            case SQLITE_BLOB: sqlite3_result_value(ctx, m_data.pValue); break;
            case SQLITE_NULL:
            default: sqlite3_result_null(ctx); break;
        }
    }

    /** 
     * @brief Binds this object to a prepared SQLite statement at the given 1-based column index. 
     * 
     * Dynamically switches on the internal SBO type (`m_type`) to call the most efficient 
     * underlying SQLite C-API function (e.g. `sqlite3_bind_int64` vs `sqlite3_bind_value`).
     */
    int bind(sqlite3_stmt* stmt, int col) const {
        switch (m_type) {
            case SQLITE_INTEGER: return sqlite3_bind_int64(stmt, col, m_data.iValue);
            case SQLITE_FLOAT: return sqlite3_bind_double(stmt, col, m_data.dValue);
            case SQLITE_TEXT: 
            case SQLITE_BLOB: return sqlite3_bind_value(stmt, col, m_data.pValue);
            case SQLITE_NULL:
            default: return sqlite3_bind_null(stmt, col);
        }
    }
    /**
     * @brief Duplicates the temporary value into owned memory or stores it inline.
     * 
     * Automatically applies Small Buffer Optimization (SBO). Integers and Floats
     * are stored inline to avoid `sqlite3_malloc` allocations. Strings and Blobs
     * are deep-copied into the heap via `sqlite3_value_dup()`.
     * 
     * @param val The transient value to copy.
     */
    explicit SqliteValueOwned(const sqlite3_value* val) {
        m_type = SqliteValueUtil::type(val);
        if (m_type == SQLITE_INTEGER) {
            m_data.iValue = sqlite3_value_int64(const_cast<sqlite3_value*>(val));
        } else if (m_type == SQLITE_FLOAT) {
            m_data.dValue = sqlite3_value_double(const_cast<sqlite3_value*>(val));
        } else if (m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) {
            m_data.pValue = sqlite3_value_dup(val);
        } else {
            m_data.pValue = nullptr; // Nulls require zero memory
        }
    }
    
    /** @brief Zero-allocation constructor for storing a 64-bit integer inline. */
    explicit SqliteValueOwned(sqlite3_int64 val) {
        m_type = SQLITE_INTEGER;
        m_data.iValue = val;
    }
    
    /** @brief Zero-allocation constructor for storing a double-precision float inline. */
    explicit SqliteValueOwned(double val) {
        m_type = SQLITE_FLOAT;
        m_data.dValue = val;
    }
    
    /** @brief Zero-allocation constructor for storing a standard int (prevents ambiguous overload). */
    explicit SqliteValueOwned(int val) {
        m_type = SQLITE_INTEGER;
        m_data.iValue = static_cast<sqlite3_int64>(val);
    }
    
    /** 
     * @brief Destructor. 
     * Automatically calls `sqlite3_value_free` ONLY if the value was heap-allocated 
     * (i.e. SQLITE_TEXT or SQLITE_BLOB). Inline types require no cleanup.
     */
    ~SqliteValueOwned() {
        if ((m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) && m_data.pValue) {
            sqlite3_value_free(m_data.pValue);
        }
    }

    // Disallow copying to prevent double-free
    SqliteValueOwned(const SqliteValueOwned&) = delete;
    SqliteValueOwned& operator=(const SqliteValueOwned&) = delete;

    // Allow moving
    SqliteValueOwned(SqliteValueOwned&& other) noexcept : m_type(other.m_type), m_data(other.m_data) {
        other.m_type = SQLITE_NULL;
        other.m_data.pValue = nullptr;
    }

    SqliteValueOwned& operator=(SqliteValueOwned&& other) noexcept {
        if (this != &other) {
            if ((m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) && m_data.pValue) {
                sqlite3_value_free(m_data.pValue);
            }
            m_type = other.m_type;
            m_data = other.m_data;
            other.m_type = SQLITE_NULL;
            other.m_data.pValue = nullptr;
        }
        return *this;
    }
    
    /** @brief Returns the SQLite datatype (e.g. SQLITE_INTEGER). */
    int type() const {
        return m_type;
    }

    /** @brief Internal helper to access heap-allocated sqlite3_value pointers for heterogeneous lookups. */
    const sqlite3_value* heap_value() const {
        return (m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) ? m_data.pValue : nullptr;
    }

    /** @brief Internal helper to access SBO integer for heterogeneous lookups. */
    sqlite3_int64 as_int64() const { return m_data.iValue; }
    /** @brief Internal helper to access SBO double for heterogeneous lookups. */
    double as_double() const { return m_data.dValue; }

    /** 
     * @brief Computes a polymorphic FNV-1a hash of the value.
     * 
     * Hashes the type ID first to guarantee no collisions between Ints, Floats, and Strings.
     * Seamlessly hashes inline SBO types or delegates to `SqliteValueUtil` for heap types.
     */
    unsigned long long hash() const {
        unsigned long long h = SqliteHashUtil::FNV_OFFSET_BASIS;

        if (m_type == SQLITE_INTEGER) {
            h = SqliteHashUtil::mix(h, &m_data.iValue, sizeof(m_data.iValue));
        } else if (m_type == SQLITE_FLOAT) {
            double d = m_data.dValue == 0.0 ? 0.0 : m_data.dValue;
            h = SqliteHashUtil::mix(h, &d, sizeof(d));
        } else if (m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) {
            return SqliteValueUtil::hash(m_data.pValue);
        }
        return h;
    }
    
    /**
     * @brief Performs a strict polymorphic equality check against another owned value.
     * Returns false immediately if the SQLite types differ.
     */
    bool operator==(const SqliteValueOwned& other) const {
        if (m_type != other.m_type) return false;
        if (m_type == SQLITE_INTEGER) return m_data.iValue == other.m_data.iValue;
        if (m_type == SQLITE_FLOAT) {
            // Explicitly handle NaN == NaN for map key stability
            if (m_data.dValue != m_data.dValue && other.m_data.dValue != other.m_data.dValue) return true;
            return m_data.dValue == other.m_data.dValue;
        }
        if (m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) {
            return SqliteValueUtil::equal(m_data.pValue, other.m_data.pValue);
        }
        return true; // SQLITE_NULL == SQLITE_NULL
    }

    /** @brief Performs a strict polymorphic inequality check against another owned value. */
    bool operator!=(const SqliteValueOwned& other) const {
        return !(*this == other);
    }

    /**
     * @brief Performs a polymorphic less-than comparison.
     * Sorts strictly by type first (NULL < Numeric < Text < Blob), then by value.
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

        int r1 = type_rank(m_type);
        int r2 = type_rank(other.m_type);
        if (r1 != r2) return r1 < r2;

        if (r1 == 1) { // Numeric
            if (m_type == SQLITE_INTEGER && other.m_type == SQLITE_INTEGER) {
                return m_data.iValue < other.m_data.iValue;
            }
            double d1 = (m_type == SQLITE_INTEGER) ? (double)m_data.iValue : m_data.dValue;
            double d2 = (other.m_type == SQLITE_INTEGER) ? (double)other.m_data.iValue : other.m_data.dValue;
            
            // 1. NaN sorting stability (Sort NaNs first)
            bool isnan1 = (d1 != d1);
            bool isnan2 = (d2 != d2);
            if (isnan1 && !isnan2) return true;  
            if (!isnan1 && isnan2) return false; 
            
            // 2. Standard numeric comparison
            if (d1 != d2) {
                return d1 < d2;
            }
            
            // 3. TIE-BREAKER: Maintain strict typing in std::map!
            return m_type < other.m_type;
        }

        if (m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) {
            return SqliteValueUtil::less(m_data.pValue, other.m_data.pValue);
        }
        return false;
    }

    // Heterogeneous lookups for SqliteValueView
    /** @brief Heterogeneous equality check against a transient SqliteValueView. */
    bool operator==(const SqliteValueView& other) const;
    /** @brief Heterogeneous inequality check against a transient SqliteValueView. */
    bool operator!=(const SqliteValueView& other) const;
    /** @brief Heterogeneous less-than comparison against a transient SqliteValueView. */
    bool operator<(const SqliteValueView& other) const;
};

// Complete heterogeneous lookups for SqliteValueOwned
inline bool SqliteValueOwned::operator==(const SqliteValueView& other) const {
    int o_type = other.type();
    if (m_type != o_type) return false;
    if (m_type == SQLITE_INTEGER) return m_data.iValue == sqlite3_value_int64(const_cast<sqlite3_value*>(other.get()));
    if (m_type == SQLITE_FLOAT) {
        double d2 = sqlite3_value_double(const_cast<sqlite3_value*>(other.get()));
        if (m_data.dValue != m_data.dValue && d2 != d2) return true; // NaN == NaN
        return m_data.dValue == d2;
    }
    if (m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) {
        return SqliteValueUtil::equal(m_data.pValue, other.get());
    }
    return true; // NULLs
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

    int o_type = other.type();
    int r1 = type_rank(m_type);
    int r2 = type_rank(o_type);
    if (r1 != r2) return r1 < r2;

    if (r1 == 1) { // Numeric
        if (m_type == SQLITE_INTEGER && o_type == SQLITE_INTEGER) {
            return m_data.iValue < sqlite3_value_int64(const_cast<sqlite3_value*>(other.get()));
        }
        double d1 = (m_type == SQLITE_INTEGER) ? (double)m_data.iValue : m_data.dValue;
        double d2 = (o_type == SQLITE_INTEGER) ? (double)sqlite3_value_int64(const_cast<sqlite3_value*>(other.get())) : sqlite3_value_double(const_cast<sqlite3_value*>(other.get()));
        
        // 1. NaN sorting stability (Sort NaNs first)
        bool isnan1 = (d1 != d1);
        bool isnan2 = (d2 != d2);
        if (isnan1 && !isnan2) return true;  
        if (!isnan1 && isnan2) return false; 
        
        // 2. Standard numeric comparison
        if (d1 != d2) {
            return d1 < d2;
        }
        
        // 3. TIE-BREAKER: Maintain strict typing in std::map!
        return m_type < o_type;
    }

    if (m_type == SQLITE_TEXT || m_type == SQLITE_BLOB) {
        return SqliteValueUtil::less(m_data.pValue, other.get());
    }
    return false;
}

// Complete heterogeneous lookups for SqliteValueView

/** @brief Heterogeneous equality check against an owned SqliteValueOwned object. */
inline bool SqliteValueView::operator==(const SqliteValueOwned& other) const {
    return other == *this;
}

/** @brief Heterogeneous inequality check against an owned SqliteValueOwned object. */
inline bool SqliteValueView::operator!=(const SqliteValueOwned& other) const {
    return !(*this == other);
}

/** @brief Heterogeneous less-than comparison against an owned SqliteValueOwned object. */
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

    if (r1 == 1) { // Numeric
        if (t1 == SQLITE_INTEGER && t2 == SQLITE_INTEGER) {
            return sqlite3_value_int64(const_cast<sqlite3_value*>(get())) < other.as_int64();
        }
        double d1 = (t1 == SQLITE_INTEGER) ? (double)sqlite3_value_int64(const_cast<sqlite3_value*>(get())) : sqlite3_value_double(const_cast<sqlite3_value*>(get()));
        double d2 = (t2 == SQLITE_INTEGER) ? (double)other.as_int64() : other.as_double();
        
        // 1. NaN sorting stability (Sort NaNs first)
        bool isnan1 = (d1 != d1);
        bool isnan2 = (d2 != d2);
        if (isnan1 && !isnan2) return true;  
        if (!isnan1 && isnan2) return false; 
        
        // 2. Standard numeric comparison
        if (d1 != d2) {
            return d1 < d2;
        }
        
        // 3. TIE-BREAKER: Maintain strict typing in std::map!
        return t1 < t2;
    }

    if (t1 == SQLITE_TEXT || t1 == SQLITE_BLOB) {
        return SqliteValueUtil::less(get(), other.heap_value());
    }
    return false;
}

// ============================================================================
// HETEROGENEOUS LOOKUPS: VALUES VS STRINGS/BLOBS
// ============================================================================

/**
 * @brief Macro to generate all 6 heterogeneous comparison operators (==, !=, <)
 *        between a variant Value type and a String type in both directions.
 * 
 * Ensures strict typing: returns false immediately if the variant is not SQLITE_TEXT.
 */
#define SQLITE_DEF_VAL_STR_OPS(VAL_TYPE, VAL_PTR, STR_TYPE, STR_DATA, STR_LEN) \
    inline bool operator==(const VAL_TYPE& val, const STR_TYPE& str) { \
        if (val.type() != SQLITE_TEXT) return false; \
        const sqlite3_value* ptr = val.VAL_PTR(); \
        return SqliteStringUtil::equal(reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(ptr))), sqlite3_value_bytes(const_cast<sqlite3_value*>(ptr)), str.STR_DATA(), str.STR_LEN()); \
    } \
    inline bool operator==(const STR_TYPE& str, const VAL_TYPE& val) { return val == str; } \
    inline bool operator!=(const VAL_TYPE& val, const STR_TYPE& str) { return !(val == str); } \
    inline bool operator!=(const STR_TYPE& str, const VAL_TYPE& val) { return !(val == str); } \
    inline bool operator<(const VAL_TYPE& val, const STR_TYPE& str) { \
        int r1 = (val.type() == SQLITE_NULL) ? 0 : (val.type() == SQLITE_INTEGER || val.type() == SQLITE_FLOAT) ? 1 : (val.type() == SQLITE_TEXT) ? 2 : 3; \
        if (r1 != 2) return r1 < 2; \
        const sqlite3_value* ptr = val.VAL_PTR(); \
        return SqliteStringUtil::less(reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(ptr))), sqlite3_value_bytes(const_cast<sqlite3_value*>(ptr)), str.STR_DATA(), str.STR_LEN()); \
    } \
    inline bool operator<(const STR_TYPE& str, const VAL_TYPE& val) { \
        int r2 = (val.type() == SQLITE_NULL) ? 0 : (val.type() == SQLITE_INTEGER || val.type() == SQLITE_FLOAT) ? 1 : (val.type() == SQLITE_TEXT) ? 2 : 3; \
        if (2 != r2) return 2 < r2; \
        const sqlite3_value* ptr = val.VAL_PTR(); \
        return SqliteStringUtil::less(str.STR_DATA(), str.STR_LEN(), reinterpret_cast<const char*>(sqlite3_value_text(const_cast<sqlite3_value*>(ptr))), sqlite3_value_bytes(const_cast<sqlite3_value*>(ptr))); \
    } \
    inline bool operator>(const VAL_TYPE& val, const STR_TYPE& str) { return str < val; } \
    inline bool operator>(const STR_TYPE& str, const VAL_TYPE& val) { return val < str; } \
    inline bool operator<=(const VAL_TYPE& val, const STR_TYPE& str) { return !(str < val); } \
    inline bool operator<=(const STR_TYPE& str, const VAL_TYPE& val) { return !(val < str); } \
    inline bool operator>=(const VAL_TYPE& val, const STR_TYPE& str) { return !(val < str); } \
    inline bool operator>=(const STR_TYPE& str, const VAL_TYPE& val) { return !(str < val); }

SQLITE_DEF_VAL_STR_OPS(SqliteValueOwned, heap_value, SqliteStringView, data, length)
SQLITE_DEF_VAL_STR_OPS(SqliteValueOwned, heap_value, SqliteStringOwned, value, length)
SQLITE_DEF_VAL_STR_OPS(SqliteValueView, get, SqliteStringView, data, length)
SQLITE_DEF_VAL_STR_OPS(SqliteValueView, get, SqliteStringOwned, value, length)

/**
 * @brief Macro to generate all 6 heterogeneous comparison operators (==, !=, <)
 *        between a variant Value type and a Blob type in both directions.
 * 
 * Ensures strict typing: returns false immediately if the variant is not SQLITE_BLOB.
 */
#define SQLITE_DEF_VAL_BLOB_OPS(VAL_TYPE, VAL_PTR, BLOB_TYPE, BLOB_DATA, BLOB_LEN) \
    inline bool operator==(const VAL_TYPE& val, const BLOB_TYPE& blob) { \
        if (val.type() != SQLITE_BLOB) return false; \
        const sqlite3_value* ptr = val.VAL_PTR(); \
        return SqliteBlobUtil::equal(sqlite3_value_blob(const_cast<sqlite3_value*>(ptr)), sqlite3_value_bytes(const_cast<sqlite3_value*>(ptr)), blob.BLOB_DATA(), blob.BLOB_LEN()); \
    } \
    inline bool operator==(const BLOB_TYPE& blob, const VAL_TYPE& val) { return val == blob; } \
    inline bool operator!=(const VAL_TYPE& val, const BLOB_TYPE& blob) { return !(val == blob); } \
    inline bool operator!=(const BLOB_TYPE& blob, const VAL_TYPE& val) { return !(val == blob); } \
    inline bool operator<(const VAL_TYPE& val, const BLOB_TYPE& blob) { \
        int r1 = (val.type() == SQLITE_NULL) ? 0 : (val.type() == SQLITE_INTEGER || val.type() == SQLITE_FLOAT) ? 1 : (val.type() == SQLITE_TEXT) ? 2 : 3; \
        if (r1 != 3) return r1 < 3; \
        const sqlite3_value* ptr = val.VAL_PTR(); \
        return SqliteBlobUtil::less(sqlite3_value_blob(const_cast<sqlite3_value*>(ptr)), sqlite3_value_bytes(const_cast<sqlite3_value*>(ptr)), blob.BLOB_DATA(), blob.BLOB_LEN()); \
    } \
    inline bool operator<(const BLOB_TYPE& blob, const VAL_TYPE& val) { \
        int r2 = (val.type() == SQLITE_NULL) ? 0 : (val.type() == SQLITE_INTEGER || val.type() == SQLITE_FLOAT) ? 1 : (val.type() == SQLITE_TEXT) ? 2 : 3; \
        if (3 != r2) return 3 < r2; \
        const sqlite3_value* ptr = val.VAL_PTR(); \
        return SqliteBlobUtil::less(blob.BLOB_DATA(), blob.BLOB_LEN(), sqlite3_value_blob(const_cast<sqlite3_value*>(ptr)), sqlite3_value_bytes(const_cast<sqlite3_value*>(ptr))); \
    } \
    inline bool operator>(const VAL_TYPE& val, const BLOB_TYPE& blob) { return blob < val; } \
    inline bool operator>(const BLOB_TYPE& blob, const VAL_TYPE& val) { return val < blob; } \
    inline bool operator<=(const VAL_TYPE& val, const BLOB_TYPE& blob) { return !(blob < val); } \
    inline bool operator<=(const BLOB_TYPE& blob, const VAL_TYPE& val) { return !(val < blob); } \
    inline bool operator>=(const VAL_TYPE& val, const BLOB_TYPE& blob) { return !(val < blob); } \
    inline bool operator>=(const BLOB_TYPE& blob, const VAL_TYPE& val) { return !(blob < val); }

SQLITE_DEF_VAL_BLOB_OPS(SqliteValueOwned, heap_value, SqliteBlobView, data, size)
SQLITE_DEF_VAL_BLOB_OPS(SqliteValueOwned, heap_value, SqliteBlobOwned, data, size)
SQLITE_DEF_VAL_BLOB_OPS(SqliteValueView, get, SqliteBlobView, data, size)
SQLITE_DEF_VAL_BLOB_OPS(SqliteValueView, get, SqliteBlobOwned, data, size)

// ============================================================================
// HETEROGENEOUS LOOKUPS: VALUES VS PRIMITIVES
// ============================================================================

/**
 * @brief Macro to generate 24 heterogeneous comparison operators (==, !=, <, >)
 *        between a variant Value type and primitive C++ types (int, int64, double) in both directions.
 * 
 * Ensures strict typing: Float(5.0) != Int(5).
 */
#define SQLITE_DEF_VAL_PRIM_OPS(VAL_TYPE) \
    /* Exact type and value match for INTEGER */ \
    inline bool operator==(const VAL_TYPE& val, sqlite3_int64 num) { return val.type() == SQLITE_INTEGER && val.as_int64() == num; } \
    inline bool operator==(sqlite3_int64 num, const VAL_TYPE& val) { return val == num; } \
    inline bool operator!=(const VAL_TYPE& val, sqlite3_int64 num) { return !(val == num); } \
    inline bool operator!=(sqlite3_int64 num, const VAL_TYPE& val) { return !(val == num); } \
    /* Less-than comparison honoring type-ranks: NULL(0) < NUMERIC(1) < TEXT(2) < BLOB(3) */ \
    inline bool operator<(const VAL_TYPE& val, sqlite3_int64 num) { \
        int t = val.type(); \
        int r1 = (t == SQLITE_NULL) ? 0 : (t == SQLITE_INTEGER || t == SQLITE_FLOAT) ? 1 : (t == SQLITE_TEXT) ? 2 : 3; \
        if (r1 != 1) return r1 < 1; \
        if (t == SQLITE_INTEGER) return val.as_int64() < num; \
        double d1 = val.as_double(), d2 = static_cast<double>(num); \
        if (d1 != d1) return true; /* NaN sorts before numbers */ \
        if (d1 != d2) return d1 < d2; \
        return SQLITE_FLOAT < SQLITE_INTEGER; /* Tie breaker: Int sorts before Float */ \
    } \
    inline bool operator<(sqlite3_int64 num, const VAL_TYPE& val) { \
        int t = val.type(); \
        int r2 = (t == SQLITE_NULL) ? 0 : (t == SQLITE_INTEGER || t == SQLITE_FLOAT) ? 1 : (t == SQLITE_TEXT) ? 2 : 3; \
        if (1 != r2) return 1 < r2; \
        if (t == SQLITE_INTEGER) return num < val.as_int64(); \
        double d1 = static_cast<double>(num), d2 = val.as_double(); \
        if (d2 != d2) return false; /* NaN is less than num, so num is NOT less than NaN */ \
        if (d1 != d2) return d1 < d2; \
        return SQLITE_INTEGER < SQLITE_FLOAT; /* Tie breaker: Int sorts before Float */ \
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
    /* Exact type and value match for FLOAT */ \
    inline bool operator==(const VAL_TYPE& val, double num) { \
        if (val.type() != SQLITE_FLOAT) return false; \
        double d = val.as_double(); \
        if (d != d && num != num) return true; /* NaN == NaN */ \
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
        if (d1 != d1 && num != num) return false; /* NaN is not less than NaN */ \
        if (d1 != d1) return true; /* NaN sorts before numbers */ \
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

/**
 * @brief A transparent Hash functor for std::unordered_map.
 * Enables zero-allocation heterogeneous lookups across Strings, Blobs, and Primitives.
 */
struct SqliteValueHash {
    using is_transparent = void;

    // Polymorphic Values
    inline size_t operator()(const SqliteValueOwned& val) const { return static_cast<size_t>(val.hash()); }
    inline size_t operator()(const SqliteValueView& val) const { return static_cast<size_t>(val.hash()); }

    // Strings
    inline size_t operator()(const SqliteStringOwned& str) const { return static_cast<size_t>(str.hash()); }
    inline size_t operator()(const SqliteStringView& str) const { return static_cast<size_t>(str.hash()); }
    inline size_t operator()(const char* str) const { return static_cast<size_t>(SqliteStringUtil::hash(str, SqliteStringUtil::sqlite_strlen(str))); }

    // Blobs
    inline size_t operator()(const SqliteBlobOwned& blob) const { return static_cast<size_t>(blob.hash()); }
    inline size_t operator()(const SqliteBlobView& blob) const { return static_cast<size_t>(blob.hash()); }

    // Primitives
    inline size_t operator()(sqlite3_int64 i) const { return static_cast<size_t>(SqliteHashUtil::hash(&i, sizeof(i))); }
    inline size_t operator()(int i) const { sqlite3_int64 val = i; return static_cast<size_t>(SqliteHashUtil::hash(&val, sizeof(val))); }
    inline size_t operator()(double d) const { return static_cast<size_t>(SqliteHashUtil::hash(&d, sizeof(d))); }
};

/**
 * @brief A transparent Equality functor for std::unordered_map.
 * Enables zero-allocation heterogeneous lookups across Strings, Blobs, and Primitives.
 */
struct SqliteValueEqual {
    using is_transparent = void;

    // The core polymorphic equality relies on the heavily overloaded operator== macros.
    template <typename T, typename U>
    inline bool operator()(const T& a, const U& b) const {
        return a == b;
    }
};

#endif // SQLITE3_VALUE_HPP
