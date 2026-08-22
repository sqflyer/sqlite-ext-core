#ifndef SQLITE3_BUFFER_HPP
#define SQLITE3_BUFFER_HPP

#include "sqlite3ext.h"
#include <string.h>
#include <utility>
#include "sqlite3_value.hpp"

/**
 * @brief A dynamic, auto-expanding byte buffer built on SQLite's memory allocator.
 * 
 * Replaces std::vector<uint8_t> in a -nostdlib++ environment.
 */
class SqliteBuffer {
protected:
    void* m_data;
    sqlite3_int64 m_size;
    sqlite3_int64 m_capacity;

    inline bool ensure_capacity(sqlite3_int64 additional_bytes) {
        sqlite3_int64 required = m_size + additional_bytes;
        if (required > m_capacity) {
            sqlite3_int64 new_capacity = m_capacity == 0 ? 32 : m_capacity * 2;
            if (new_capacity < required) {
                new_capacity = required;
            }
            
            void* new_data = sqlite3_realloc64(m_data, new_capacity);
            if (!new_data) {
                return false; // OOM
            }
            m_data = new_data;
            m_capacity = new_capacity;
        }
        return true;
    }

public:
    /** @brief Constructs an empty buffer with zero capacity. */
    inline SqliteBuffer() : m_data(nullptr), m_size(0), m_capacity(0) {}
    
    /** @brief Destroys the buffer and frees any allocated memory via sqlite3_free. */
    inline ~SqliteBuffer() {
        if (m_data) {
            sqlite3_free(m_data);
        }
    }

    // Non-copyable to prevent accidental massive allocations
    SqliteBuffer(const SqliteBuffer&) = delete;
    SqliteBuffer& operator=(const SqliteBuffer&) = delete;

    // Moveable
    /** @brief Move constructor: transfers ownership of the buffer without copying. */
    inline SqliteBuffer(SqliteBuffer&& other) noexcept 
        : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity) 
    {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    /** @brief Move assignment: frees current memory and transfers ownership of the other buffer. */
    inline SqliteBuffer& operator=(SqliteBuffer&& other) noexcept {
        if (this != &other) {
            if (m_data) sqlite3_free(m_data);
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    /** @brief Appends raw bytes to the buffer, auto-expanding if necessary. */
    inline bool append(const void* data, sqlite3_int64 bytes) {
        if (!data || bytes <= 0) return true;
        if (!ensure_capacity(bytes)) return false;
        
        char* dest = static_cast<char*>(m_data) + m_size;
        memcpy(dest, data, bytes);
        m_size += bytes;
        return true;
    }

    /** @brief Returns a pointer to the raw underlying byte array. */
    inline void* data() const { return m_data; }
    
    /** @brief Returns the number of bytes currently stored in the buffer. */
    inline sqlite3_int64 bytes() const { return m_size; }
    
    /** @brief Returns the total allocated capacity of the buffer. */
    inline sqlite3_int64 capacity() const { return m_capacity; }
    
    /** @brief Resets the active size to 0 without freeing the allocated capacity. */
    inline void clear() { m_size = 0; }
    
    /** @brief Computes a 64-bit FNV-1a hash of the buffer. */
    inline unsigned long long hash() const {
        return SqliteHashUtil::hash(m_data, static_cast<int>(m_size));
    }
    
    /** @brief Checks if the buffer is identical to another buffer. */
    inline bool operator==(const SqliteBuffer& other) const {
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), other.m_data, static_cast<int>(other.m_size));
    }
    
    /** @brief Checks if the buffer differs from another buffer. */
    inline bool operator!=(const SqliteBuffer& other) const {
        return !(*this == other);
    }

    /** @brief Performs a lexicographical less-than comparison against another buffer. */
    inline bool operator<(const SqliteBuffer& other) const {
        int min_len = (m_size < other.m_size) ? static_cast<int>(m_size) : static_cast<int>(other.m_size);
        int cmp = 0;
        if (min_len > 0) {
            cmp = memcmp(m_data, other.m_data, min_len);
        }
        return (cmp != 0) ? (cmp < 0) : (m_size < other.m_size);
    }
    
    /** @brief Performs a lexicographical greater-than comparison against another buffer. */
    inline bool operator>(const SqliteBuffer& other) const { return other < *this; }
    
    /** @brief Performs a lexicographical less-than-or-equal comparison against another buffer. */
    inline bool operator<=(const SqliteBuffer& other) const { return !(*this > other); }
    
    /** @brief Performs a lexicographical greater-than-or-equal comparison against another buffer. */
    inline bool operator>=(const SqliteBuffer& other) const { return !(*this < other); }

    /** @brief Checks if the buffer is identical to the blob contents of a SqliteValue. */
    inline bool operator==(const SqliteValueView& other) const {
        if (other.type() != SQLITE_BLOB) return false;
        
        sqlite3_value* v = const_cast<sqlite3_value*>(other.get());
        if (!v) return false; // Shouldn't happen if type is BLOB, but safe
        
        const void* blob_data = sqlite3_value_blob(v);
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), blob_data, sqlite3_value_bytes(v));
    }
    
    /** @brief Checks if the buffer differs from the blob contents of a SqliteValue. */
    inline bool operator!=(const SqliteValueView& other) const {
        return !(*this == other);
    }
};

/**
 * @brief A dynamic string built on SQLite's memory allocator.
 * 
 * Replaces std::string in a -nostdlib++ environment.
 * Guarantees null-termination.
 */
class SqliteString : protected SqliteBuffer {
public:
    using SqliteBuffer::data;
    using SqliteBuffer::bytes;
    using SqliteBuffer::capacity;
    using SqliteBuffer::hash;

    /** @brief Constructs an empty string. */
    inline SqliteString() : SqliteBuffer() {}
    
    /** @brief Constructs a string by copying the provided null-terminated C-string. */
    inline SqliteString(const char* str) : SqliteBuffer() {
        if (str) append(str);
    }
    
    /** @brief Move constructor: transfers ownership of the string without copying. */
    inline SqliteString(SqliteString&& other) noexcept : SqliteBuffer(std::move(other)) {}
    
    /** @brief Move assignment: frees current string and transfers ownership of the other string. */
    inline SqliteString& operator=(SqliteString&& other) noexcept {
        SqliteBuffer::operator=(std::move(other));
        return *this;
    }
    
    // Non-copyable
    SqliteString(const SqliteString&) = delete;
    SqliteString& operator=(const SqliteString&) = delete;

    /** @brief Appends a null-terminated C-string. */
    inline bool append(const char* str) {
        if (!str) return true;
        sqlite3_int64 len = strlen(str);
        
        // Ensure capacity for the new string PLUS the null terminator
        if (!ensure_capacity(len + 1)) return false;
        
        char* dest = static_cast<char*>(m_data) + m_size;
        memcpy(dest, str, len);
        m_size += len;
        
        // Null terminate (not included in m_size)
        static_cast<char*>(m_data)[m_size] = '\0';
        return true;
    }

    /** @brief Returns a pointer to the null-terminated C-string. */
    inline const char* c_str() const { 
        return m_data ? static_cast<const char*>(m_data) : ""; 
    }
    
    /** @brief Returns the length of the string, excluding the null terminator. */
    inline sqlite3_int64 length() const { return m_size; }
    
    /** @brief Checks if the string is identical to the provided C-string. */
    inline bool operator==(const char* other) const {
        if (!other) return m_size == 0;
        return strcmp(c_str(), other) == 0;
    }
    
    /** @brief Checks if the string differs from the provided C-string. */
    inline bool operator!=(const char* other) const {
        return !(*this == other);
    }
    
    /** @brief Lexicographical less-than comparison against a C-string. */
    inline bool operator<(const char* other) const {
        const char* rhs = other ? other : "";
        int rhs_len = SqliteStringUtil::sqlite_strlen(rhs);
        int min_len = (m_size < rhs_len) ? static_cast<int>(m_size) : rhs_len;
        
        int cmp = 0;
        if (min_len > 0) {
            cmp = memcmp(c_str(), rhs, min_len);
        }
        return (cmp != 0) ? (cmp < 0) : (m_size < rhs_len);
    }
    
    /** @brief Lexicographical greater-than comparison against a C-string. */
    inline bool operator>(const char* other) const {
        const char* rhs = other ? other : "";
        int rhs_len = SqliteStringUtil::sqlite_strlen(rhs);
        int min_len = (rhs_len < m_size) ? rhs_len : static_cast<int>(m_size);
        
        int cmp = 0;
        if (min_len > 0) {
            cmp = memcmp(rhs, c_str(), min_len);
        }
        return (cmp != 0) ? (cmp < 0) : (rhs_len < m_size);
    }
    
    /** @brief Lexicographical less-than-or-equal comparison against a C-string. */
    inline bool operator<=(const char* other) const { return !(*this > other); }
    
    /** @brief Lexicographical greater-than-or-equal comparison against a C-string. */
    inline bool operator>=(const char* other) const { return !(*this < other); }
    
    /** @brief Checks if the string is identical to the contents of a SqliteValue. */
    inline bool operator==(const SqliteValueView& other) const {
        if (other.type() != SQLITE_TEXT) return false;
        
        sqlite3_value* v = const_cast<sqlite3_value*>(other.get());
        if (!v) return false; // Shouldn't happen if type is TEXT, but safe
        
        const char* text_data = reinterpret_cast<const char*>(sqlite3_value_text(v));
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), text_data, sqlite3_value_bytes(v));
    }
    
    /** @brief Checks if the string differs from the contents of a SqliteValue. */
    inline bool operator!=(const SqliteValueView& other) const {
        return !(*this == other);
    }
    
    /** @brief Clears the string, resetting its length to 0 and re-establishing the null terminator. */
    inline void clear() {
        SqliteBuffer::clear();
        if (m_data && m_capacity > 0) {
            static_cast<char*>(m_data)[0] = '\0';
        }
    }
};

// ============================================================================
// Symmetric Global Equality Operators (LHS == RHS)
// ============================================================================

inline bool operator==(const SqliteValueView& lhs, const SqliteBuffer& rhs) {
    return rhs == lhs;
}

inline bool operator!=(const SqliteValueView& lhs, const SqliteBuffer& rhs) {
    return rhs != lhs;
}

inline bool operator==(const SqliteValueView& lhs, const SqliteString& rhs) {
    return rhs == lhs;
}

inline bool operator!=(const SqliteValueView& lhs, const SqliteString& rhs) {
    return rhs != lhs;
}

inline bool operator==(const char* lhs, const SqliteString& rhs) {
    return rhs == lhs;
}

inline bool operator!=(const char* lhs, const SqliteString& rhs) {
    return rhs != lhs;
}

#endif // SQLITE3_BUFFER_HPP
