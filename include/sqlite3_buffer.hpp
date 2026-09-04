#ifndef SQLITE3_BUFFER_HPP
#define SQLITE3_BUFFER_HPP

#include "sqlite3ext.h"
#include <string.h>
#include <utility>
#include "sqlite3_value.hpp"

/**
 * @brief A non-owning view over a raw memory buffer.
 * 
 * Replaces std::span<const uint8_t> or std::string_view in a -nostdlib++ environment.
 * Costs zero heap allocations.
 */
class SqliteBufferSlice {
    const void* m_data;
    sqlite3_int64 m_size;

public:
    inline SqliteBufferSlice() : m_data(nullptr), m_size(0) {}
    inline SqliteBufferSlice(const void* data, sqlite3_int64 size) : m_data(data), m_size(size) {}

    inline const void* data() const { return m_data; }
    inline sqlite3_int64 bytes() const { return m_size; }
    
    inline unsigned long long hash() const {
        return SqliteHashUtil::hash(m_data, static_cast<int>(m_size));
    }
    
    inline bool operator==(const SqliteBufferSlice& other) const {
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), other.m_data, static_cast<int>(other.m_size));
    }
    inline bool operator!=(const SqliteBufferSlice& other) const { return !(*this == other); }
    
    inline bool operator<(const SqliteBufferSlice& other) const {
        int min_len = (m_size < other.m_size) ? static_cast<int>(m_size) : static_cast<int>(other.m_size);
        int cmp = 0;
        if (min_len > 0) cmp = memcmp(m_data, other.m_data, min_len);
        return (cmp != 0) ? (cmp < 0) : (m_size < other.m_size);
    }
    inline bool operator>(const SqliteBufferSlice& other) const { return other < *this; }
    inline bool operator<=(const SqliteBufferSlice& other) const { return !(*this > other); }
    inline bool operator>=(const SqliteBufferSlice& other) const { return !(*this < other); }
    
    inline bool operator==(const SqliteValueView& other) const {
        if (other.type() != SQLITE_BLOB && other.type() != SQLITE_TEXT) return false;
        sqlite3_value* v = const_cast<sqlite3_value*>(other.get());
        if (!v) return false;
        const void* val_data = (other.type() == SQLITE_BLOB) ? sqlite3_value_blob(v) : sqlite3_value_text(v);
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), val_data, sqlite3_value_bytes(v));
    }
    inline bool operator!=(const SqliteValueView& other) const { return !(*this == other); }
    
    // Compare against C-strings
    inline bool operator==(const char* str) const {
        if (!str) return m_size == 0 && m_data == nullptr;
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), str, static_cast<int>(strlen(str)));
    }
    inline bool operator!=(const char* str) const { return !(*this == str); }
    
    // Compare against nullptr
    inline bool operator==(decltype(nullptr)) const { return m_size == 0 && m_data == nullptr; }
    inline bool operator!=(decltype(nullptr)) const { return !(*this == nullptr); }
};

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
    
    /** @brief Implicitly converts the buffer to a non-owning slice. */
    inline operator SqliteBufferSlice() const {
        return SqliteBufferSlice(m_data, m_size);
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

    /** @brief Attempts to reserve buffer capacity, returning SqliteStatus. */
    inline SqliteStatus try_reserve(sqlite3_int64 new_capacity) {
        if (new_capacity <= m_capacity) return SqliteStatus::ok();
        void* new_data = sqlite3_realloc64(m_data, new_capacity);
        if (!new_data) {
            return SqliteStatus::nomem("Failed to reallocate memory in SqliteBuffer::try_reserve");
        }
        m_data = new_data;
        m_capacity = new_capacity;
        return SqliteStatus::ok();
    }

    /** @brief Attempts to append raw bytes to the buffer, returning SqliteStatus. */
    inline SqliteStatus try_append(const void* data, sqlite3_int64 bytes) {
        if (!data || bytes <= 0) return SqliteStatus::ok();
        if (!ensure_capacity(bytes)) {
            return SqliteStatus::nomem("Failed to grow buffer in SqliteBuffer::try_append");
        }
        char* dest = static_cast<char*>(m_data) + m_size;
        memcpy(dest, data, bytes);
        m_size += bytes;
        return SqliteStatus::ok();
    }

    /** @brief Returns a pointer to the raw underlying byte array. */
    inline void* data() const { return m_data; }
    
    /** @brief Returns the number of bytes currently stored in the buffer. */
    inline sqlite3_int64 bytes() const { return m_size; }
    
    /** @brief Returns the total allocated capacity of the buffer. */
    inline sqlite3_int64 capacity() const { return m_capacity; }
    
    /** @brief Checks if the buffer holds a valid state (or is cleanly empty). */
    inline bool is_valid() const {
        return m_size == 0 || m_data != nullptr;
    }

    /** @brief Explicit boolean conversion checking validity. */
    inline explicit operator bool() const {
        return is_valid();
    }

    /** @brief Resets the active size to 0 without freeing the allocated capacity. */
    inline void clear() { m_size = 0; }
    
    /** @brief Truncates the active size of the buffer. Cannot expand the buffer. */
    inline void truncate(sqlite3_int64 new_size) {
        if (new_size >= 0 && new_size < m_size) {
            m_size = new_size;
        }
    }
    
    /** 
     * @brief Expands the buffer size without initializing the new memory.
     * Useful for direct streaming operations (e.g. from a SqliteBlobStream or file handle).
     * 
     * @param additional_bytes The number of bytes to add to the active size.
     * @return A pointer to the beginning of the newly allocated uninitialized region, or nullptr on OOM.
     */
    inline void* append_uninitialized(sqlite3_int64 additional_bytes) {
        if (additional_bytes <= 0) return nullptr;
        if (!ensure_capacity(additional_bytes)) return nullptr;
        
        char* dest = static_cast<char*>(m_data) + m_size;
        m_size += additional_bytes;
        return dest;
    }

    /**
     * @brief Attempts to expand the buffer without initializing the new memory, returning SqliteResult.
     */
    inline SqliteResult<void*> try_append_uninitialized(sqlite3_int64 additional_bytes) {
        if (additional_bytes <= 0) return SqliteResult<void*>::ok(nullptr);
        if (!ensure_capacity(additional_bytes)) {
            return SqliteResult<void*>::nomem("Failed to grow buffer in SqliteBuffer::try_append_uninitialized");
        }
        char* dest = static_cast<char*>(m_data) + m_size;
        m_size += additional_bytes;
        return SqliteResult<void*>::ok(dest);
    }
    
    /**
     * @brief Extracts a non-owning slice of the buffer.
     * 
     * @param offset The zero-based index to start the slice.
     * @param length The number of bytes to extract.
     * @return A SqliteBufferSlice pointing to the requested region.
     */
    inline SqliteBufferSlice bufferSlice(sqlite3_int64 offset, sqlite3_int64 length) const {
        if (offset < 0 || offset >= m_size || length <= 0) return SqliteBufferSlice();
        
        sqlite3_int64 actual_length = (offset + length > m_size) ? (m_size - offset) : length;
        return SqliteBufferSlice(static_cast<const char*>(m_data) + offset, actual_length);
    }
    
    /** @brief Computes a 64-bit MurmurHash2 of the buffer. */
    inline unsigned long long hash() const {
        return SqliteHashUtil::hash(m_data, static_cast<int>(m_size));
    }
    
    /** @brief Checks if the buffer is identical to another buffer. */
    inline bool operator==(const SqliteBuffer& other) const {
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), other.m_data, static_cast<int>(other.m_size));
    }
    
    /** @brief Checks if the buffer is identical to a slice. */
    inline bool operator==(const SqliteBufferSlice& other) const {
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), other.data(), static_cast<int>(other.bytes()));
    }
    
    inline bool operator!=(const SqliteBufferSlice& other) const { return !(*this == other); }
    
    inline bool operator<(const SqliteBufferSlice& other) const {
        int min_len = (m_size < other.bytes()) ? static_cast<int>(m_size) : static_cast<int>(other.bytes());
        int cmp = 0;
        if (min_len > 0) cmp = memcmp(m_data, other.data(), min_len);
        return (cmp != 0) ? (cmp < 0) : (m_size < other.bytes());
    }
    inline bool operator>(const SqliteBufferSlice& other) const { return other < *this; }
    inline bool operator<=(const SqliteBufferSlice& other) const { return !(*this > other); }
    inline bool operator>=(const SqliteBufferSlice& other) const { return !(*this < other); }
    
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
    using SqliteBuffer::try_reserve;
    using SqliteBuffer::clear;
    using SqliteBuffer::truncate;
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
        return append(str, len);
    }

    /** @brief Appends a buffer with explicit length. */
    inline bool append(const char* str, sqlite3_int64 len) {
        if (!str || len <= 0) return true;
        if (!ensure_capacity(len + 1)) return false;
        char* dest = static_cast<char*>(m_data) + m_size;
        memcpy(dest, str, len);
        m_size += len;
        static_cast<char*>(m_data)[m_size] = '\0';
        return true;
    }

    /** @brief Attempts to append a null-terminated C-string, returning SqliteStatus. */
    inline SqliteStatus try_append(const char* str) {
        if (!str) return SqliteStatus::ok();
        sqlite3_int64 len = strlen(str);
        return try_append(str, len);
    }

    /** @brief Attempts to append a buffer with explicit length, returning SqliteStatus. */
    inline SqliteStatus try_append(const char* str, sqlite3_int64 len) {
        if (!str || len <= 0) return SqliteStatus::ok();
        if (!ensure_capacity(len + 1)) {
            return SqliteStatus::nomem("Failed to grow string buffer in SqliteString::try_append");
        }
        char* dest = static_cast<char*>(m_data) + m_size;
        memcpy(dest, str, len);
        m_size += len;
        static_cast<char*>(m_data)[m_size] = '\0';
        return SqliteStatus::ok();
    }

    /** @brief Attempts to construct a dynamic string, returning SqliteResult. */
    static inline SqliteResult<SqliteString> try_create(const char* str = nullptr) {
        SqliteString s;
        if (str) {
            SqliteStatus stat = s.try_append(str);
            if (stat.is_err()) {
                return SqliteResult<SqliteString>::err(stat.err_code(), stat.err_message());
            }
        }
        return SqliteResult<SqliteString>::ok(sqlite_move(s));
    }

    /** @brief Returns a pointer to the null-terminated C-string. */
    inline const char* c_str() const { 
        return m_data ? static_cast<const char*>(m_data) : ""; 
    }
    
    /** @brief Returns the length of the string, excluding the null terminator. */
    inline sqlite3_int64 length() const { return m_size; }
    
    /** @brief Checks if the string holds a valid state (or is cleanly empty). */
    inline bool is_valid() const {
        return m_size == 0 || m_data != nullptr;
    }

    /** @brief Explicit boolean conversion checking validity. */
    inline explicit operator bool() const {
        return is_valid();
    }

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
