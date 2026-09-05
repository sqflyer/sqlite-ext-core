#ifndef SQLITE3_BUFFER_HPP
#define SQLITE3_BUFFER_HPP

#include "sqlite3ext.h"
#include <string.h>
#include "sqlite3_allocator.hpp"
#include "sqlite3_hash.hpp"


class SqliteStringView;

/**
 * @brief A lightweight, non-owning view over a contiguous raw memory buffer.
 * 
 * Replaces `std::span<const uint8_t>` or `std::string_view` in a `-nostdlib++` environment.
 * Incurs zero heap allocations and trivial copy overhead (pointer + 64-bit size, 16 bytes).
 *
 * Characteristics:
 * - Non-owning: The caller guarantees that the referenced memory buffer remains valid
 *   for the lifetime of the slice.
 * - Universal Comparison: Supports lexicographical comparison and equality against other slices,
 *   raw C-strings (`const char*`), SQLite value views (`SqliteValueView`), and `nullptr`.
 * - Hashing: Computes 64-bit MurmurHash2 for zero-allocation hashtable keys and lookups.
 */
class SqliteBufferSlice {
    const void*   m_data;
    sqlite3_int64 m_size;

public:
    /** @brief Constructs an empty slice pointing to nullptr with 0 bytes. */
    inline SqliteBufferSlice() noexcept : m_data(nullptr), m_size(0) {}

    /**
     * @brief Constructs a non-owning slice referencing an existing memory region.
     * @param data Pointer to the memory buffer.
     * @param size Length of the buffer in bytes.
     */
    inline SqliteBufferSlice(const void* data, sqlite3_int64 size) noexcept
        : m_data(data), m_size(size) {}

    /** @brief Returns a const pointer to the referenced data. */
    inline const void* data() const noexcept { return m_data; }

    /** @brief Returns the size of the slice in bytes. */
    inline sqlite3_int64 bytes() const noexcept { return m_size; }

    /** @brief STL-compatible size accessor. */
    inline sqlite3_int64 size() const noexcept { return m_size; }

    /** @brief Checks if the slice references 0 bytes. */
    inline bool empty() const noexcept { return m_size == 0; }

    /**
     * @brief Computes a 64-bit MurmurHash2 over the referenced memory slice.
     * @return 64-bit hash value.
     */
    inline unsigned long long hash() const noexcept {
        return SqliteHashUtil::hash(m_data, static_cast<int>(m_size));
    }

    /**
     * @brief Sets this slice as the BLOB return result of a SQLite UDF context.
     * @param ctx Target SQLite UDF execution context.
     * @param dtor Memory disposal strategy callback (defaults to SQLITE_TRANSIENT).
     * @param subtype Optional SQLite 3.9+ application subtype.
     */
    inline void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        sqlite3_result_blob(ctx, m_data, static_cast<int>(m_size), dtor);
        if (subtype != SQLITE_SUBTYPE_NONE) {
            sqlite3_result_subtype(ctx, subtype);
        }
    }

    template <typename TContext>
    inline void result(TContext& ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        result(ctx.get(), dtor, subtype);
    }

    /**
     * @brief Binds this slice as a BLOB parameter to a prepared statement at the given 1-based index.
     * @param stmt Prepared statement handle.
     * @param col 1-based parameter index.
     * @param dtor Memory disposal strategy callback (defaults to SQLITE_TRANSIENT).
     * @return SQLITE_OK on success, or SQLite error code.
     */
    inline int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_blob(stmt, col, m_data, static_cast<int>(m_size), dtor);
    }

    template <typename TStatement>
    inline int bind(TStatement& stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return bind(stmt.get(), col, dtor);
    }

    // ========================================================================
    // Comparison against other SqliteBufferSlice instances
    // ========================================================================

    /** @brief Equality comparison using fast byte-by-byte memory comparison. */
    inline bool operator==(const SqliteBufferSlice& other) const noexcept {
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), other.m_data, static_cast<int>(other.m_size));
    }
    inline bool operator!=(const SqliteBufferSlice& other) const noexcept { return !(*this == other); }

    /** @brief Lexicographical less-than comparison. */
    inline bool operator<(const SqliteBufferSlice& other) const noexcept {
        int min_len = (m_size < other.m_size) ? static_cast<int>(m_size) : static_cast<int>(other.m_size);
        int cmp = 0;
        if (min_len > 0) cmp = memcmp(m_data, other.m_data, min_len);
        return (cmp != 0) ? (cmp < 0) : (m_size < other.m_size);
    }
    inline bool operator>(const SqliteBufferSlice& other) const noexcept { return other < *this; }
    inline bool operator<=(const SqliteBufferSlice& other) const noexcept { return !(*this > other); }
    inline bool operator>=(const SqliteBufferSlice& other) const noexcept { return !(*this < other); }


    // ========================================================================
    // Comparison against null-terminated C-strings
    // ========================================================================

    /** @brief Compares byte slice against a null-terminated C-string. */
    inline bool operator==(const char* str) const noexcept {
        if (!str) return m_size == 0 && m_data == nullptr;
        return SqliteMemoryUtil::memcmp_equal(m_data, static_cast<int>(m_size), str, static_cast<int>(strlen(str)));
    }
    inline bool operator!=(const char* str) const noexcept { return !(*this == str); }

    // ========================================================================
    // Comparison against nullptr
    // ========================================================================

    /** @brief Checks if the slice is empty and references nullptr. */
    inline bool operator==(decltype(nullptr)) const noexcept { return m_size == 0 && m_data == nullptr; }
    inline bool operator!=(decltype(nullptr)) const noexcept { return !(*this == nullptr); }
};

/**
 * @brief Dynamic, auto-expanding byte buffer with Small Buffer Optimization (SBO).
 * 
 * Replaces std::vector<uint8_t> in a -nostdlib++ environment. Exactly 24 bytes in size.
 *
 * Architecture & Memory Model:
 * - Employs a 24-byte union supporting two distinct operational states:
 *   1. SBO (Inline / Stack) Mode:
 *      - Stores up to 22 bytes inline within the object itself (zero heap allocation).
 *      - Byte 0: Discriminator `SboTag` (bit 0 = 1 for SBO, bits 1..7 = active byte length).
 *      - Bytes 1..22: Raw payload bytes.
 *      - Byte 23: Guaranteed null terminator ('\0').
 *   2. Heap Mode:
 *      - Backed by dynamic SQLite allocator (`sqlite3_malloc64` / `sqlite3_realloc64`).
 *      - Byte 0..7: `Capacity` struct (bit 0 = 0 for Heap, bits 1..63 = buffer capacity).
 *      - Byte 8..15: Active byte length (`m_size`, 64-bit integer).
 *      - Byte 16..23: Pointer to heap memory (`void* m_data`).
 *
 * Invariants & Guarantees:
 * - Size: `sizeof(SqliteBuffer) == 24` bytes on 64-bit architectures.
 * - Discriminator: Bit 0 of Byte 0 distinguishes SBO (`is_sbo == 1`) from Heap (`is_sbo == 0`).
 * - Null-Termination: The byte immediately following active data (`data()[bytes()]`) is
 *   GUARANTEED to be `'\0'` across all states (empty, SBO, heap, clear, truncate).
 *   This enables safe zero-cost C-string projection (`c_str()`) without re-allocation.
 * - Exception Safety & OOM: Operates under `-nostdlib++` with no C++ exceptions.
 *   Provides `try_*` methods returning `SqliteStatus` or `SqliteResult` for robust OOM handling.
 * - Move Semantics: Non-copyable; transfers ownership strictly via `sqlite_move`.
 */
class SqliteBuffer {
public:
    /**
     * @brief Maximum number of inline payload bytes stored without heap allocation.
     * With 1 byte for SboTag and 1 byte reserved for guaranteed '\0', 22 bytes remain for data.
     */
    static const size_t SBO_CAPACITY = 22;

    /**
     * @brief 1-byte discriminator tag for Small Buffer Optimization (SBO).
     *
     * Occupies the first byte (byte 0) of the 24-byte layout.
     * On little-endian architectures:
     *   - Bit 0 (`is_sbo`): Set to 1 when inline on stack, 0 when on heap.
     *   - Bits 1..7 (`length`): Active byte length (0 to 22).
     */
    struct SboTag {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        uint8_t length : 7;
        uint8_t is_sbo : 1;
#else
        uint8_t is_sbo : 1;
        uint8_t length : 7;
#endif

        /** @brief Sets SBO length, explicitly setting discriminator bit 0 to 1. */
        inline void set(sqlite3_int64 len) noexcept {
            is_sbo = 1;
            length = static_cast<uint8_t>(len & 0x7F);
        }

        /** @brief Sets the SBO active byte length. */
        inline void set_length(sqlite3_int64 len) noexcept {
            set(len);
        }

        /** @brief Returns the 7-bit active byte length. */
        inline sqlite3_int64 get() const noexcept {
            return static_cast<sqlite3_int64>(length);
        }

        /** @brief Returns the 7-bit active byte length as uint8_t. */
        inline uint8_t get_length() const noexcept {
            return length;
        }

        /** @brief Resets the tag to empty SBO state (is_sbo = 1, length = 0). */
        inline void clear() noexcept {
            is_sbo = 1;
            length = 0;
        }
    };
    static_assert(sizeof(SboTag) == 1, "SboTag must be exactly 1 byte!");

    /**
     * @brief 8-byte heap capacity layout matching SboTag at Byte 0 Bit 0.
     *
     * When on heap, bit 0 (`is_sbo`) is set to 0.
     * Bits 1..63 (`value`) store the allocated heap capacity (up to 2^63 - 1).
     * Because byte 0 bit 0 is the discriminator across both union branches,
     * checking `m_sbo.tag.is_sbo` determines whether the buffer is in SBO or heap mode.
     */
    struct Capacity {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        uint64_t value  : 63;
        uint64_t is_sbo : 1;
#else
        uint64_t is_sbo : 1;
        uint64_t value  : 63;
#endif

        /** @brief Sets heap capacity, explicitly clearing discriminator bit 0. */
        inline void set(sqlite3_int64 cap) noexcept {
            is_sbo = 0;
            value = static_cast<uint64_t>(cap);
        }

        /** @brief Returns the 63-bit capacity value. */
        inline sqlite3_int64 get() const noexcept {
            return static_cast<sqlite3_int64>(value);
        }
    };
    static_assert(sizeof(Capacity) == 8, "Capacity must be exactly 8 bytes!");

protected:
    union {
        /** @brief Heap state: 24 bytes total (Capacity: 8, Size: 8, Pointer: 8). */
        struct {
            Capacity      m_capacity;
            sqlite3_int64 m_size;
            void*         m_data;
        } m_heap;

        /** @brief SBO state: 24 bytes total (SboTag: 1, Payload + Null-term: 23). */
        struct {
            SboTag  tag;
            uint8_t m_sbo[23];
        } m_sbo;

        /** @brief Raw 24-byte representation for bulk block transfers and static sizing. */
        uint8_t raw_bytes[24];
    };

    /** @brief Initializes the buffer in an empty SBO state on the stack. */
    inline void init_empty() noexcept {
        m_heap.m_capacity.set(0);
        m_heap.m_size = 0;
        m_heap.m_data = nullptr;
        m_sbo.tag.clear();
        m_sbo.m_sbo[0] = '\0';
    }

    /** @brief Transfers raw bytes from another buffer and resets the source to empty. */
    inline void move_from(SqliteBuffer&& other) noexcept {
        memcpy(raw_bytes, other.raw_bytes, 24);
        other.init_empty();
    }

    /** @brief Helper to verify and ensure capacity for additional bytes. */
    inline bool ensure_capacity(sqlite3_int64 additional_bytes) {
        if (additional_bytes <= 0) return true;
        sqlite3_int64 target_cap = bytes() + additional_bytes;
        return try_reserve(target_cap).is_ok();
    }

public:
    /** @brief Constructs an empty buffer on the stack with zero heap allocation. */
    inline SqliteBuffer() noexcept {
        init_empty();
    }

    /**
     * @brief Constructs a buffer by copying the provided byte array.
     * @param data_ptr Pointer to source data (can be nullptr if len is 0).
     * @param len Number of bytes to copy.
     */
    inline SqliteBuffer(const void* data_ptr, sqlite3_int64 len) {
        init_empty();
        if (data_ptr && len > 0) append(data_ptr, len);
    }

    /** @brief Destructor. Automatically frees heap memory if allocated. */
    inline ~SqliteBuffer() {
        if (is_heap() && m_heap.m_data) {
            sqlite3_free(m_heap.m_data);
        }
    }

    // Non-copyable to prevent accidental large memory duplications
    SqliteBuffer(const SqliteBuffer&) = delete;
    SqliteBuffer& operator=(const SqliteBuffer&) = delete;

    /** @brief Move constructor: transfers ownership of the buffer without copying. */
    inline SqliteBuffer(SqliteBuffer&& other) noexcept {
        move_from(sqlite_move(other));
    }

    /** @brief Move assignment: frees current memory and transfers ownership from other. */
    inline SqliteBuffer& operator=(SqliteBuffer&& other) noexcept {
        if (this != &other) {
            if (is_heap() && m_heap.m_data) {
                sqlite3_free(m_heap.m_data);
            }
            move_from(sqlite_move(other));
        }
        return *this;
    }

    /** @brief Takes the owned buffer, resetting this object to empty without reallocation. */
    inline SqliteBuffer take() noexcept {
        return sqlite_move(*this);
    }

    /** @brief Returns true if buffer is currently stored inline on the stack (SBO). */
    inline bool is_sbo() const noexcept {
        return m_sbo.tag.is_sbo != 0;
    }

    /** @brief Alias for is_sbo(): returns true if buffer resides on the stack. */
    inline bool is_stack() const noexcept {
        return is_sbo();
    }

    /** @brief Alias for is_sbo(): returns true if buffer resides inline. */
    inline bool is_inline() const noexcept {
        return is_sbo();
    }

    /** @brief Returns true if the buffer has transitioned to heap allocation. */
    inline bool is_heap() const noexcept {
        return m_sbo.tag.is_sbo == 0;
    }

    /** @brief Returns a mutable pointer to the underlying byte array. */
    inline void* data() noexcept {
        return is_sbo() ? static_cast<void*>(m_sbo.m_sbo) : m_heap.m_data;
    }

    /** @brief Returns a const pointer to the underlying byte array. */
    inline const void* data() const noexcept {
        return is_sbo() ? static_cast<const void*>(m_sbo.m_sbo) : m_heap.m_data;
    }

    /**
     * @brief Returns a const C-string pointer.
     * Guaranteed to be null-terminated at data()[bytes()] in both SBO and heap modes.
     */
    inline const char* c_str() const noexcept {
        return static_cast<const char*>(data());
    }

    /** @brief Returns the active number of bytes stored in the buffer. */
    inline sqlite3_int64 bytes() const noexcept {
        return is_sbo() ? m_sbo.tag.get() : m_heap.m_size;
    }

    /** @brief STL-compatible size accessor returning the number of active bytes. */
    inline sqlite3_int64 size() const noexcept {
        return bytes();
    }

    /**
     * @brief Returns current buffer capacity.
     * Returns 22 in SBO mode, or the allocated heap capacity in heap mode.
     */
    inline sqlite3_int64 capacity() const noexcept {
        return is_sbo() ? static_cast<sqlite3_int64>(SBO_CAPACITY) : m_heap.m_capacity.get();
    }

    /** @brief STL-compatible empty check. */
    inline bool empty() const noexcept {
        return bytes() == 0;
    }

    /** @brief Checks if the buffer holds a valid state. */
    inline bool is_valid() const noexcept {
        return is_sbo() || m_heap.m_data != nullptr;
    }

    /** @brief Explicit boolean conversion checking validity. */
    inline explicit operator bool() const noexcept {
        return is_valid();
    }

    /** @brief Implicitly converts the buffer to a non-owning slice. */
    inline operator SqliteBufferSlice() const noexcept {
        return SqliteBufferSlice(data(), bytes());
    }

    /**
     * @brief Sets this buffer as the BLOB return result of a SQLite UDF context.
     * @param ctx Target SQLite UDF execution context.
     * @param dtor Memory disposal strategy callback (defaults to SQLITE_TRANSIENT).
     * @param subtype Optional SQLite 3.9+ application subtype.
     */
    inline void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        sqlite3_result_blob(ctx, data(), static_cast<int>(bytes()), dtor);
        if (subtype != SQLITE_SUBTYPE_NONE) {
            sqlite3_result_subtype(ctx, subtype);
        }
    }

    template <typename TContext>
    inline void result(TContext& ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        result(ctx.get(), dtor, subtype);
    }

    /**
     * @brief Binds this buffer as a BLOB parameter to a prepared statement at the given 1-based index.
     * @param stmt Prepared statement handle.
     * @param col 1-based parameter index.
     * @param dtor Memory disposal strategy callback (defaults to SQLITE_TRANSIENT).
     * @return SQLITE_OK on success, or SQLite error code.
     */
    inline int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_blob(stmt, col, data(), static_cast<int>(bytes()), dtor);
    }

    template <typename TStatement>
    inline int bind(TStatement& stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return bind(stmt.get(), col, dtor);
    }

    /**
     * @brief Attempts to reserve buffer capacity, returning SqliteStatus.
     * @param target_cap Minimum desired capacity in bytes.
     * @return SqliteStatus::ok() on success, or SqliteStatus::nomem() on allocation failure.
     */
    inline SqliteStatus try_reserve(sqlite3_int64 target_cap) {
        if (target_cap <= capacity()) return SqliteStatus::ok();
        if (is_sbo() && target_cap <= static_cast<sqlite3_int64>(SBO_CAPACITY)) {
            return SqliteStatus::ok();
        }
        sqlite3_int64 cur_len = bytes();
        sqlite3_int64 needed_cap = target_cap + 1; // +1 extra byte for guaranteed null-terminator
        if (needed_cap < 32) needed_cap = 32;

        if (is_sbo()) {
            void* new_data = sqlite3_malloc64(static_cast<sqlite3_uint64>(needed_cap));
            if (!new_data) {
                return SqliteStatus::nomem("Failed to reallocate memory in SqliteBuffer::try_reserve");
            }
            if (cur_len > 0) {
                memcpy(new_data, m_sbo.m_sbo, static_cast<size_t>(cur_len));
            }
            static_cast<char*>(new_data)[cur_len] = '\0';
            m_heap.m_capacity.set(needed_cap);
            m_heap.m_size = cur_len;
            m_heap.m_data = new_data;
        } else {
            void* new_data = sqlite3_realloc64(m_heap.m_data, static_cast<sqlite3_uint64>(needed_cap));
            if (!new_data) {
                return SqliteStatus::nomem("Failed to reallocate memory in SqliteBuffer::try_reserve");
            }
            m_heap.m_data = new_data;
            m_heap.m_capacity.set(needed_cap);
        }
        return SqliteStatus::ok();
    }

    /** @brief Reserves buffer capacity, returning true on success. */
    inline bool reserve(sqlite3_int64 target_cap) {
        return try_reserve(target_cap).is_ok();
    }

    /**
     * @brief Attempts to append raw bytes to the buffer, returning SqliteStatus.
     * Automatically transitions from SBO to heap if length exceeds 22 bytes.
     * Always preserves the trailing '\0' null-terminator invariant.
     * @param data_ptr Pointer to data to append.
     * @param len Number of bytes to append.
     * @return SqliteStatus indicating success or specific error.
     */
    inline SqliteStatus try_append(const void* data_ptr, sqlite3_int64 len) {
        if (!data_ptr || len <= 0) return SqliteStatus::ok();
        sqlite3_int64 cur_len = bytes();
        sqlite3_int64 new_len = cur_len + len;
        if (new_len < cur_len) {
            return SqliteStatus::err(SQLITE_TOOBIG, "Buffer size overflow in SqliteBuffer::try_append");
        }

        if (is_sbo()) {
            if (new_len <= static_cast<sqlite3_int64>(SBO_CAPACITY)) {
                memcpy(m_sbo.m_sbo + cur_len, data_ptr, static_cast<size_t>(len));
                m_sbo.m_sbo[new_len] = '\0';
                m_sbo.tag.set(new_len);
                return SqliteStatus::ok();
            }

            // Transition stack -> heap
            sqlite3_int64 new_cap = 32;
            while (new_cap < new_len + 1) {
                new_cap *= 2;
            }
            void* new_data = sqlite3_malloc64(static_cast<sqlite3_uint64>(new_cap));
            if (!new_data) {
                return SqliteStatus::nomem("Failed to grow buffer in SqliteBuffer::try_append");
            }
            if (cur_len > 0) {
                memcpy(new_data, m_sbo.m_sbo, static_cast<size_t>(cur_len));
            }
            memcpy(static_cast<char*>(new_data) + cur_len, data_ptr, static_cast<size_t>(len));
            static_cast<char*>(new_data)[new_len] = '\0';
            m_heap.m_capacity.set(new_cap);
            m_heap.m_size = new_len;
            m_heap.m_data = new_data;
            return SqliteStatus::ok();
        }

        // Already on heap
        sqlite3_int64 cur_cap = capacity();
        if (new_len + 1 > cur_cap) {
            sqlite3_int64 new_cap = cur_cap == 0 ? 32 : cur_cap * 2;
            if (new_cap < new_len + 1) {
                new_cap = new_len + 1;
            }
            void* new_data = sqlite3_realloc64(m_heap.m_data, static_cast<sqlite3_uint64>(new_cap));
            if (!new_data) {
                return SqliteStatus::nomem("Failed to grow buffer in SqliteBuffer::try_append");
            }
            m_heap.m_data = new_data;
            m_heap.m_capacity.set(new_cap);
        }
        memcpy(static_cast<char*>(m_heap.m_data) + cur_len, data_ptr, static_cast<size_t>(len));
        m_heap.m_size = new_len;
        static_cast<char*>(m_heap.m_data)[new_len] = '\0';
        return SqliteStatus::ok();
    }

    /** @brief Appends raw bytes to the buffer, auto-expanding if necessary. */
    inline bool append(const void* data_ptr, sqlite3_int64 bytes_len) {
        return try_append(data_ptr, bytes_len).is_ok();
    }

    /**
     * @brief Resets the active size to 0 without releasing allocated capacity.
     * Preserves the trailing null terminator at index 0.
     */
    inline void clear() noexcept {
        if (is_heap()) {
            m_heap.m_size = 0;
            if (m_heap.m_data) {
                static_cast<char*>(m_heap.m_data)[0] = '\0';
            }
        } else {
            init_empty();
        }
    }

    /** @brief Resets buffer and releases any heap allocation, returning to stack (SBO) mode. */
    inline void reset() noexcept {
        if (is_heap() && m_heap.m_data) {
            sqlite3_free(m_heap.m_data);
        }
        init_empty();
    }

    /**
     * @brief Truncates the active size of the buffer. Cannot expand the buffer.
     * Preserves the trailing null terminator at new_size.
     */
    inline void truncate(sqlite3_int64 new_size) noexcept {
        if (new_size < 0) new_size = 0;
        if (new_size >= bytes()) return;
        if (is_heap()) {
            m_heap.m_size = new_size;
            if (m_heap.m_data) {
                static_cast<char*>(m_heap.m_data)[new_size] = '\0';
            }
        } else {
            m_sbo.tag.set(new_size);
            m_sbo.m_sbo[new_size] = '\0';
        }
    }

    /** 
     * @brief Expands the buffer size without initializing the new memory.
     * Useful for direct streaming operations (e.g. from SqliteBlobStream or file handle).
     * Maintains trailing null-termination immediately following the uninitialized region.
     */
    inline void* append_uninitialized(sqlite3_int64 additional_bytes) {
        if (additional_bytes <= 0) return nullptr;
        sqlite3_int64 cur_len = bytes();
        if (!ensure_capacity(additional_bytes)) return nullptr;
        if (is_sbo() && (cur_len + additional_bytes <= static_cast<sqlite3_int64>(SBO_CAPACITY))) {
            m_sbo.tag.set(cur_len + additional_bytes);
            m_sbo.m_sbo[cur_len + additional_bytes] = '\0';
            return m_sbo.m_sbo + cur_len;
        } else {
            m_heap.m_size = cur_len + additional_bytes;
            static_cast<char*>(m_heap.m_data)[cur_len + additional_bytes] = '\0';
            return static_cast<char*>(m_heap.m_data) + cur_len;
        }
    }

    /** @brief Attempts to expand the buffer without initializing the new memory, returning SqliteResult. */
    inline SqliteResult<void*> try_append_uninitialized(sqlite3_int64 additional_bytes) {
        void* ptr = append_uninitialized(additional_bytes);
        if (!ptr && additional_bytes > 0) {
            return SqliteResult<void*>::nomem("Failed to grow buffer in SqliteBuffer::try_append_uninitialized");
        }
        return SqliteResult<void*>::ok(ptr);
    }

    /** @brief Extracts a non-owning slice of the buffer. */
    inline SqliteBufferSlice bufferSlice(sqlite3_int64 offset, sqlite3_int64 length) const {
        if (offset < 0 || offset >= bytes() || length <= 0) return SqliteBufferSlice();
        sqlite3_int64 actual_length = (offset + length > bytes()) ? (bytes() - offset) : length;
        return SqliteBufferSlice(static_cast<const char*>(data()) + offset, actual_length);
    }

    /** @brief Computes a 64-bit MurmurHash2 of the active buffer contents. */
    inline unsigned long long hash() const {
        return SqliteHashUtil::hash(data(), static_cast<int>(bytes()));
    }

    // Comparison against SqliteBuffer
    inline bool operator==(const SqliteBuffer& other) const {
        return SqliteMemoryUtil::memcmp_equal(data(), static_cast<int>(bytes()), other.data(), static_cast<int>(other.bytes()));
    }
    inline bool operator!=(const SqliteBuffer& other) const {
        return !(*this == other);
    }
    inline bool operator<(const SqliteBuffer& other) const {
        int min_len = (bytes() < other.bytes()) ? static_cast<int>(bytes()) : static_cast<int>(other.bytes());
        int cmp = 0;
        if (min_len > 0) cmp = memcmp(data(), other.data(), min_len);
        return (cmp != 0) ? (cmp < 0) : (bytes() < other.bytes());
    }
    inline bool operator>(const SqliteBuffer& other) const { return other < *this; }
    inline bool operator<=(const SqliteBuffer& other) const { return !(*this > other); }
    inline bool operator>=(const SqliteBuffer& other) const { return !(*this < other); }

    // Comparison against SqliteBufferSlice
    inline bool operator==(const SqliteBufferSlice& other) const {
        return SqliteMemoryUtil::memcmp_equal(data(), static_cast<int>(bytes()), other.data(), static_cast<int>(other.bytes()));
    }
    inline bool operator!=(const SqliteBufferSlice& other) const { return !(*this == other); }
    inline bool operator<(const SqliteBufferSlice& other) const {
        int min_len = (bytes() < other.bytes()) ? static_cast<int>(bytes()) : static_cast<int>(other.bytes());
        int cmp = 0;
        if (min_len > 0) cmp = memcmp(data(), other.data(), min_len);
        return (cmp != 0) ? (cmp < 0) : (bytes() < other.bytes());
    }
    inline bool operator>(const SqliteBufferSlice& other) const { return other < *this; }
    inline bool operator<=(const SqliteBufferSlice& other) const { return !(*this > other); }
    inline bool operator>=(const SqliteBufferSlice& other) const { return !(*this < other); }

};

static_assert(sizeof(SqliteBuffer) == 24, "SqliteBuffer must be exactly 24 bytes!");

/**
 * @brief High-performance dynamic string wrapping SqliteBuffer with guaranteed null-termination.
 * 
 * Replaces std::string in a -nostdlib++ environment. Exactly 24 bytes in size.
 *
 * Architecture & Design:
 * - Direct subclass of `SqliteBuffer`: inherits the exact 24-byte SBO layout without adding member variables.
 * - Zero Heap Allocations for Small Strings: Strings up to 22 characters live entirely on the stack.
 * - Guaranteed Null-Termination: Inherits `SqliteBuffer`'s invariant where `data()[length()] == '\0'`
 *   is unconditionally preserved across all mutations, making `c_str()` instantaneous and zero-cost.
 * - Pure Wrapper: Delegates all memory management, capacity growth, and SBO transitions to `SqliteBuffer`.
 *   Adds string-specific semantics: C-string construction, string views, character indexing, and lexicographical comparisons.
 */
class SqliteString : public SqliteBuffer {
public:
    /** @brief Constructs an empty string on the stack with zero heap allocation. */
    inline SqliteString() noexcept : SqliteBuffer() {}

    /**
     * @brief Constructs a string by copying a null-terminated C-string.
     * @param str C-string source pointer (safely treats nullptr as empty string).
     */
    inline SqliteString(const char* str)
        : SqliteBuffer(str, str ? SqliteStringUtil::sqlite_strlen(str) : 0) {}

    /**
     * @brief Constructs a string by copying explicit length of characters.
     * @param str Character array source pointer.
     * @param len Number of characters to copy.
     */
    inline SqliteString(const char* str, sqlite3_int64 len) : SqliteBuffer(str, len) {}

    /** @brief Move constructor: transfers ownership of the string without memory copying. */
    inline SqliteString(SqliteString&& other) noexcept : SqliteBuffer(sqlite_move(other)) {}

    /** @brief Move assignment: frees current memory and transfers ownership from other. */
    inline SqliteString& operator=(SqliteString&& other) noexcept {
        SqliteBuffer::operator=(sqlite_move(other));
        return *this;
    }

    /** @brief Takes the owned string value, transferring ownership and resetting this object to empty. */
    inline SqliteString take() noexcept {
        return sqlite_move(*this);
    }

    /**
     * @brief Returns a pointer to the null-terminated C-string.
     * Guaranteed to be non-null and valid in both stack and heap modes.
     */
    inline const char* c_str() const noexcept {
        return static_cast<const char*>(SqliteBuffer::data());
    }

    /** @brief Returns a const pointer to the underlying character data. */
    inline const char* data() const noexcept {
        return c_str();
    }

    /** @brief Returns a mutable pointer to the underlying character data. */
    inline char* data() noexcept {
        return static_cast<char*>(SqliteBuffer::data());
    }

    /** @brief Returns the length of the string, excluding the null terminator. */
    inline sqlite3_int64 length() const noexcept {
        return bytes();
    }

    /** @brief Appends a null-terminated C-string. */
    inline bool append(const char* str) {
        if (!str) return true;
        return SqliteBuffer::append(str, SqliteStringUtil::sqlite_strlen(str));
    }

    /** @brief Appends a character buffer with explicit length. */
    inline bool append(const char* str, sqlite3_int64 len) {
        return SqliteBuffer::append(str, len);
    }

    /** @brief Appends a single character. */
    inline bool append(char ch) {
        return SqliteBuffer::append(&ch, 1);
    }

    /** @brief Attempts to append a null-terminated C-string, returning SqliteStatus. */
    inline SqliteStatus try_append(const char* str) {
        if (!str) return SqliteStatus::ok();
        return SqliteBuffer::try_append(str, SqliteStringUtil::sqlite_strlen(str));
    }

    /** @brief Attempts to append a character buffer with explicit length, returning SqliteStatus. */
    inline SqliteStatus try_append(const char* str, sqlite3_int64 len) {
        return SqliteBuffer::try_append(str, len);
    }

    /** @brief Returns a zero-allocation SqliteStringView over this string (defined in sqlite3_value.hpp). */
    inline SqliteStringView view() const noexcept;

    /** @brief Extracts a non-owning slice of the string buffer. */
    inline SqliteBufferSlice bufferSlice(sqlite3_int64 offset, sqlite3_int64 len) const {
        return SqliteBuffer::bufferSlice(offset, len);
    }

    /**
     * @brief Sets this string as the TEXT return result of a SQLite UDF context.
     * @param ctx Target SQLite UDF execution context.
     * @param dtor Memory disposal strategy callback (defaults to SQLITE_TRANSIENT).
     * @param subtype Optional SQLite 3.9+ application subtype.
     */
    inline void result(sqlite3_context* ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        sqlite3_result_text(ctx, c_str(), static_cast<int>(length()), dtor);
        if (subtype != SQLITE_SUBTYPE_NONE) {
            sqlite3_result_subtype(ctx, subtype);
        }
    }

    template <typename TContext>
    inline void result(TContext& ctx, void(*dtor)(void*) = SQLITE_TRANSIENT, uint8_t subtype = SQLITE_SUBTYPE_NONE) const {
        result(ctx.get(), dtor, subtype);
    }

    /**
     * @brief Binds this string as a TEXT parameter to a prepared statement at the given 1-based index.
     * @param stmt Prepared statement handle.
     * @param col 1-based parameter index.
     * @param dtor Memory disposal strategy callback (defaults to SQLITE_TRANSIENT).
     * @return SQLITE_OK on success, or SQLite error code.
     */
    inline int bind(sqlite3_stmt* stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return sqlite3_bind_text(stmt, col, c_str(), static_cast<int>(length()), dtor);
    }

    template <typename TStatement>
    inline int bind(TStatement& stmt, int col, void(*dtor)(void*) = SQLITE_TRANSIENT) const {
        return bind(stmt.get(), col, dtor);
    }

    /**
     * @brief Factory method: attempts to construct a dynamic string, returning SqliteResult.
     * @param str Optional initial C-string.
     */
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

    /**
     * @brief Factory method: attempts to construct a dynamic string with explicit length.
     * @param str Initial character buffer.
     * @param len Length in characters.
     */
    static inline SqliteResult<SqliteString> try_create(const char* str, sqlite3_int64 len) {
        SqliteString s;
        if (str && len > 0) {
            SqliteStatus stat = s.try_append(str, len);
            if (stat.is_err()) {
                return SqliteResult<SqliteString>::err(stat.err_code(), stat.err_message());
            }
        }
        return SqliteResult<SqliteString>::ok(sqlite_move(s));
    }

    /** @brief Const character subscript access. */
    inline char operator[](sqlite3_int64 idx) const noexcept {
        return c_str()[idx];
    }

    /** @brief Mutable character subscript access. */
    inline char& operator[](sqlite3_int64 idx) noexcept {
        return data()[idx];
    }

    // Comparison against C-strings
    inline bool operator==(const char* other) const noexcept {
        if (!other) return length() == 0;
        return strcmp(c_str(), other) == 0;
    }
    inline bool operator!=(const char* other) const noexcept {
        return !(*this == other);
    }
    inline bool operator<(const char* other) const noexcept {
        const char* rhs = other ? other : "";
        int rhs_len = SqliteStringUtil::sqlite_strlen(rhs);
        int cur_len = static_cast<int>(length());
        int min_len = (cur_len < rhs_len) ? cur_len : rhs_len;
        int cmp = (min_len > 0) ? memcmp(c_str(), rhs, min_len) : 0;
        return (cmp != 0) ? (cmp < 0) : (cur_len < rhs_len);
    }
    inline bool operator>(const char* other) const noexcept {
        const char* rhs = other ? other : "";
        int rhs_len = SqliteStringUtil::sqlite_strlen(rhs);
        int cur_len = static_cast<int>(length());
        int min_len = (rhs_len < cur_len) ? rhs_len : cur_len;
        int cmp = (min_len > 0) ? memcmp(rhs, c_str(), min_len) : 0;
        return (cmp != 0) ? (cmp < 0) : (rhs_len < cur_len);
    }
    inline bool operator<=(const char* other) const noexcept { return !(*this > other); }
    inline bool operator>=(const char* other) const noexcept { return !(*this < other); }

    // Comparison against SqliteString
    inline bool operator==(const SqliteString& other) const noexcept {
        if (length() != other.length()) return false;
        if (length() == 0) return true;
        return memcmp(c_str(), other.c_str(), static_cast<size_t>(length())) == 0;
    }
    inline bool operator!=(const SqliteString& other) const noexcept {
        return !(*this == other);
    }
    inline bool operator<(const SqliteString& other) const noexcept {
        int min_len = (length() < other.length()) ? static_cast<int>(length()) : static_cast<int>(other.length());
        int cmp = (min_len > 0) ? memcmp(c_str(), other.c_str(), min_len) : 0;
        return (cmp != 0) ? (cmp < 0) : (length() < other.length());
    }
    inline bool operator>(const SqliteString& other) const noexcept { return other < *this; }
    inline bool operator<=(const SqliteString& other) const noexcept { return !(other < *this); }
    inline bool operator>=(const SqliteString& other) const noexcept { return !(*this < other); }

    // Comparison against nullptr
    inline bool operator==(decltype(nullptr)) const noexcept { return length() == 0; }
    inline bool operator!=(decltype(nullptr)) const noexcept { return length() != 0; }
    inline bool operator<=(decltype(nullptr)) const noexcept { return length() == 0; }
    inline bool operator>=(decltype(nullptr)) const noexcept { return true; }
    inline bool operator<(decltype(nullptr)) const noexcept { return false; }
    inline bool operator>(decltype(nullptr)) const noexcept { return length() > 0; }

};

static_assert(sizeof(SqliteString) == 24, "SqliteString must be exactly 24 bytes!");

// ============================================================================
// Symmetric Global Relational & Equality Operators (LHS == RHS)
// ============================================================================


inline bool operator==(const char* lhs, const SqliteString& rhs) {
    return rhs == lhs;
}

inline bool operator!=(const char* lhs, const SqliteString& rhs) {
    return rhs != lhs;
}

inline bool operator<(const char* lhs, const SqliteString& rhs) noexcept {
    return rhs > lhs;
}

inline bool operator>(const char* lhs, const SqliteString& rhs) noexcept {
    return rhs < lhs;
}

inline bool operator<=(const char* lhs, const SqliteString& rhs) noexcept {
    return rhs >= lhs;
}

inline bool operator>=(const char* lhs, const SqliteString& rhs) noexcept {
    return rhs <= lhs;
}

inline bool operator==(decltype(nullptr), const SqliteString& rhs) noexcept {
    return rhs == nullptr;
}

inline bool operator!=(decltype(nullptr), const SqliteString& rhs) noexcept {
    return rhs != nullptr;
}

inline bool operator<=(decltype(nullptr), const SqliteString& rhs) noexcept {
    return rhs >= nullptr;
}

inline bool operator>=(decltype(nullptr), const SqliteString& rhs) noexcept {
    return rhs <= nullptr;
}

// SqliteBufferSlice symmetric operators
inline bool operator==(decltype(nullptr), const SqliteBufferSlice& rhs) noexcept {
    return rhs == nullptr;
}

inline bool operator!=(decltype(nullptr), const SqliteBufferSlice& rhs) noexcept {
    return rhs != nullptr;
}

inline bool operator==(const char* lhs, const SqliteBufferSlice& rhs) noexcept {
    return rhs == lhs;
}

inline bool operator!=(const char* lhs, const SqliteBufferSlice& rhs) noexcept {
    return rhs != lhs;
}

// SqliteBufferSlice vs SqliteBuffer symmetric operators
inline bool operator==(const SqliteBufferSlice& lhs, const SqliteBuffer& rhs) noexcept {
    return rhs == lhs;
}

inline bool operator!=(const SqliteBufferSlice& lhs, const SqliteBuffer& rhs) noexcept {
    return rhs != lhs;
}

inline bool operator<(const SqliteBufferSlice& lhs, const SqliteBuffer& rhs) noexcept {
    return rhs > lhs;
}

inline bool operator>(const SqliteBufferSlice& lhs, const SqliteBuffer& rhs) noexcept {
    return rhs < lhs;
}

inline bool operator<=(const SqliteBufferSlice& lhs, const SqliteBuffer& rhs) noexcept {
    return rhs >= lhs;
}

inline bool operator>=(const SqliteBufferSlice& lhs, const SqliteBuffer& rhs) noexcept {
    return rhs <= lhs;
}

#endif // SQLITE3_BUFFER_HPP
