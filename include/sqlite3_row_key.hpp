#ifndef SQLITE3_ROW_KEY_HPP
#define SQLITE3_ROW_KEY_HPP

#include <sqlite3.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "sqlite3_hash.hpp"
#include "sqlite3_value.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_allocator.hpp"

// Forward declaration
class SqliteRowKeyOwned;

// ============================================================================
// SqliteRowKeyOwned: 16-Byte Small Buffer Optimized (SBO) Owned Row Key
// ============================================================================

/**
 * @class SqliteRowKeyOwned
 * @brief Exact 16-Byte (128-bit) Small Buffer Optimized (SBO) Primary Key container.
 * 
 * - Single-column keys (N = 1, 95% of use cases): Stored 100% in-situ with 0 heap allocations.
 * - Multi-column composite keys (N = 2..8): Reuses the 16 bytes as a dynamic heap descriptor.
 * - Memory density: Fits exactly 4 complete keys per 64-byte L1 CPU cache line.
 * - Relational Matrix: Full symmetric comparisons (==, !=, <, <=, >, >=) against keys,
 *   row spans, view arrays, strings, blobs, and native C++ primitives (int, double, const char*).
 * - Transparent STL Lookups: Supports zero-allocation B-Tree and Swiss Table queries via
 *   SqliteRowKeyHash, SqliteRowKeyEqual, and SqliteRowKeyLess.
 */
class SqliteRowKeyOwned {
private:
    // Representation B: Dynamic Heap Array Descriptor (size != 1)
    struct HeapRep {
        SqliteValueOwned*   ptr;      // 8 Bytes (Offset 0..7: pointer to heap array)
        uint32_t            size;     // 4 Bytes (Offset 8..11: column count)
        uint16_t            capacity; // 2 Bytes (Offset 12..13: buffer capacity)
        uint8_t             reserved; // 1 Byte  (Offset 14: ABI alignment byte)
        SqliteOwnedValueTag tag;      // 1 Byte  (Offset 15: Shared Control Tag Byte!)
    };
    static_assert(sizeof(HeapRep) == 16, "HeapRep must be exactly 16 bytes");

    // The Overlapping 16-Byte Union
    union {
        SqliteValueOwned m_single; // Mode A: In-Situ Value (tag at Offset 15)
        HeapRep          m_heap;   // Mode B: Array Descriptor (tag at Offset 15)
        uint64_t         m_align;  // Forces 8-byte alignment
    };

    /** @brief Accesses the shared tag byte at Offset 15 directly. */
    inline SqliteOwnedValueTag tag() const noexcept {
        return m_heap.tag;
    }

    /** @brief Checks if the container holds a multi-element composite key (size != 1). */
    inline bool is_row_key() const noexcept {
        return tag().is_row_key();
    }

    /** @brief Initializes an empty key state (Mode B, size = 0). */
    inline void init_empty() noexcept {
        m_heap.ptr = nullptr;
        m_heap.size = 0;
        m_heap.capacity = 0;
        m_heap.reserved = 0;
        m_heap.tag.set_as_row_key(); // Sets tag.raw = 0x00
    }

    /** @brief Destroys active payload and frees heap buffer if in composite Mode B. */
    inline void destroy_payload() noexcept {
        if (is_row_key()) {
            if (m_heap.ptr) {
                sqlite_destroy_n(m_heap.ptr, m_heap.size);
                sqlite_delete_array(m_heap.ptr);
                m_heap.ptr = nullptr;
            }
            m_heap.size = 0;
        } else {
            m_single.~SqliteValueOwned();
            init_empty();
        }
    }

public:
    // ========================================================================
    // Constructors & Destructor
    // ========================================================================

    /** @brief Constructs an empty 16-byte key (size = 0). */
    inline SqliteRowKeyOwned() noexcept {
        init_empty();
    }

    /** @brief Constructs an in-situ 1-element key from a single owned value (0 mallocs). */
    inline explicit SqliteRowKeyOwned(const SqliteValueOwned& val) noexcept {
        sqlite_construct_at(&m_single, val.clone());
    }

    /** @brief Constructs an in-situ 1-element key from a single transient view (0 mallocs). */
    inline explicit SqliteRowKeyOwned(const SqliteValueView& val) noexcept {
        sqlite_construct_at(&m_single, val.to_owned());
    }

    /** @brief Constructs an in-situ 1-element key from a string view (0 mallocs if fits in SBO). */
    inline explicit SqliteRowKeyOwned(const SqliteStringView& str) noexcept {
        sqlite_construct_at(&m_single, SqliteValueOwned::from_text(str.data(), str.length()));
    }

    /** @brief Constructs an in-situ 1-element key from an owned string. */
    inline explicit SqliteRowKeyOwned(const SqliteStringOwned& str) noexcept {
        sqlite_construct_at(&m_single, SqliteValueOwned::from_text(str.value(), str.length()));
    }

    /** @brief Constructs an in-situ 1-element key from a blob view. */
    inline explicit SqliteRowKeyOwned(const SqliteBlobView& blob) noexcept {
        sqlite_construct_at(&m_single, SqliteValueOwned::from_blob(blob.data(), blob.size()));
    }

    /** @brief Constructs an in-situ 1-element key from an owned blob. */
    inline explicit SqliteRowKeyOwned(const SqliteBlobOwned& blob) noexcept {
        sqlite_construct_at(&m_single, SqliteValueOwned::from_blob(blob.data(), blob.size()));
    }

    /** @brief Constructs an owned key from a transient argument view array. */
    inline explicit SqliteRowKeyOwned(const SqliteValueViewArray& view_arr) {
        int count = view_arr.size();
        if (count == 0) {
            init_empty();
        } else if (count == 1) {
            sqlite_construct_at(&m_single, view_arr[0].to_owned());
        } else {
            init_empty();
            resize(count);
            for (int i = 0; i < count; ++i) {
                m_heap.ptr[i] = view_arr[i].to_owned();
            }
        }
    }

    /** @brief Constructs an owned key from a non-owning SqliteRowOwnedWrapper span. */
    inline explicit SqliteRowKeyOwned(const SqliteRowOwnedWrapper& view_span) {
        int count = view_span.size();
        if (count == 0) {
            init_empty();
        } else if (count == 1) {
            sqlite_construct_at(&m_single, view_span[0].clone());
        } else {
            init_empty();
            resize(count);
            for (int i = 0; i < count; ++i) {
                m_heap.ptr[i] = view_span[i].clone();
            }
        }
    }

    /** 
     * @brief Extracts key columns from a dynamic row using column index mapping.
     * 
     * @tparam RowType Source row container type.
     * @param full_row Complete database row (all columns).
     * @param key_indices Array of column indices defining the composite key.
     * @param key_count Number of key columns.
     */
    template <typename RowType>
    inline SqliteRowKeyOwned(const RowType& full_row, const int* key_indices, int key_count) {
        if (!key_indices || key_count <= 0) {
            init_empty();
        } else if (key_count == 1) {
            int col = key_indices[0];
            if (col >= 0 && col < full_row.size()) {
                sqlite_construct_at(&m_single, full_row[col].clone());
            } else {
                sqlite_construct_at(&m_single);
            }
        } else {
            init_empty();
            resize(key_count);
            for (int i = 0; i < key_count; ++i) {
                int col = key_indices[i];
                if (col >= 0 && col < full_row.size()) {
                    m_heap.ptr[i] = full_row[col].clone();
                } else {
                    m_heap.ptr[i] = SqliteValueOwned();
                }
            }
        }
    }

    /** @brief Destructor that releases allocated payload. */
    ~SqliteRowKeyOwned() {
        destroy_payload();
    }

    // Move Semantics (1-cycle transfer)
    inline SqliteRowKeyOwned(SqliteRowKeyOwned&& other) noexcept {
        memcpy(static_cast<void*>(this), &other, sizeof(SqliteRowKeyOwned));
        other.init_empty();
    }

    inline SqliteRowKeyOwned& operator=(SqliteRowKeyOwned&& other) noexcept {
        if (this != &other) {
            destroy_payload();
            memcpy(static_cast<void*>(this), &other, sizeof(SqliteRowKeyOwned));
            other.init_empty();
        }
        return *this;
    }

    // Copy Semantics (Deep copy)
    inline SqliteRowKeyOwned(const SqliteRowKeyOwned& other) {
        if (!other.is_row_key()) {
            sqlite_construct_at(&m_single, other.m_single.clone());
        } else {
            init_empty();
            if (other.m_heap.size > 0 && other.m_heap.ptr) {
                resize(other.m_heap.size);
                for (uint32_t i = 0; i < other.m_heap.size; ++i) {
                    m_heap.ptr[i] = other.m_heap.ptr[i].clone();
                }
            }
        }
    }

    inline SqliteRowKeyOwned& operator=(const SqliteRowKeyOwned& other) {
        if (this != &other) {
            destroy_payload();
            if (!other.is_row_key()) {
                sqlite_construct_at(&m_single, other.m_single.clone());
            } else {
                init_empty();
                if (other.m_heap.size > 0 && other.m_heap.ptr) {
                    resize(other.m_heap.size);
                    for (uint32_t i = 0; i < other.m_heap.size; ++i) {
                        m_heap.ptr[i] = other.m_heap.ptr[i].clone();
                    }
                }
            }
        }
        return *this;
    }

    // ========================================================================
    // Capacity & Element Accessors
    // ========================================================================

    /** @brief Returns column count (1 for in-situ scalar, N for composite). */
    inline int  size()  const noexcept { return is_row_key() ? static_cast<int>(m_heap.size) : 1; }
    /** @brief Alias for size(). */
    inline int  count() const noexcept { return size(); }
    /** @brief Checks if empty (size == 0). */
    inline bool empty() const noexcept { return is_row_key() ? (m_heap.size == 0) : false; }

    /** 
     * @brief Resizes column count, automatically transitioning between SBO modes.
     * 
     * @param new_count Target number of columns.
     */
    inline void resize(int new_count) {
        if (new_count < 0) new_count = 0;
        uint32_t target = static_cast<uint32_t>(new_count);

        if (target == 1) {
            if (is_row_key()) {
                SqliteValueOwned* old_ptr = m_heap.ptr;
                uint32_t old_sz = m_heap.size;
                SqliteValueOwned temp = (old_sz > 0 && old_ptr) ? sqlite_move(old_ptr[0]) : SqliteValueOwned();

                if (old_ptr) {
                    sqlite_destroy_n(old_ptr, old_sz);
                    sqlite_delete_array(old_ptr);
                }
                sqlite_construct_at(&m_single, sqlite_move(temp));
            }
            return;
        }

        if (!is_row_key()) {
            SqliteValueOwned temp = sqlite_move(m_single);
            m_single.~SqliteValueOwned();
            init_empty();

            if (target > 0) {
                m_heap.ptr = sqlite_new_array<SqliteValueOwned>(target);
                m_heap.capacity = static_cast<uint16_t>(target);
                m_heap.size = target;
                m_heap.tag.set_as_row_key();
                sqlite_construct_at(&m_heap.ptr[0], sqlite_move(temp));
                for (uint32_t i = 1; i < target; ++i) sqlite_construct_at(&m_heap.ptr[i]);
            }
        } else {
            if (target > m_heap.capacity) {
                SqliteValueOwned* new_buf = sqlite_new_array<SqliteValueOwned>(target);
                for (uint32_t i = 0; i < m_heap.size; ++i) {
                    sqlite_construct_at(&new_buf[i], sqlite_move(m_heap.ptr[i]));
                }
                if (m_heap.ptr) {
                    sqlite_destroy_n(m_heap.ptr, m_heap.size);
                    sqlite_delete_array(m_heap.ptr);
                }
                m_heap.ptr = new_buf;
                m_heap.capacity = static_cast<uint16_t>(target);
            }
            if (target > m_heap.size) {
                for (uint32_t i = m_heap.size; i < target; ++i) sqlite_construct_at(&m_heap.ptr[i]);
            } else if (target < m_heap.size) {
                sqlite_destroy_n(m_heap.ptr + target, m_heap.size - target);
            }
            m_heap.size = target;
            m_heap.tag.set_as_row_key();
        }
    }

    /** @brief Direct pointer to contiguous key column buffer. */
    inline SqliteValueOwned* data() noexcept {
        return is_row_key() ? m_heap.ptr : &m_single;
    }

    /** @brief Const pointer to contiguous key column buffer. */
    inline const SqliteValueOwned* data() const noexcept {
        return is_row_key() ? m_heap.ptr : &m_single;
    }

    /** @brief Indexed column accessor with bounds clamping. */
    inline SqliteValueOwned& operator[](int idx) noexcept {
        return data()[idx >= 0 && idx < size() ? idx : 0];
    }

    /** @brief Const indexed column accessor with bounds clamping. */
    inline const SqliteValueOwned& operator[](int idx) const noexcept {
        return data()[idx >= 0 && idx < size() ? idx : 0];
    }

    /** @brief Yields a zero-allocation 16-byte SqliteRowOwnedWrapper span. */
    inline SqliteRowOwnedWrapper view() const noexcept {
        return SqliteRowOwnedWrapper(data(), size());
    }

    /** @brief Implicit conversion to SqliteRowOwnedWrapper span. */
    inline operator SqliteRowOwnedWrapper() const noexcept {
        return view();
    }

    // Typed Column Extraction Accessors, Composite Hashing & Iterator
    SQLITE_DERIVE_ARRAY_ACCESSORS
    SQLITE_DERIVE_ARRAY_HASH
    SQLITE_DERIVE_ARRAY_ITERATOR(SqliteRowKeyOwned, const SqliteValueOwned&)

    // ========================================================================
    // Full Relational Operators (==, !=, <, <=, >, >=)
    // ========================================================================
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowKeyOwned)
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowOwnedWrapper)
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteValueViewArray)
    SQLITE_DERIVE_CONTAINER_RELATIONAL_OPS(SqliteRowView)
    SQLITE_DERIVE_ALL_SCALAR_RELATIONAL_OPS
};

// ============================================================================
// Non-Member Symmetric Reverse Relational Operators for SqliteRowKeyOwned
// ============================================================================

SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteRowOwnedWrapper, SqliteRowKeyOwned)
SQLITE_DERIVE_REVERSE_RELATIONAL_OPS(SqliteRowView, SqliteRowKeyOwned)
SQLITE_DERIVE_ALL_REVERSE_RELATIONAL_OPS(SqliteRowKeyOwned)

// ============================================================================
// Transparent Functors for Swiss Tables & B-Trees
// ============================================================================

/**
 * @struct SqliteRowKeyHash
 * @brief Transparent 64-bit MurmurHash2 functor for map containers.
 * 
 * Supports zero-allocation hashing over SqliteRowKeyOwned, SqliteRowOwnedWrapper,
 * SqliteValueViewArray, strings, blobs, and native C++ primitive types.
 */
struct SqliteRowKeyHash {
    using is_transparent = void;

    inline size_t operator()(const SqliteRowKeyOwned& k) const noexcept { return static_cast<size_t>(k.hash()); }
    SQLITE_DERIVE_TRANSPARENT_ROW_HASH_OVERLOADS
};

/**
 * @struct SqliteRowKeyEqual
 * @brief Transparent equality functor for map containers.
 */
SQLITE_DERIVE_TRANSPARENT_EQUAL(SqliteRowKeyEqual)

/**
 * @struct SqliteRowKeyLess
 * @brief Transparent less-than functor for B-Tree and ordered map containers.
 */
SQLITE_DERIVE_TRANSPARENT_LESS(SqliteRowKeyLess)

#endif // SQLITE3_ROW_KEY_HPP
