#ifndef DUOSTL_HASH_HPP
#define DUOSTL_HASH_HPP

/* ============================================================================
 * duo_hash.hpp - Dual-ABI Robin Hood Hash Map & Set for C++17
 *
 * Part of DuoSTL: Zero-dependency embedded container library for SQLite extensions
 * and high-performance native systems.
 *
 * Design Guarantees:
 *   - Freestanding C++17 (-nostdlib++) with zero standard library headers
 *   - No <new>, no <type_traits>, no <utility>, no <memory>, no <functional>
 *   - Zero C++ exceptions (-fno-exceptions)
 *   - Zero C++ Run-Time Type Information (-fno-rtti)
 *   - 100% standard-layout, binary-compatible with pure C C11 ABI structs
 *   - Single-pointer footprint (exactly 8 bytes on 64-bit platforms)
 *
 * Architecture:
 *   duo::HashMap<Key, Value> and duo::HashSet<T> wrap standard-layout C mirror
 *   structs (c_hashmap_t<Key, Value> and c_hashset_t<T>) containing a single
 *   pointer to the underlying pure C Robin Hood hash table (duo_hashmap_t*).
 *
 *   Features:
 *     1. Open-addressed Robin Hood hashing with Distance-to-Initial-Bucket (DIB).
 *     2. Automatic backward-shift deletion without tombstones.
 *     3. Compile-time branching for trivially copyable/destructible types vs
 *        non-trivial types via C++17 `if constexpr`.
 *     4. Structured bindings support (`auto [key, val] : map`).
 *     5. Complete Dual-ABI FFI via DUO_CXX_FFI_OPS and direct `c_ptr()` / `c_val()`.
 *     6. Atomic, hazard-free predicate filtering (`filter(pred)`) resolving
 *        upstream tidwall/hashmap.c Issue #41.
 *     7. Dual key and value destructors resolving Issue #38.
 *     8. Const-correct retrieval and safe copy-out resolving Issue #42.
 * ============================================================================ */

#include "duo_alloc.hpp"

extern "C" {
    #include "duo_hash.h"
}

namespace duo {

// ============================================================================
// 1. FREESTANDING HASH & EQUALITY FUNCTORS
// ============================================================================

/**
 * @struct Hash
 * @brief Default 64-bit hashing functor using xxHash3.
 *
 * Supports 3-argument `operator()(const T&, uint64_t seed0, uint64_t seed1)`
 * as well as 1-argument `operator()(const T&)`.
 *
 * @tparam T Type to hash.
 */
template <typename T, typename = void>
struct Hash {
    inline uint64_t operator()(const T& key, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return duo_hash_xxhash3(&key, sizeof(T), seed0, seed1);
    }
};

/**
 * @brief Hash specialization for raw pointers: hashes pointer address value.
 *
 * @tparam T Pointee type.
 */
template <typename T>
struct Hash<T*> {
    inline uint64_t operator()(const T* ptr, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return duo_hash_xxhash3(&ptr, sizeof(ptr), seed0, seed1);
    }
};

/**
 * @brief Hash specialization for null-terminated C-style strings.
 */
template <>
struct Hash<const char*> {
    inline uint64_t operator()(const char* s, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        if (!s) return 0;
        size_t len = 0;
        while (s[len]) len++;
        return duo_hash_xxhash3(s, len, seed0, seed1);
    }
};

/**
 * @brief Hash specialization for mutable char* null-terminated strings.
 */
template <>
struct Hash<char*> {
    inline uint64_t operator()(char* s, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return Hash<const char*>{}(s, seed0, seed1);
    }
};

/**
 * @brief SFINAE specialization for containers exposing member .hash(seed0, seed1) (e.g. duo::String, duo::Bytes, duo::SpanView).
 *
 * @tparam T Container type with member hash method.
 */
template <typename T>
struct Hash<T, void_t<decltype(declval<const T>().hash(uint64_t(0), uint64_t(0)))>> {
    inline uint64_t operator()(const T& s, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return s.hash(seed0, seed1);
    }
};

/**
 * @brief Internal SFINAE helper to invoke a 3-argument hash functor h(key, seed0, seed1).
 */
template <typename H, typename K>
static inline auto invoke_hash_fn(const H& h, const K& key, uint64_t s0, uint64_t s1, int)
    -> decltype(h(key, s0, s1)) {
    return h(key, s0, s1);
}

/**
 * @brief Internal fallback helper to invoke a 1-argument hash functor h(key).
 */
template <typename H, typename K>
static inline auto invoke_hash_fn(const H& h, const K& key, uint64_t, uint64_t, ...)
    -> decltype(h(key)) {
    return static_cast<uint64_t>(h(key));
}

/**
 * @struct Equal
 * @brief Default equality comparison functor using operator== or strcmp.
 *
 * @tparam T Type to compare.
 */
template <typename T, typename = void>
struct Equal {
    inline bool operator()(const T& a, const T& b) const noexcept {
        return a == b;
    }
};

/**
 * @brief Equal specialization for null-terminated C-style strings using strcmp logic.
 */
template <>
struct Equal<const char*> {
    /**
     * @brief Compares two null-terminated C strings for equality.
     *
     * @param a First string.
     * @param b Second string.
     * @return true if strings match, false otherwise.
     */
    inline bool operator()(const char* a, const char* b) const noexcept {
        if (a == b) return true;
        if (!a || !b) return false;
        while (*a && (*a == *b)) { a++; b++; }
        return *a == *b;
    }
};

/**
 * @brief Equal specialization for mutable char* null-terminated strings.
 */
template <>
struct Equal<char*> {
    /**
     * @brief Compares two null-terminated C strings for equality.
     *
     * @param a First string.
     * @param b Second string.
     * @return true if strings match, false otherwise.
     */
    inline bool operator()(char* a, char* b) const noexcept {
        return Equal<const char*>{}(a, b);
    }
};

/**
 * @struct SipHash
 * @brief Standalone 64-bit SipHash-2-4 functor.
 *
 * @tparam T Type to hash.
 */
template <typename T>
struct SipHash {
    /**
     * @brief Hashes key bytes using SipHash-2-4.
     *
     * @param key   Object to hash.
     * @param seed0 Primary 64-bit seed.
     * @param seed1 Secondary 64-bit seed.
     * @return 64-bit hash code.
     */
    inline uint64_t operator()(const T& key, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return duo_hash_sip(&key, sizeof(T), seed0, seed1);
    }
};

/**
 * @brief SipHash-2-4 specialization for null-terminated C-style strings.
 */
template <>
struct SipHash<const char*> {
    /**
     * @brief Hashes null-terminated string bytes using SipHash-2-4.
     *
     * @param s     String to hash.
     * @param seed0 Primary 64-bit seed.
     * @param seed1 Secondary 64-bit seed.
     * @return 64-bit hash code.
     */
    inline uint64_t operator()(const char* s, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        if (!s) return 0;
        size_t len = 0;
        while (s[len]) len++;
        return duo_hash_sip(s, len, seed0, seed1);
    }
};

/**
 * @brief SipHash-2-4 specialization for mutable char* strings.
 */
template <>
struct SipHash<char*> {
    /**
     * @brief Hashes null-terminated string bytes using SipHash-2-4.
     *
     * @param s     String to hash.
     * @param seed0 Primary 64-bit seed.
     * @param seed1 Secondary 64-bit seed.
     * @return 64-bit hash code.
     */
    inline uint64_t operator()(char* s, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return SipHash<const char*>{}(s, seed0, seed1);
    }
};

/**
 * @struct MurmurHash
 * @brief Standalone 64-bit MurmurHash3_86_128 functor (hardened against unaligned loads and UB shift).
 *
 * @tparam T Type to hash.
 */
template <typename T>
struct MurmurHash {
    /**
     * @brief Hashes key bytes using MurmurHash3.
     *
     * @param key   Object to hash.
     * @param seed0 Primary 64-bit seed.
     * @param seed1 Secondary 64-bit seed.
     * @return 64-bit hash code.
     */
    inline uint64_t operator()(const T& key, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return duo_hash_murmur(&key, sizeof(T), seed0, seed1);
    }
};

/**
 * @brief MurmurHash3 specialization for null-terminated C-style strings.
 */
template <>
struct MurmurHash<const char*> {
    /**
     * @brief Hashes null-terminated string bytes using MurmurHash3.
     *
     * @param s     String to hash.
     * @param seed0 Primary 64-bit seed.
     * @param seed1 Secondary 64-bit seed.
     * @return 64-bit hash code.
     */
    inline uint64_t operator()(const char* s, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        if (!s) return 0;
        size_t len = 0;
        while (s[len]) len++;
        return duo_hash_murmur(s, len, seed0, seed1);
    }
};

/**
 * @brief MurmurHash3 specialization for mutable char* strings.
 */
template <>
struct MurmurHash<char*> {
    /**
     * @brief Hashes null-terminated string bytes using MurmurHash3.
     *
     * @param s     String to hash.
     * @param seed0 Primary 64-bit seed.
     * @param seed1 Secondary 64-bit seed.
     * @return 64-bit hash code.
     */
    inline uint64_t operator()(char* s, uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return MurmurHash<const char*>{}(s, seed0, seed1);
    }
};

/**
 * @brief Standalone 64-bit xxHash3 function (pure C duo_hash_xxhash3 wrapper).
 */
inline uint64_t xxhash3(const void* data, size_t len, uint64_t seed0 = 0, uint64_t seed1 = 0) noexcept {
    return duo_hash_xxhash3(data, len, seed0, seed1);
}

/**
 * @brief Standalone 64-bit SipHash-2-4 function (pure C duo_hash_sip wrapper).
 */
inline uint64_t siphash(const void* data, size_t len, uint64_t seed0 = 0, uint64_t seed1 = 0) noexcept {
    return duo_hash_sip(data, len, seed0, seed1);
}

/**
 * @brief Standalone 64-bit MurmurHash3_86_128 function (pure C duo_hash_murmur wrapper).
 */
inline uint64_t murmurhash(const void* data, size_t len, uint64_t seed0 = 0, uint64_t seed1 = 0) noexcept {
    return duo_hash_murmur(data, len, seed0, seed1);
}

// ============================================================================
// 2. PAIR & ITERATOR ENTRY PROXIES FOR STRUCTURED BINDINGS
// ============================================================================

/**
 * @struct Pair
 * @brief Lightweight, standard-layout Key-Value pair for C++17.
 *
 * @tparam First  Type of the first member.
 * @tparam Second Type of the second member.
 */
template <typename First, typename Second>
struct Pair {
    First first;   /**< First element (key). */
    Second second; /**< Second element (value). */
};

/**
 * @struct EntryRef
 * @brief Mutable Key-Value proxy yielding references to bucket elements.
 *
 * Designed as a strict 2-member C++17 aggregate to enable native structured
 * bindings (`for (auto [key, val] : map)` or `for (auto& [key, val] : map)`).
 *
 * @tparam K Key type.
 * @tparam V Value type.
 */
template <typename K, typename V>
struct EntryRef {
    const K& key;   /**< Immutable key reference. */
    V&       value; /**< Mutable value reference. */

    /** @brief Accessor yielding immutable key reference (std::pair emulation). */
    inline const K& first() const noexcept { return key; }
    /** @brief Accessor yielding mutable value reference (std::pair emulation). */
    inline V& second() const noexcept { return value; }
};

/**
 * @struct ConstEntryRef
 * @brief Immutable Key-Value proxy yielding const references to bucket elements.
 *
 * @tparam K Key type.
 * @tparam V Value type.
 */
template <typename K, typename V>
struct ConstEntryRef {
    const K& key;   /**< Immutable key reference. */
    const V& value; /**< Immutable value reference. */

    /** @brief Accessor yielding immutable key reference (std::pair emulation). */
    inline const K& first() const noexcept { return key; }
    /** @brief Accessor yielding immutable value reference (std::pair emulation). */
    inline const V& second() const noexcept { return value; }
};

// ============================================================================
// 3. STANDARD-LAYOUT C MIRROR STRUCTS
// ============================================================================

/**
 * @struct c_hashmap_t
 * @brief Standard-layout C mirror struct representing an owning hash map.
 * Exactly 8 bytes on 64-bit architectures, wrapping `duo_hashmap_t* raw`.
 *
 * @tparam Key   Key type.
 * @tparam Value Value type.
 */
template <typename Key, typename Value>
struct c_hashmap_t {
    duo_hashmap_t* raw; /**< Pointer to pure C Robin Hood hash table. */

    /** @brief Implicit conversion to underlying pure C duo_hashmap_t handle pointer. */
    inline operator duo_hashmap_t*() const noexcept { return raw; }
};

/**
 * @struct c_hashset_t
 * @brief Standard-layout C mirror struct representing an owning hash set.
 * Exactly 8 bytes on 64-bit architectures, wrapping `duo_hashset_t* raw`.
 *
 * @tparam T Element type.
 */
template <typename T>
struct c_hashset_t {
    duo_hashset_t* raw; /**< Pointer to pure C Robin Hood hash table. */

    /** @brief Implicit conversion to underlying pure C duo_hashset_t handle pointer. */
    inline operator duo_hashset_t*() const noexcept { return raw; }
};


// ============================================================================
// 4. C++17 ASSOCIATIVE CONTAINER: duo::HashMap<Key, Value>
// ============================================================================

/**
 * @class HashMap
 * @brief Freestanding Robin Hood hash map container for C++17.
 *
 * Wraps a standard-layout C mirror struct `m_inner` of type `c_hashmap_t<Key, Value>`
 * (exactly 8 bytes, standard-layout).
 *
 * @tparam Key     Key type.
 * @tparam Value   Value type.
 * @tparam HashFn  Hash functor (default duo::Hash<Key>).
 * @tparam EqualFn Key equality functor (default duo::Equal<Key>).
 */
template <typename Key, typename Value, typename HashFn = Hash<Key>, typename EqualFn = Equal<Key>>
class HashMap {
public:
    using self_type       = HashMap;
    using c_type          = c_hashmap_t<Key, Value>;
    using key_type        = Key;
    using mapped_type     = Value;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using hasher          = HashFn;
    using key_equal       = EqualFn;

    c_type m_inner{nullptr}; /**< Standard-layout C mirror struct at Offset 0. */

private:
    // Static C Trampolines
    /**
     * @brief Static pure C hash trampoline adapting HashFn to duo_hash_hash_fn signature.
     *
     * @param key   Pointer to key payload.
     * @param seed0 Primary 64-bit seed.
     * @param seed1 Secondary 64-bit seed.
     * @return 64-bit hash value.
     */
    static uint64_t hash_trampoline(const void* key, uint64_t seed0, uint64_t seed1) {
        const Key* k = static_cast<const Key*>(key);
        return invoke_hash_fn(HashFn{}, *k, seed0, seed1, 0);
    }

    /**
     * @brief Static pure C equality comparator trampoline adapting EqualFn to duo_hash_compare_fn signature.
     *
     * @param a     Pointer to first key.
     * @param b     Pointer to second key.
     * @param udata User data pointer (unused).
     * @return 0 if equal, 1 otherwise.
     */
    static int compare_trampoline(const void* a, const void* b, void* udata) {
        (void)udata;
        const Key* ka = static_cast<const Key*>(a);
        const Key* kb = static_cast<const Key*>(b);
        return EqualFn{}(*ka, *kb) ? 0 : 1;
    }

    /**
     * @brief Static key destructor trampoline invoking C++ destructor for non-trivially destructible key types.
     *
     * @param key Pointer to key payload inside bucket.
     */
    static void key_destructor_trampoline(void* key) {
        if constexpr (!duo::is_trivially_destructible<Key>::value) {
            duo::destroy_at(static_cast<Key*>(key));
        }
    }

    /**
     * @brief Static value destructor trampoline invoking C++ destructor for non-trivially destructible value types.
     *
     * @param val Pointer to value payload inside bucket.
     */
    static void val_destructor_trampoline(void* val) {
        if constexpr (!duo::is_trivially_destructible<Value>::value) {
            duo::destroy_at(static_cast<Value*>(val));
        }
    }

    /** @brief Compile-time offset of Key payload within a hash bucket (past 8-byte header). */
    static constexpr size_t k_key_offset = sizeof(struct duo_hash_bucket);

    /** @brief Compile-time aligned offset of Value payload within a hash bucket. */
    static constexpr size_t k_val_offset = (k_key_offset + sizeof(Key) + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);

    /** @brief Total aligned stride of a single bucket in bytes. */
    static constexpr size_t k_bucketsz   = ((sizeof(Value) > 0 ? k_val_offset + sizeof(Value) : k_val_offset) + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);

private:
    /**
     * @brief Computes mutable bucket pointer at index in raw contiguous bucket memory.
     *
     * @param buckets Base pointer to bucket array.
     * @param idx     Bucket slot index.
     * @return Pointer to duo_hash_bucket header.
     */
    static inline struct duo_hash_bucket* get_bucket(void* buckets, size_t idx) noexcept {
        return reinterpret_cast<struct duo_hash_bucket*>(static_cast<char*>(buckets) + (k_bucketsz * idx));
    }

    /**
     * @brief Computes const bucket pointer at index in raw contiguous bucket memory.
     *
     * @param buckets Base pointer to bucket array.
     * @param idx     Bucket slot index.
     * @return Const pointer to duo_hash_bucket header.
     */
    static inline const struct duo_hash_bucket* get_bucket(const void* buckets, size_t idx) noexcept {
        return reinterpret_cast<const struct duo_hash_bucket*>(static_cast<const char*>(buckets) + (k_bucketsz * idx));
    }

    /**
     * @brief Computes mutable key pointer embedded inside a bucket.
     *
     * @param b Pointer to bucket.
     * @return Pointer to Key payload.
     */
    static inline Key* get_key(struct duo_hash_bucket* b) noexcept {
        return reinterpret_cast<Key*>(reinterpret_cast<char*>(b) + k_key_offset);
    }

    /**
     * @brief Computes const key pointer embedded inside a bucket.
     *
     * @param b Pointer to bucket.
     * @return Const pointer to Key payload.
     */
    static inline const Key* get_key(const struct duo_hash_bucket* b) noexcept {
        return reinterpret_cast<const Key*>(reinterpret_cast<const char*>(b) + k_key_offset);
    }

    /**
     * @brief Computes mutable value pointer embedded inside a bucket.
     *
     * @param b Pointer to bucket.
     * @return Pointer to Value payload.
     */
    static inline Value* get_val(struct duo_hash_bucket* b) noexcept {
        return reinterpret_cast<Value*>(reinterpret_cast<char*>(b) + k_val_offset);
    }

    /**
     * @brief Computes const value pointer embedded inside a bucket.
     *
     * @param b Pointer to bucket.
     * @return Const pointer to Value payload.
     */
    static inline const Value* get_val(const struct duo_hash_bucket* b) noexcept {
        return reinterpret_cast<const Value*>(reinterpret_cast<const char*>(b) + k_val_offset);
    }

    /**
     * @brief Accessor retrieving mutable bucket at slot index from active table.
     *
     * @param idx Bucket slot index.
     * @return Pointer to bucket header.
     */
    inline struct duo_hash_bucket* bucket_at(size_t idx) noexcept {
        return get_bucket(m_inner.raw->buckets, idx);
    }

    /**
     * @brief Accessor retrieving const bucket at slot index from active table.
     *
     * @param idx Bucket slot index.
     * @return Const pointer to bucket header.
     */
    inline const struct duo_hash_bucket* bucket_at(size_t idx) const noexcept {
        return get_bucket(m_inner.raw->buckets, idx);
    }

    /**
     * @brief Computes 48-bit clipped hash code for key using HashFn and table seeds.
     *
     * @param k Key reference.
     * @return 48-bit masked hash signature.
     */
    inline uint64_t calc_hash(const Key& k) const noexcept {
        return invoke_hash_fn(HashFn{}, k, m_inner.raw->seed0, m_inner.raw->seed1, 0) & 0xFFFFFFFFFFFFULL;
    }

    /**
     * @brief Internal reallocation and Robin Hood re-insertion algorithm for capacity changes.
     *
     * Allocates a new contiguous bucket array of size `new_cap`, re-hashes existing active entries,
     * resolving collisions using Robin Hood displacement (richer elements steal from poorer elements),
     * and frees the old bucket array.
     *
     * @param new_cap Target bucket capacity (power of two >= 16).
     * @return true on successful reallocation, false on memory allocation failure.
     */
    /**
     * @brief Destroys key and value payloads in a bucket slot and clears header.
     *
     * @param b Pointer to bucket.
     */
    static inline void destroy_bucket_payload(struct duo_hash_bucket* b) noexcept {
        if constexpr (!duo::is_trivially_destructible<Key>::value) {
            get_key(b)->~Key();
        }
        if constexpr (!duo::is_trivially_destructible<Value>::value && sizeof(Value) > 0) {
            get_val(b)->~Value();
        }
        b->dib  = 0;
        b->hash = 0;
    }

    /**
     * @brief Move-constructs key and value payload from src bucket into dest bucket.
     *
     * @param dest Destination bucket pointer.
     * @param src  Source bucket pointer.
     */
    static inline void move_bucket_payload(struct duo_hash_bucket* dest, struct duo_hash_bucket* src) noexcept {
        if constexpr (duo::is_trivially_copyable<Key>::value && duo::is_trivially_copyable<Value>::value) {
            DUO_MEMCPY(reinterpret_cast<char*>(dest) + k_key_offset,
                   reinterpret_cast<char*>(src) + k_key_offset,
                   sizeof(Key));
            DUO_MEMCPY(reinterpret_cast<char*>(dest) + k_val_offset,
                   reinterpret_cast<char*>(src) + k_val_offset,
                   sizeof(Value));
        } else {
            duo::construct_at(get_key(dest), duo::move(*get_key(src)));
            get_key(src)->~Key();
            if constexpr (sizeof(Value) > 0) {
                duo::construct_at(get_val(dest), duo::move(*get_val(src)));
                get_val(src)->~Value();
            }
        }
    }

    /**
     * @brief Inserts an entry into target bucket memory displacing richer entries using Robin Hood hashing.
     *
     * @tparam K Key forward reference type.
     * @tparam V Value forward reference type.
     * @param target_buckets Pointer to contiguous bucket memory.
     * @param target_mask    Bitmask (capacity - 1).
     * @param cur_hash       Hash code of entry.
     * @param cur_k          Key forward reference.
     * @param cur_v          Value forward reference.
     */
    template <typename K, typename V>
    static inline void insert_displace_into(void* target_buckets, size_t target_mask,
                                            uint64_t cur_hash, K&& cur_k, V&& cur_v) noexcept {
        uint16_t cur_dib = 1;
        if constexpr (duo::is_trivially_copyable<Key>::value && duo::is_trivially_copyable<Value>::value) {
            alignas(alignof(struct duo_hash_bucket)) unsigned char entry_buf[k_bucketsz];
            struct duo_hash_bucket* entry = reinterpret_cast<struct duo_hash_bucket*>(entry_buf);
            entry->hash = cur_hash;
            entry->dib  = 1;
            Key k_obj(duo::forward<K>(cur_k));
            Value v_obj(duo::forward<V>(cur_v));
            DUO_MEMCPY(reinterpret_cast<char*>(entry) + k_key_offset, &k_obj, sizeof(Key));
            DUO_MEMCPY(reinterpret_cast<char*>(entry) + k_val_offset, &v_obj, sizeof(Value));

            size_t i = cur_hash & target_mask;
            while (true) {
                struct duo_hash_bucket* b = get_bucket(target_buckets, i);
                if (b->dib == 0) {
                    DUO_MEMCPY(b, entry, k_bucketsz);
                    return;
                }
                if (b->dib < entry->dib) {
                    alignas(alignof(struct duo_hash_bucket)) unsigned char tmp[k_bucketsz];
                    DUO_MEMCPY(tmp, b, k_bucketsz);
                    DUO_MEMCPY(b, entry, k_bucketsz);
                    DUO_MEMCPY(entry, tmp, k_bucketsz);
                }
                i = (i + 1) & target_mask;
                entry->dib++;
            }
        } else {
            Key k_val(duo::forward<K>(cur_k));
            Value v_val(duo::forward<V>(cur_v));

            size_t i = cur_hash & target_mask;
            while (true) {
                struct duo_hash_bucket* b = get_bucket(target_buckets, i);
                if (b->dib == 0) {
                    b->hash = cur_hash;
                    b->dib  = cur_dib;
                    duo::construct_at(get_key(b), duo::move(k_val));
                    duo::construct_at(get_val(b), duo::move(v_val));
                    return;
                }
                if (b->dib < cur_dib) {
                    uint64_t tmp_hash = b->hash;
                    uint16_t tmp_dib  = b->dib;
                    b->hash = cur_hash;
                    b->dib  = cur_dib;
                    cur_hash = tmp_hash;
                    cur_dib  = tmp_dib;

                    Key tmp_k(duo::move(*get_key(b)));
                    get_key(b)->~Key();
                    duo::construct_at(get_key(b), duo::move(k_val));
                    k_val.~Key();
                    duo::construct_at(&k_val, duo::move(tmp_k));

                    Value tmp_v(duo::move(*get_val(b)));
                    get_val(b)->~Value();
                    duo::construct_at(get_val(b), duo::move(v_val));
                    v_val.~Value();
                    duo::construct_at(&v_val, duo::move(tmp_v));
                }
                i = (i + 1) & target_mask;
                cur_dib++;
            }
        }
    }

    /**
     * @brief Internal reallocation and Robin Hood re-insertion algorithm for capacity changes.
     *
     * @param new_cap Target bucket capacity (power of two >= 16).
     * @return true on successful reallocation, false on memory allocation failure.
     */
    bool rehash_impl(size_t new_cap) {
        if (!m_inner.raw) return false;
        size_t cap = 16;
        while (cap < new_cap) cap <<= 1;
        new_cap = cap;

        void* new_buckets = DUO_MALLOC(k_bucketsz * new_cap);
        if (!new_buckets) return false;
        DUO_MEMSET(new_buckets, 0, k_bucketsz * new_cap);

        size_t new_mask = new_cap - 1;
        size_t old_nbuckets = m_inner.raw->nbuckets;
        void* old_buckets = m_inner.raw->buckets;

        for (size_t i = 0; i < old_nbuckets; ++i) {
            struct duo_hash_bucket* old_b = get_bucket(old_buckets, i);
            if (old_b->dib == 0) continue;
            insert_displace_into(new_buckets, new_mask, old_b->hash,
                                 duo::move(*get_key(old_b)), duo::move(*get_val(old_b)));
            destroy_bucket_payload(old_b);
        }

        DUO_FREE(old_buckets);
        m_inner.raw->buckets  = new_buckets;
        m_inner.raw->nbuckets = new_cap;
        m_inner.raw->mask     = new_mask;
        m_inner.raw->growat   = static_cast<size_t>(new_cap * (m_inner.raw->loadfactor / 100.0));
        m_inner.raw->shrinkat = static_cast<size_t>(new_cap * DUO_HASHMAP_SHRINK_AT);
        return true;
    }

    /**
     * @brief Checks if table capacity threshold has been reached and grows capacity by power exponent if needed.
     *
     * @return true if capacity is sufficient or grow succeeded, false on OOM.
     */
    inline bool grow_if_needed() noexcept {
        if (m_inner.raw->count >= m_inner.raw->growat) {
            size_t new_cap = m_inner.raw->nbuckets * (static_cast<size_t>(1) << m_inner.raw->growpower);
            if (!rehash_impl(new_cap)) {
                m_inner.raw->oom = true;
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Internal Robin Hood insertion/assignment implementation with DIB displacement.
     *
     * @tparam K Key forward reference type.
     * @tparam V Value forward reference type.
     * @param k                 Key to insert or assign.
     * @param v                 Value to associate with key.
     * @param precomputed_hash  Optional precomputed 64-bit hash code.
     * @param has_hash          True if precomputed_hash is supplied, false to compute via calc_hash.
     * @return true on successful insertion/update, false on memory allocation failure.
     */
    template <typename K, typename V>
    inline bool insert_or_assign_impl(K&& k, V&& v, uint64_t precomputed_hash = 0, bool has_hash = false) {
        if (!m_inner.raw && !init()) return false;
        uint64_t hash = has_hash ? duo_hashmap_clip_hash(precomputed_hash) : calc_hash(k);

        // In-place update if key exists
        if (m_inner.raw->count > 0) {
            size_t i = hash & m_inner.raw->mask;
            while (true) {
                struct duo_hash_bucket* b = bucket_at(i);
                if (b->dib == 0) break;
                if (b->hash == hash && EqualFn{}(k, *get_key(b))) {
                    if constexpr (!duo::is_trivially_destructible<Value>::value) {
                        duo::destroy_at(get_val(b));
                    }
                    duo::construct_at(get_val(b), duo::forward<V>(v));
                    return true;
                }
                i = (i + 1) & m_inner.raw->mask;
            }
        }

        if (!grow_if_needed()) return false;

        insert_displace_into(m_inner.raw->buckets, m_inner.raw->mask, hash,
                             duo::forward<K>(k), duo::forward<V>(v));
        m_inner.raw->count++;
        return true;
    }

    /**
     * @brief Performs backward-shift deletion starting from slot index i.
     *
     * Shifts occupied buckets backwards until an empty bucket or bucket with DIB <= 1 is found.
     *
     * @param i Starting slot index where an entry was removed.
     */
    inline void backward_shift(size_t i) noexcept {
        struct duo_hash_bucket* b = bucket_at(i);
        while (true) {
            size_t next_i = (i + 1) & m_inner.raw->mask;
            struct duo_hash_bucket* next_b = bucket_at(next_i);
            if (next_b->dib <= 1) {
                b->dib  = 0;
                b->hash = 0;
                break;
            }

            b->hash = next_b->hash;
            b->dib  = next_b->dib - 1;
            move_bucket_payload(b, next_b);

            i = next_i;
            b = next_b;
        }
    }

    /**
     * @brief Shrinks bucket capacity by half if element count falls below shrink threshold.
     */
    inline void shrink_if_needed() noexcept {
        if (m_inner.raw->nbuckets > m_inner.raw->cap && m_inner.raw->count <= m_inner.raw->shrinkat) {
            rehash_impl(m_inner.raw->nbuckets / 2);
        }
    }

    /**
     * @brief Internal extraction implementation transferring element ownership without destruction.
     *
     * Transfers key and/or value out to caller destinations, then applies backward-shift deletion.
     *
     * @param key                Key to extract.
     * @param out_val            Optional destination pointer to receive moved value.
     * @param out_key            Optional destination pointer to receive moved key.
     * @param precomputed_hash   Optional precomputed 64-bit hash.
     * @param has_hash           True if precomputed_hash is supplied, false to compute via calc_hash.
     * @return true if entry was found and extracted, false otherwise.
     */
    inline bool extract_impl(const Key& key, Value* out_val = nullptr, Key* out_key = nullptr, uint64_t precomputed_hash = 0, bool has_hash = false) noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return false;
        uint64_t hash = has_hash ? duo_hashmap_clip_hash(precomputed_hash) : calc_hash(key);
        size_t i = hash & m_inner.raw->mask;

        while (true) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return false;
            if (b->hash == hash && EqualFn{}(key, *get_key(b))) {
                if (out_key) {
                    *out_key = duo::move(*get_key(b));
                }
                if (out_val && sizeof(Value) > 0) {
                    *out_val = duo::move(*get_val(b));
                }
                destroy_bucket_payload(b);
                m_inner.raw->count--;

                backward_shift(i);
                shrink_if_needed();
                return true;
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Internal backward-shift deletion without tombstones for key removal.
     *
     * Delegates directly to extract_impl without out-pointers, avoiding duplicate backward-shift logic.
     *
     * @param key                Key to locate and erase.
     * @param precomputed_hash   Optional precomputed 64-bit hash.
     * @param has_hash           True if precomputed_hash is supplied, false to compute via calc_hash.
     * @return true if key was found and removed, false if not present.
     */
    inline bool erase_impl(const Key& key, uint64_t precomputed_hash = 0, bool has_hash = false) noexcept {
        return extract_impl(key, nullptr, nullptr, precomputed_hash, has_hash);
    }

    /**
     * @brief Resets bucket buffer allocation and table metadata to clean state.
     *
     * @param update_cap If true, keeps current capacity; if false, shrinks back to initial capacity.
     */
    inline void reset_table_buffers(bool update_cap) noexcept {
        m_inner.raw->count = 0;
        if (update_cap) {
            m_inner.raw->cap = m_inner.raw->nbuckets;
        } else if (m_inner.raw->nbuckets != m_inner.raw->cap) {
            void* new_buckets = DUO_MALLOC(k_bucketsz * m_inner.raw->cap);
            if (new_buckets) {
                DUO_FREE(m_inner.raw->buckets);
                m_inner.raw->buckets = new_buckets;
                m_inner.raw->nbuckets = m_inner.raw->cap;
            }
        }
        DUO_MEMSET(m_inner.raw->buckets, 0, k_bucketsz * m_inner.raw->nbuckets);
        m_inner.raw->mask = m_inner.raw->nbuckets - 1;
        m_inner.raw->growat = static_cast<size_t>(m_inner.raw->nbuckets * (m_inner.raw->loadfactor / 100.0));
        m_inner.raw->shrinkat = static_cast<size_t>(m_inner.raw->nbuckets * DUO_HASHMAP_SHRINK_AT);
    }

    /**
     * @brief Internal clear implementation destroying active elements and resetting table buffers.
     *
     * @param update_cap If true, resets base capacity to current capacity; if false, reallocates initial capacity.
     */
    inline void clear_impl(bool update_cap = false) noexcept {
        if (!m_inner.raw) return;
        for (size_t i = 0; i < m_inner.raw->nbuckets; ++i) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib != 0) {
                destroy_bucket_payload(b);
            }
        }
        reset_table_buffers(update_cap);
    }

    /**
     * @brief Calculates shrink target capacity and rehashes table after bulk deletions.
     */
    inline void shrink_to_target_cap() noexcept {
        size_t target_cap = m_inner.raw->nbuckets;
        while (target_cap > m_inner.raw->cap && m_inner.raw->count <= static_cast<size_t>(target_cap * DUO_HASHMAP_SHRINK_AT)) {
            target_cap /= 2;
        }
        if (target_cap < m_inner.raw->cap) {
            target_cap = m_inner.raw->cap;
        }
        rehash_impl(target_cap);
    }

    /**
     * @brief Internal atomic predicate filter scanning and removing elements matching predicate.
     *
     * Resolves upstream tidwall/hashmap.c Issue #41 by invalidating matching buckets
     * and performing a single rehash/re-pack pass to maintain Robin Hood invariant.
     *
     * @tparam Predicate Callable accepting `(const Key&, Value&)`.
     * @param pred Filter predicate returning true to remove, false to keep.
     * @return Count of removed elements.
     */
    template <typename Predicate>
    inline size_t filter_impl(Predicate&& pred) {
        if (!m_inner.raw || m_inner.raw->count == 0) return 0;
        size_t removed = 0;
        for (size_t i = 0; i < m_inner.raw->nbuckets; ++i) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib != 0) {
                if (pred(*get_key(b), *get_val(b))) {
                    destroy_bucket_payload(b);
                    removed++;
                }
            }
        }
        if (removed == 0) return 0;

        m_inner.raw->count -= removed;
        if (m_inner.raw->count == 0) {
            clear_impl(false);
            return removed;
        }

        shrink_to_target_cap();
        return removed;
    }

public:
    // ------------------------------------------------------------------------
    // Iterator Suite
    // ------------------------------------------------------------------------
    /**
     * @class const_iterator
     * @brief Immutable forward iterator scanning non-empty buckets of HashMap.
     */
    class const_iterator {
    protected:
        const duo_hashmap_t* m_map{nullptr}; /**< Non-owning pointer to map table. */
        size_t               m_index{0};     /**< Current bucket slot index. */

        /**
         * @brief Advances cursor to next occupied bucket (dib != 0) or table end.
         */
        void advance_to_valid() noexcept {
            if (!m_map) return;
            while (m_index < m_map->nbuckets) {
                struct duo_hash_bucket* b = duo_hashmap_bucket_at(m_map, m_index);
                if (b->dib != 0) {
                    return;
                }
                m_index++;
            }
        }

    public:
        using iterator_category = forward_iterator_tag;
        using value_type        = ConstEntryRef<Key, Value>;
        using difference_type   = ptrdiff_t;

        /** @brief Default constructor creating an unattached end iterator. */
        inline const_iterator() noexcept = default;

        /**
         * @brief Parameterized constructor initializing iterator at specified bucket slot index.
         *
         * @param map Pointer to hash map instance.
         * @param idx Starting bucket index.
         */
        inline const_iterator(const duo_hashmap_t* map, size_t idx) noexcept 
            : m_map(map), m_index(idx) {
            advance_to_valid();
        }

        /** @brief Returns const reference to key of current entry. */
        inline const Key& key() const noexcept {
            struct duo_hash_bucket* b = duo_hashmap_bucket_at(m_map, m_index);
            return *static_cast<const Key*>(duo_hashmap_bucket_key_const(m_map, b));
        }

        /** @brief Returns const reference to value of current entry. */
        inline const Value& value() const noexcept {
            struct duo_hash_bucket* b = duo_hashmap_bucket_at(m_map, m_index);
            return *static_cast<const Value*>(duo_hashmap_bucket_val_const(m_map, b));
        }

        /**
         * @brief Dereferences iterator to produce ConstEntryRef proxy for structured bindings.
         *
         * @return ConstEntryRef aggregate containing immutable key and value references.
         */
        inline ConstEntryRef<Key, Value> operator*() const noexcept {
            return ConstEntryRef<Key, Value>{ key(), value() };
        }

        /** @brief Prefix increment advancing to next occupied bucket. */
        inline const_iterator& operator++() noexcept {
            if (m_map && m_index < m_map->nbuckets) {
                m_index++;
                advance_to_valid();
            }
            return *this;
        }

        /** @brief Postfix increment advancing to next occupied bucket. */
        inline const_iterator operator++(int) noexcept {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        /** @brief Equality comparison testing if two iterators point to same bucket or are both at end. */
        inline bool operator==(const const_iterator& o) const noexcept {
            size_t cap_a = m_map ? m_map->nbuckets : 0;
            size_t cap_b = o.m_map ? o.m_map->nbuckets : 0;
            bool end_a = (!m_map || m_index >= cap_a);
            bool end_b = (!o.m_map || o.m_index >= cap_b);
            if (end_a && end_b) return true;
            return m_map == o.m_map && m_index == o.m_index;
        }

        /** @brief Inequality comparison testing whether two iterators differ. */
        inline bool operator!=(const const_iterator& o) const noexcept {
            return !(*this == o);
        }
    };

    /**
     * @class iterator
     * @brief Mutable forward iterator scanning non-empty buckets of HashMap.
     */
    class iterator : public const_iterator {
    public:
        using value_type = EntryRef<Key, Value>;

        /** @brief Default constructor creating an unattached end iterator. */
        inline iterator() noexcept = default;

        /**
         * @brief Parameterized constructor initializing iterator at specified bucket slot index.
         *
         * @param map Pointer to hash map instance.
         * @param idx Starting bucket index.
         */
        inline iterator(duo_hashmap_t* map, size_t idx) noexcept 
            : const_iterator(map, idx) {}

        /** @brief Returns mutable reference to value of current entry. */
        inline Value& value() const noexcept {
            struct duo_hash_bucket* b = duo_hashmap_bucket_at(this->m_map, this->m_index);
            return *static_cast<Value*>(duo_hashmap_bucket_val(const_cast<duo_hashmap_t*>(this->m_map), b));
        }

        /**
         * @brief Dereferences iterator to produce mutable EntryRef proxy for structured bindings.
         *
         * @return EntryRef aggregate containing immutable key and mutable value reference.
         */
        inline EntryRef<Key, Value> operator*() const noexcept {
            return EntryRef<Key, Value>{ this->key(), value() };
        }

        /** @brief Prefix increment advancing to next occupied bucket. */
        inline iterator& operator++() noexcept {
            const_iterator::operator++();
            return *this;
        }

        /** @brief Postfix increment advancing to next occupied bucket. */
        inline iterator operator++(int) noexcept {
            iterator tmp = *this;
            const_iterator::operator++();
            return tmp;
        }
    };

    // ------------------------------------------------------------------------
    // Constructors & Destructor
    // ------------------------------------------------------------------------

    /**
     * @brief Constructs an empty hash map without allocating bucket memory.
     */
    inline HashMap() noexcept : m_inner{nullptr} {}

    /**
     * @brief Constructs a hash map with initial capacity and optional 64-bit seeds (matching duo_hashmap_new).
     *
     * @param initial_cap Initial bucket capacity hint (default 16).
     * @param seed0       Primary 64-bit hash seed.
     * @param seed1       Secondary 64-bit hash seed.
     */
    inline explicit HashMap(size_t initial_cap, uint64_t seed0 = 0, uint64_t seed1 = 0) noexcept {
        init(initial_cap, seed0, seed1);
    }


    /**
     * @brief Destroys all stored entries and releases table memory (matching duo_hashmap_free).
     */
    inline ~HashMap() noexcept {
        if (m_inner.raw) {
            duo_hashmap_free(m_inner.raw);
            m_inner.raw = nullptr;
        }
    }

    // Move Semantics

    /**
     * @brief Move constructor transfers ownership of table handle without memory allocations.
     */
    inline HashMap(HashMap&& other) noexcept : m_inner{other.m_inner.raw} {
        other.m_inner.raw = nullptr;
    }

    /**
     * @brief Move assignment operator transfers table ownership, freeing existing elements.
     */
    inline HashMap& operator=(HashMap&& other) noexcept {
        if (this != &other) {
            if (m_inner.raw) {
                duo_hashmap_free(m_inner.raw);
            }
            m_inner.raw = other.m_inner.raw;
            other.m_inner.raw = nullptr;
        }
        return *this;
    }

    // Copy Semantics

    /**
     * @brief Deep copy constructor cloning all active key-value entries.
     */
    inline HashMap(const HashMap& other) : m_inner{nullptr} {
        if (other.m_inner.raw) {
            init(other.m_inner.raw->cap, other.m_inner.raw->seed0, other.m_inner.raw->seed1);
            for (const auto [k, v] : other) {
                insert(k, v);
            }
        }
    }

    /**
     * @brief Deep copy assignment operator using copy-and-swap idiom.
     */
    inline HashMap& operator=(const HashMap& other) {
        if (this != &other) {
            HashMap tmp(other);
            swap(tmp);
        }
        return *this;
    }

    // ------------------------------------------------------------------------
    // Table Initialization & Lifecycle
    // ------------------------------------------------------------------------

    /**
     * @brief Initializes table memory with given capacity and seeds (matching duo_hashmap_new).
     *
     * @param cap   Capacity hint (rounded up to power-of-two >= 16).
     * @param seed0 Primary 64-bit hash seed.
     * @param seed1 Secondary 64-bit hash seed.
     * @return true on successful allocation, false on OOM.
     */
    inline bool init(size_t cap = 16, uint64_t seed0 = 0, uint64_t seed1 = 0) noexcept {
        if (m_inner.raw) {
            duo_hashmap_free(m_inner.raw);
            m_inner.raw = nullptr;
        }
        void (*k_free)(void*) = duo::is_trivially_destructible<Key>::value ? nullptr : &key_destructor_trampoline;
        void (*v_free)(void*) = duo::is_trivially_destructible<Value>::value ? nullptr : &val_destructor_trampoline;

        m_inner.raw = duo_hashmap_new(
            sizeof(Key),
            sizeof(Value),
            cap,
            seed0,
            seed1,
            &hash_trampoline,
            &compare_trampoline,
            k_free,
            v_free,
            nullptr
        );
        return m_inner.raw != nullptr;
    }


    // ------------------------------------------------------------------------
    // Insertion & Modification
    // ------------------------------------------------------------------------

    /**
     * @brief Inserts or assigns key-value pair, updating value if key already exists (matching duo_hashmap_set).
     *
     * @param k Key forward reference.
     * @param v Value forward reference.
     * @return true on success, false on OOM.
     */
    template <typename K, typename V>
    inline bool insert_or_assign(K&& k, V&& v) {
        return insert_or_assign_impl(duo::forward<K>(k), duo::forward<V>(v));
    }

    /**
     * @brief Inserts or updates an entry using a precomputed 64-bit hash.
     *
     * @param k    Key forward reference.
     * @param v    Value forward reference.
     * @param hash Precomputed 64-bit hash.
     * @return true on success, false on OOM.
     */
    template <typename K, typename V>
    inline bool insert_or_assign_with_hash(K&& k, V&& v, uint64_t hash) {
        return insert_or_assign_impl(duo::forward<K>(k), duo::forward<V>(v), hash, true);
    }

    /**
     * @brief Inserts key-value pair into map (semantic alias for insert_or_assign).
     */
    inline bool insert(const Key& key, const Value& val) {
        return insert_or_assign_impl(key, val);
    }

    /**
     * @brief Inserts key-value pair with moved value into map.
     */
    inline bool insert(const Key& key, Value&& val) {
        return insert_or_assign_impl(key, duo::move(val));
    }

    /**
     * @brief Inserts key-value pair with moved key into map.
     */
    inline bool insert(Key&& key, const Value& val) {
        return insert_or_assign_impl(duo::move(key), val);
    }

    /**
     * @brief Inserts key-value pair with moved key and value into map.
     */
    inline bool insert(Key&& key, Value&& val) {
        return insert_or_assign_impl(duo::move(key), duo::move(val));
    }

    /**
     * @brief Inserts or updates an entry using a precomputed 64-bit hash.
     *
     * @param key  Key to insert or update.
     * @param val  Value to associate with key.
     * @param hash Precomputed 64-bit hash.
     * @return true on success, false on OOM.
     */
    inline bool insert_with_hash(const Key& key, const Value& val, uint64_t hash) {
        return insert_or_assign_impl(key, val, hash, true);
    }

    /**
     * @brief Inserts or updates a Key-Value pair (semantic alias for insert_or_assign matching pure C duo_hashmap_set).
     *
     * @param k Key forward reference.
     * @param v Value forward reference.
     * @return true on success, false on OOM.
     */
    template <typename K, typename V>
    inline bool set(K&& k, V&& v) {
        return insert_or_assign(duo::forward<K>(k), duo::forward<V>(v));
    }

    /**
     * @brief Inserts or updates a Key-Value pair using precomputed hash (matching pure C duo_hashmap_set_with_hash).
     *
     * @param k    Key forward reference.
     * @param v    Value forward reference.
     * @param hash Precomputed 64-bit hash.
     * @return true on success, false on OOM.
     */
    template <typename K, typename V>
    inline bool set_with_hash(K&& k, V&& v, uint64_t hash) {
        return insert_or_assign_with_hash(duo::forward<K>(k), duo::forward<V>(v), hash);
    }

    /**
     * @brief In-place constructs value associated with key.
     */
    template <typename... Args>
    inline bool emplace(const Key& key, Args&&... args) {
        return insert_or_assign_impl(key, Value(duo::forward<Args>(args)...));
    }

    /**
     * @brief In-place constructs value associated with moved key.
     */
    template <typename... Args>
    inline bool emplace(Key&& key, Args&&... args) {
        return insert_or_assign_impl(duo::move(key), Value(duo::forward<Args>(args)...));
    }

    /**
     * @brief Subscript operator returning reference to value for key, inserting default value if missing.
     *
     * @param key Key to find or insert.
     * @return Mutable reference to associated value.
     */
    inline Value& operator[](const Key& key) {
        if (!m_inner.raw) init();
        Value* existing = get(key);
        if (existing) return *existing;
        insert_or_assign_impl(key, Value{});
        return *get(key);
    }

    /**
     * @brief Subscript operator with moved key, inserting default value if missing.
     *
     * @param key Rvalue key to find or insert.
     * @return Mutable reference to associated value.
     */
    inline Value& operator[](Key&& key) {
        if (!m_inner.raw) init();
        Value* existing = get(key);
        if (existing) return *existing;
        Key k_copy = key;
        insert_or_assign_impl(duo::move(key), Value{});
        return *get(k_copy);
    }

    // ------------------------------------------------------------------------
    // Lookup & Inspection
    // ------------------------------------------------------------------------

    /**
     * @brief Retrieves a mutable pointer to the value associated with key (matching pure C duo_hashmap_get).
     *
     * @param key Key to search for.
     * @return Mutable pointer to stored value, or nullptr if not found.
     */
    inline Value* get(const Key& key) noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        uint64_t hash = calc_hash(key);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return nullptr;
            if (b->hash == hash && EqualFn{}(key, *get_key(b))) {
                return get_val(b);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Retrieves an immutable pointer to the value associated with key (matching pure C duo_hashmap_get_const).
     *
     * @param key Key to search for.
     * @return Const pointer to stored value, or nullptr if not found.
     */
    inline const Value* get(const Key& key) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        uint64_t hash = calc_hash(key);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            const struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return nullptr;
            if (b->hash == hash && EqualFn{}(key, *get_key(b))) {
                return get_val(b);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Retrieves a mutable pointer to the value matching key using a precomputed hash.
     *
     * @param key  Key to search for.
     * @param hash Precomputed 64-bit hash.
     * @return Mutable pointer to stored value, or nullptr if not found.
     */
    inline Value* get_with_hash(const Key& key, uint64_t hash) noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        hash = duo_hashmap_clip_hash(hash);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return nullptr;
            if (b->hash == hash && EqualFn{}(key, *get_key(b))) {
                return get_val(b);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Retrieves an immutable pointer to the value matching key using a precomputed hash.
     *
     * @param key  Key to search for.
     * @param hash Precomputed 64-bit hash.
     * @return Const pointer to stored value, or nullptr if not found.
     */
    inline const Value* get_with_hash(const Key& key, uint64_t hash) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        hash = duo_hashmap_clip_hash(hash);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            const struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return nullptr;
            if (b->hash == hash && EqualFn{}(key, *get_key(b))) {
                return get_val(b);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Retrieves an immutable pointer to value associated with key (semantic alias matching duo_hashmap_get_const).
     */
    inline const Value* get_const(const Key& key) const noexcept {
        return get(key);
    }

    /**
     * @brief Retrieves an immutable pointer using precomputed hash.
     */
    inline const Value* get_const_with_hash(const Key& key, uint64_t hash) const noexcept {
        return get_with_hash(key, hash);
    }

    /**
     * @brief Copies stored value for key into destination buffer out (matching pure C duo_hashmap_get_copy).
     *
     * @param key Key to search for.
     * @param out Destination buffer to receive copy of value.
     * @return true if key found and copied, false otherwise.
     */
    inline bool get_copy(const Key& key, Value& out) const noexcept {
        const Value* v = get(key);
        if (!v) return false;
        out = *v;
        return true;
    }

    /**
     * @brief Tests whether key exists in the hash map (matching pure C duo_hashmap_contains).
     *
     * @param key Key to search for.
     * @return true if key exists, false otherwise.
     */
    inline bool contains(const Key& key) const noexcept {
        return get(key) != nullptr;
    }

    /**
     * @brief Tests whether key exists in the hash map using a precomputed 64-bit hash.
     *
     * @param key  Key to search for.
     * @param hash Precomputed 64-bit hash.
     * @return true if key exists, false otherwise.
     */
    inline bool contains_with_hash(const Key& key, uint64_t hash) const noexcept {
        return get_with_hash(key, hash) != nullptr;
    }

    /**
     * @brief Returns number of elements matching key (0 or 1 for associative map).
     *
     * @param key Key to count.
     * @return 1 if found, 0 otherwise.
     */
    inline size_t count(const Key& key) const noexcept {
        return contains(key) ? 1 : 0;
    }

    /**
     * @brief Searches for an entry matching key, returning a mutable iterator.
     *
     * @param key Key to search for.
     * @return Iterator to matching entry, or end() if not found.
     */
    inline iterator find(const Key& key) noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return end();
        uint64_t hash = calc_hash(key);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return end();
            if (b->hash == hash && EqualFn{}(key, *get_key(b))) {
                return iterator(m_inner.raw, i);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Searches for an entry matching key, returning an immutable const iterator.
     *
     * @param key Key to search for.
     * @return Const iterator to matching entry, or cend() if not found.
     */
    inline const_iterator find(const Key& key) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return cend();
        uint64_t hash = calc_hash(key);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            const struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return cend();
            if (b->hash == hash && EqualFn{}(key, *get_key(b))) {
                return const_iterator(m_inner.raw, i);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Finds an entry by key using a precomputed hash.
     *
     * @param key  Key to search for.
     * @param hash Precomputed 64-bit hash.
     * @return Iterator to matching entry, or end() if not found.
     */
    inline iterator find_with_hash(const Key& key, uint64_t hash) noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return end();
        hash = duo_hashmap_clip_hash(hash);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return end();
            if (b->hash == hash && EqualFn{}(key, *get_key(b))) {
                return iterator(m_inner.raw, i);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Finds an entry by key using a precomputed hash (const overload).
     *
     * @param key  Key to search for.
     * @param hash Precomputed 64-bit hash.
     * @return Const iterator to matching entry, or cend() if not found.
     */
    inline const_iterator find_with_hash(const Key& key, uint64_t hash) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return cend();
        hash = duo_hashmap_clip_hash(hash);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            const struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return cend();
            if (b->hash == hash && EqualFn{}(key, *get_key(b))) {
                return const_iterator(m_inner.raw, i);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Returns a mutable pointer to the value at bucket index modulo capacity (matching pure C duo_hashmap_probe).
     *
     * @param index Bucket index (modulo table capacity).
     * @return Mutable pointer to stored value, or nullptr if bucket is empty.
     */
    inline Value* probe(size_t index) noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        size_t i = index & m_inner.raw->mask;
        struct duo_hash_bucket* b = bucket_at(i);
        return b->dib != 0 ? get_val(b) : nullptr;
    }

    /**
     * @brief Returns an immutable pointer to the value at bucket index modulo capacity (matching pure C duo_hashmap_probe_const).
     *
     * @param index Bucket index (modulo table capacity).
     * @return Const pointer to stored value, or nullptr if bucket is empty.
     */
    inline const Value* probe(size_t index) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        size_t i = index & m_inner.raw->mask;
        const struct duo_hash_bucket* b = bucket_at(i);
        return b->dib != 0 ? get_val(b) : nullptr;
    }

    /**
     * @brief Const overload of probe returning pointer to value at slot index.
     */
    inline const Value* probe_const(size_t index) const noexcept {
        return probe(index);
    }

    /**
     * @brief Returns an immutable pointer to the key at bucket index modulo capacity.
     *
     * @param index Bucket index (modulo table capacity).
     * @return Const pointer to key in bucket, or nullptr if bucket is empty.
     */
    inline const Key* probe_key(size_t index) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        size_t i = index & m_inner.raw->mask;
        const struct duo_hash_bucket* b = bucket_at(i);
        return b->dib != 0 ? get_key(b) : nullptr;
    }

    /**
     * @brief Const overload of probe_key returning pointer to key at slot index.
     */
    inline const Key* probe_key_const(size_t index) const noexcept {
        return probe_key(index);
    }

    /**
     * @brief Direct pointer to raw bucket at slot index (matching pure C duo_hashmap_bucket_at).
     *
     * @param index Bucket slot index.
     * @return Pointer to bucket at index, or nullptr if table uninitialized.
     */
    inline struct duo_hash_bucket* bucket_at_index(size_t index) noexcept {
        return m_inner.raw ? duo_hashmap_bucket_at(m_inner.raw, index) : nullptr;
    }

    /**
     * @brief Direct pointer to raw bucket at slot index (const overload matching duo_hashmap_bucket_at).
     *
     * @param index Bucket slot index.
     * @return Const pointer to bucket at index, or nullptr if table uninitialized.
     */
    inline const struct duo_hash_bucket* bucket_at_index(size_t index) const noexcept {
        return m_inner.raw ? duo_hashmap_bucket_at(m_inner.raw, index) : nullptr;
    }

    /**
     * @brief Retrieves key pointer from a raw bucket (matching pure C duo_hashmap_bucket_key).
     *
     * @param b Raw bucket pointer.
     * @return Pointer to key inside bucket, or nullptr if b is null.
     */
    inline Key* bucket_key(struct duo_hash_bucket* b) noexcept {
        return b ? get_key(b) : nullptr;
    }

    /**
     * @brief Retrieves const key pointer from a raw bucket (const overload matching duo_hashmap_bucket_key).
     *
     * @param b Raw bucket pointer.
     * @return Const pointer to key inside bucket, or nullptr if b is null.
     */
    inline const Key* bucket_key(const struct duo_hash_bucket* b) const noexcept {
        return b ? get_key(b) : nullptr;
    }

    /**
     * @brief Retrieves const key pointer from a raw bucket (matching pure C duo_hashmap_bucket_key_const).
     *
     * @param b Raw bucket pointer.
     * @return Const pointer to key inside bucket, or nullptr if b is null.
     */
    inline const Key* bucket_key_const(const struct duo_hash_bucket* b) const noexcept {
        return b ? get_key(b) : nullptr;
    }

    /**
     * @brief Retrieves value pointer from a raw bucket (matching pure C duo_hashmap_bucket_val).
     *
     * @param b Raw bucket pointer.
     * @return Pointer to value inside bucket, or nullptr if b is null.
     */
    inline Value* bucket_val(struct duo_hash_bucket* b) noexcept {
        return b ? get_val(b) : nullptr;
    }

    /**
     * @brief Retrieves const value pointer from a raw bucket (const overload matching duo_hashmap_bucket_val).
     *
     * @param b Raw bucket pointer.
     * @return Const pointer to value inside bucket, or nullptr if b is null.
     */
    inline const Value* bucket_val(const struct duo_hash_bucket* b) const noexcept {
        return b ? get_val(b) : nullptr;
    }

    /**
     * @brief Retrieves const value pointer from a raw bucket (matching pure C duo_hashmap_bucket_val_const).
     *
     * @param b Raw bucket pointer.
     * @return Const pointer to value inside bucket, or nullptr if b is null.
     */
    inline const Value* bucket_val_const(const struct duo_hash_bucket* b) const noexcept {
        return b ? get_val(b) : nullptr;
    }

    // ------------------------------------------------------------------------
    // Erasure & Clearing
    // ------------------------------------------------------------------------

    /**
     * @brief Removes an entry matching key using backward-shift deletion (matching pure C duo_hashmap_delete).
     *
     * @param key Key to remove.
     * @return true if key was found and removed, false if not found.
     */
    inline bool erase(const Key& key) noexcept {
        return erase_impl(key);
    }

    /**
     * @brief Removes an entry matching key using a precomputed 64-bit hash (matching pure C duo_hashmap_delete_with_hash).
     *
     * @param key  Key to remove.
     * @param hash Precomputed 64-bit hash.
     * @return true if found and removed, false if not found.
     */
    inline bool erase_with_hash(const Key& key, uint64_t hash) noexcept {
        return erase_impl(key, hash, true);
    }

    /**
     * @brief Removes an entry matching key (semantic alias for erase matching pure C duo_hashmap_delete).
     */
    inline bool delete_key(const Key& key) noexcept {
        return erase(key);
    }

    /**
     * @brief Removes an entry matching key using a precomputed hash (matching pure C duo_hashmap_delete_with_hash).
     */
    inline bool delete_with_hash(const Key& key, uint64_t hash) noexcept {
        return erase_with_hash(key, hash);
    }

    /**
     * @brief Extracts an entry, transferring key and/or value out to caller destinations (matching pure C duo_hashmap_extract).
     *
     * @param key     Key to extract.
     * @param out_val Optional destination to move value into.
     * @param out_key Optional destination to move key into.
     * @return true if found and extracted, false otherwise.
     */
    inline bool extract(const Key& key, Value* out_val = nullptr, Key* out_key = nullptr) noexcept {
        return extract_impl(key, out_val, out_key, 0, false);
    }

    /**
     * @brief Extracts value associated with key, transferring ownership to out_val.
     */
    inline bool extract(const Key& key, Value& out_val) noexcept {
        return extract_impl(key, &out_val, nullptr, 0, false);
    }

    /**
     * @brief Extracts an entry using a precomputed hash without invoking destructors (matching pure C duo_hashmap_extract_with_hash).
     */
    inline bool extract_with_hash(const Key& key, uint64_t hash, Value* out_val = nullptr, Key* out_key = nullptr) noexcept {
        return extract_impl(key, out_val, out_key, hash, true);
    }

    /**
     * @brief Extracts value associated with key using a precomputed hash.
     */
    inline bool extract_with_hash(const Key& key, uint64_t hash, Value& out_val) noexcept {
        return extract_impl(key, &out_val, nullptr, hash, true);
    }

    /**
     * @brief Erases all entries, optionally resetting capacity to default (matching pure C duo_hashmap_clear).
     *
     * @param update_cap If true, shrinks capacity to initial table size; if false, retains bucket allocation.
     */
    inline void clear(bool update_cap = false) noexcept {
        clear_impl(update_cap);
    }

    // ------------------------------------------------------------------------
    // Issue #41 Predicate Filter
    // ------------------------------------------------------------------------

    /**
     * @brief Atomically filters map in-place using predicate callback (matching pure C duo_hashmap_filter).
     *
     * Removes all entries for which pred(key, val) returns true, re-packing survivors in a single pass.
     *
     * @param pred Callable returning true to delete, false to retain.
     * @return Total count of deleted entries.
     */
    template <typename Predicate>
    inline size_t filter(Predicate&& pred) {
        return filter_impl(duo::forward<Predicate>(pred));
    }

    // ------------------------------------------------------------------------
    // Iteration & Higher-Order Scans
    // ------------------------------------------------------------------------

    /**
     * @brief Iterates over all active entries, invoking callback on each key-value pair.
     *
     * @param cb Callable accepting `(const Key& key, Value& val)`. Aborts iteration if returning false.
     * @return true if entire table was traversed, false if aborted early.
     */
    template <typename Callback>
    inline bool for_each(Callback&& cb) {
        if (!m_inner.raw) return true;
        for (auto it = begin(); it != end(); ++it) {
            if (!cb(it.key(), it.value())) return false;
        }
        return true;
    }

    /**
     * @brief Iterates over all active entries, invoking callback on each const key-value pair.
     *
     * @param cb Callable accepting `(const Key& key, const Value& val)`. Aborts iteration if returning false.
     * @return true if entire table was traversed, false if aborted early.
     */
    template <typename Callback>
    inline bool for_each(Callback&& cb) const {
        if (!m_inner.raw) return true;
        for (auto it = cbegin(); it != cend(); ++it) {
            if (!cb(it.key(), it.value())) return false;
        }
        return true;
    }

    /**
     * @brief Higher-order scan alias executing callback on all active entries (matching pure C duo_hashmap_scan).
     *
     * @param cb Callable accepting `(const Key& key, Value& val)`.
     * @return true if all items were visited, false if iteration was terminated early.
     */
    template <typename Callback>
    inline bool scan(Callback&& cb) {
        return for_each(duo::forward<Callback>(cb));
    }

    /**
     * @brief Higher-order scan alias executing callback on all active entries (const overload).
     *
     * @param cb Callable accepting `(const Key& key, const Value& val)`.
     * @return true if all items were visited, false if iteration was terminated early.
     */
    template <typename Callback>
    inline bool scan(Callback&& cb) const {
        return for_each(duo::forward<Callback>(cb));
    }

    /**
     * @brief Cursor-based step-by-step element iterator matching pure C duo_hashmap_iter.
     *
     * Caller initializes `size_t i = 0` before first call.
     *
     * @param i     Cursor index (incremented across calls).
     * @param out_k Receives pointer to yielded key.
     * @param out_v Receives mutable pointer to yielded value.
     * @return true if entry yielded, false if iteration complete.
     */
    inline bool iter(size_t& i, const Key*& out_k, Value*& out_v) noexcept {
        if (!m_inner.raw) return false;
        void* k_ptr = nullptr;
        void* v_ptr = nullptr;
        if (duo_hashmap_iter(m_inner.raw, &i, &k_ptr, &v_ptr)) {
            out_k = static_cast<const Key*>(k_ptr);
            out_v = static_cast<Value*>(v_ptr);
            return true;
        }
        return false;
    }

    /**
     * @brief Const overload of iter yielding immutable key and value pointers.
     */
    inline bool iter(size_t& i, const Key*& out_k, const Value*& out_v) const noexcept {
        if (!m_inner.raw) return false;
        void* k_ptr = nullptr;
        void* v_ptr = nullptr;
        if (duo_hashmap_iter(const_cast<duo_hashmap_t*>(m_inner.raw), &i, &k_ptr, &v_ptr)) {
            out_k = static_cast<const Key*>(k_ptr);
            out_v = static_cast<const Value*>(v_ptr);
            return true;
        }
        return false;
    }

    /**
     * @brief Returns a mutable iterator pointing to first occupied bucket slot.
     *
     * @return iterator pointing to first valid entry or end().
     */
    inline iterator begin() noexcept             { return iterator(m_inner.raw, 0); }

    /**
     * @brief Returns a mutable iterator representing the end sentinel past all buckets.
     *
     * @return End iterator.
     */
    inline iterator end() noexcept               { return iterator(m_inner.raw, m_inner.raw ? m_inner.raw->nbuckets : 0); }

    /**
     * @brief Returns an immutable const iterator pointing to first occupied bucket slot.
     *
     * @return const_iterator pointing to first valid entry or end().
     */
    inline const_iterator begin() const noexcept { return const_iterator(m_inner.raw, 0); }

    /**
     * @brief Returns an immutable const iterator representing the end sentinel past all buckets.
     *
     * @return Const end iterator.
     */
    inline const_iterator end() const noexcept   { return const_iterator(m_inner.raw, m_inner.raw ? m_inner.raw->nbuckets : 0); }

    /**
     * @brief Returns an immutable const iterator pointing to first occupied bucket slot.
     *
     * @return const_iterator pointing to first valid entry or cend().
     */
    inline const_iterator cbegin() const noexcept{ return const_iterator(m_inner.raw, 0); }

    /**
     * @brief Returns an immutable const iterator representing the end sentinel past all buckets.
     *
     * @return Const end iterator.
     */
    inline const_iterator cend() const noexcept  { return const_iterator(m_inner.raw, m_inner.raw ? m_inner.raw->nbuckets : 0); }

    // ------------------------------------------------------------------------
    // Capacity, Resizing & Hashing Configuration
    // ------------------------------------------------------------------------

    /**
     * @brief Returns total number of active key-value entries (matching pure C duo_hashmap_count).
     */
    inline size_t size() const noexcept         { return m_inner.raw ? m_inner.raw->count : 0; }

    /**
     * @brief Returns true if map contains no entries.
     */
    inline bool empty() const noexcept          { return size() == 0; }

    /**
     * @brief Returns total allocated bucket capacity (matching pure C duo_hashmap_capacity / duo_hashmap_nbuckets).
     */
    inline size_t bucket_count() const noexcept { return m_inner.raw ? m_inner.raw->nbuckets : 0; }

    /**
     * @brief Capacity alias returning total allocated bucket slots.
     */
    inline size_t capacity() const noexcept     { return bucket_count(); }

    /**
     * @brief Pure C naming parity alias returning total bucket slots (matching duo_hashmap_nbuckets).
     */
    inline size_t nbuckets() const noexcept     { return bucket_count(); }

    /**
     * @brief Returns current load factor (count / nbuckets).
     */
    inline double load_factor() const noexcept {
        if (!m_inner.raw || m_inner.raw->nbuckets == 0) return 0.0;
        return static_cast<double>(m_inner.raw->count) / static_cast<double>(m_inner.raw->nbuckets);
    }

    /**
     * @brief Returns target maximum load factor triggering capacity resize.
     */
    inline double max_load_factor() const noexcept {
        if (!m_inner.raw) return DUO_HASHMAP_LOAD_FACTOR;
        return m_inner.raw->loadfactor / 100.0;
    }

    /**
     * @brief Configures maximum load factor threshold triggering growth (matching pure C duo_hashmap_set_load_factor).
     *
     * @param factor Target load factor between 0.50 (50%) and 0.95 (95%).
     */
    inline void max_load_factor(double factor) noexcept {
        if (m_inner.raw) {
            duo_hashmap_set_load_factor(m_inner.raw, factor);
        }
    }

    /**
     * @brief Setter alias configuring maximum load factor threshold.
     *
     * @param factor Target load factor between 0.50 (50%) and 0.95 (95%).
     */
    inline void load_factor(double factor) noexcept {
        max_load_factor(factor);
    }

    /**
     * @brief Configures power-of-two geometric growth multiplier (matching duo_hashmap_set_grow_by_power).
     *
     * @param power Multiplier exponent (1 <= power <= 16; default 1 = double).
     */
    inline void grow_by_power(size_t power) noexcept {
        if (m_inner.raw) {
            duo_hashmap_set_grow_by_power(m_inner.raw, power);
        }
    }

    /**
     * @brief Returns the current power-of-two growth exponent.
     */
    inline size_t grow_by_power() const noexcept {
        return m_inner.raw ? m_inner.raw->growpower : 1;
    }

    /**
     * @brief Computes 48-bit clipped hash for key using table seeds and functor (matching duo_hashmap_calc_hash).
     */
    inline uint64_t hash(const Key& key) const noexcept {
        return calc_hash(key);
    }

    /**
     * @brief Masks a 64-bit hash to 48-bit packed bucket signature (matching duo_hashmap_clip_hash).
     */
    static inline uint64_t clip_hash(uint64_t h) noexcept {
        return duo_hashmap_clip_hash(h);
    }

    /**
     * @brief Rehashes the map to hold at least bucket_count slots (matching pure C duo_hashmap_resize).
     *
     * @param bucket_count Target bucket capacity hint.
     * @return true on success, false on OOM.
     */
    inline bool rehash(size_t bucket_count) noexcept {
        if (!m_inner.raw && !init(bucket_count)) return false;
        return rehash_impl(bucket_count);
    }

    /**
     * @brief Pre-reserves capacity to hold at least count elements without triggering rehashing.
     *
     * @param count Minimum number of elements to reserve space for.
     * @return true on success, false on OOM.
     */
    inline bool reserve(size_t count) noexcept {
        if (!m_inner.raw) init();
        double factor = max_load_factor();
        size_t needed = static_cast<size_t>(count / factor) + 1;
        return rehash(needed);
    }

    /**
     * @brief Checks if most recent allocation or resize attempt failed due to OOM (matching pure C duo_hashmap_oom).
     */
    inline bool oom() const noexcept {
        return m_inner.raw ? m_inner.raw->oom : false;
    }

    /**
     * @brief Compares two keys for equality using the configured EqualFn (matching pure C duo_hashmap_compare_keys).
     */
    inline bool compare_keys(const Key& a, const Key& b) const noexcept {
        return EqualFn{}(a, b);
    }

    /**
     * @brief Static comparator matching signature of duo_hashmap_default_compare.
     */
    static inline int default_compare(const void* a, const void* b, void* udata) noexcept {
        return duo_hashmap_default_compare(a, b, udata);
    }


    /**
     * @brief Clamps a load factor within valid range (matching duo_hashmap_clamp_load_factor).
     */
    static inline double clamp_load_factor(double factor, double default_factor = DUO_HASHMAP_LOAD_FACTOR) noexcept {
        return duo_hashmap_clamp_load_factor(factor, default_factor);
    }

    /**
     * @brief Explicit capacity resizing alias matching duo_hashmap_resize.
     */
    inline bool resize(size_t new_cap) noexcept {
        return rehash(new_cap);
    }

    /**
     * @brief Destroys active elements while preserving table structure (matching duo_hashmap_free_elements).
     */
    inline void free_elements() noexcept {
        clear_impl(false);
    }

    /**
     * @brief Swaps internal table representations with another map in O(1) time without heap copies.
     */
    inline void swap(HashMap& other) noexcept {
        duo::swap(m_inner.raw, other.m_inner.raw);
    }

    // ------------------------------------------------------------------------
    // Dual-ABI C Interoperability
    // ------------------------------------------------------------------------

    /**
     * @brief Returns direct pointer to underlying pure C duo_hashmap_t handle.
     */
    inline duo_hashmap_t* raw() const noexcept { return m_inner.raw; }

    /**
     * @brief Implicit conversion operator to pure C duo_hashmap_t pointer.
     */
    inline operator duo_hashmap_t*() const noexcept { return m_inner.raw; }

    /**
     * @brief Returns address of internal duo_hashmap_t handle pointer.
     */
    inline duo_hashmap_t** raw_ptr() noexcept { return &m_inner.raw; }

    /**
     * @brief Adopts an existing raw pure C duo_hashmap_t pointer into an owning C++ HashMap container.
     */
    static inline HashMap adopt(duo_hashmap_t* raw_handle) noexcept {
        HashMap m;
        m.m_inner.raw = raw_handle;
        return m;
    }

    DUO_CXX_FFI_OPS(m_inner)
};

/**
 * @brief Freestanding swap overload for HashMap supporting ADL.
 *
 * @tparam K Key type.
 * @tparam V Value type.
 * @tparam H Hash functor type.
 * @tparam E Equality functor type.
 * @param a First map.
 * @param b Second map.
 */
template <typename K, typename V, typename H, typename E>
inline void swap(HashMap<K, V, H, E>& a, HashMap<K, V, H, E>& b) noexcept {
    a.swap(b);
}

// Static ABI assertions for HashMap
static_assert(sizeof(c_hashmap_t<int, int>) == sizeof(void*), "c_hashmap_t<K, V> must be 1 pointer (8 bytes)!");
static_assert(sizeof(HashMap<int, int>) == sizeof(void*), "HashMap<K, V> must be 1 pointer (8 bytes)!");
static_assert(is_standard_layout<c_hashmap_t<int, int>>::value, "c_hashmap_t<K, V> must be standard layout!");
static_assert(is_standard_layout<HashMap<int, int>>::value, "HashMap<K, V> must be standard layout!");
static_assert(is_trivially_copyable<c_hashmap_t<int, int>>::value, "c_hashmap_t<K, V> must be trivially copyable!");

// ============================================================================
// 5. C++17 ASSOCIATIVE SET: duo::HashSet<T>
// ============================================================================

/**
 * @class HashSet
 * @brief Freestanding Robin Hood hash set container for C++17.
 *
 * Wraps a standard-layout C mirror struct `m_inner` of type `c_hashset_t<T>`
 * (exactly 8 bytes, standard-layout).
 *
 * @tparam T       Element type.
 * @tparam HashFn  Hash functor (default duo::Hash<T>).
 * @tparam EqualFn Element equality functor (default duo::Equal<T>).
 */
template <typename T, typename HashFn = Hash<T>, typename EqualFn = Equal<T>>
class HashSet {
public:
    using self_type       = HashSet;
    using c_type          = c_hashset_t<T>;
    using key_type        = T;
    using value_type      = T;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using hasher          = HashFn;
    using key_equal       = EqualFn;

    c_type m_inner{nullptr}; /**< Standard-layout C mirror struct at Offset 0. */

private:
    // Static C Trampolines
    /**
     * @brief Static pure C hash trampoline adapting HashFn to duo_hash_hash_fn signature.
     *
     * @param key   Pointer to element payload.
     * @param seed0 Primary 64-bit seed.
     * @param seed1 Secondary 64-bit seed.
     * @return 64-bit hash value.
     */
    static uint64_t hash_trampoline(const void* key, uint64_t seed0, uint64_t seed1) {
        const T* k = static_cast<const T*>(key);
        return invoke_hash_fn(HashFn{}, *k, seed0, seed1, 0);
    }

    /**
     * @brief Static pure C equality comparator trampoline adapting EqualFn to duo_hash_compare_fn signature.
     *
     * @param a     Pointer to first element.
     * @param b     Pointer to second element.
     * @param udata User data pointer (unused).
     * @return 0 if equal, 1 otherwise.
     */
    static int compare_trampoline(const void* a, const void* b, void* udata) {
        (void)udata;
        const T* ka = static_cast<const T*>(a);
        const T* kb = static_cast<const T*>(b);
        return EqualFn{}(*ka, *kb) ? 0 : 1;
    }

    /**
     * @brief Static element destructor trampoline invoking C++ destructor for non-trivially destructible types.
     *
     * @param key Pointer to element payload inside bucket.
     */
    static void key_destructor_trampoline(void* key) {
        if constexpr (!duo::is_trivially_destructible<T>::value) {
            duo::destroy_at(static_cast<T*>(key));
        }
    }

    /** @brief Compile-time offset of element payload within a hash bucket (past 8-byte header). */
    static constexpr size_t k_key_offset = sizeof(struct duo_hash_bucket);

    /** @brief Total aligned stride of a single set bucket in bytes. */
    static constexpr size_t k_bucketsz   = (k_key_offset + sizeof(T) + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);

    /**
     * @brief Computes mutable bucket pointer at index in raw contiguous bucket memory.
     *
     * @param buckets Base pointer to bucket array.
     * @param idx     Bucket slot index.
     * @return Pointer to duo_hash_bucket header.
     */
    static inline struct duo_hash_bucket* get_bucket(void* buckets, size_t idx) noexcept {
        return reinterpret_cast<struct duo_hash_bucket*>(static_cast<char*>(buckets) + idx * k_bucketsz);
    }

    /**
     * @brief Computes const bucket pointer at index in raw contiguous bucket memory.
     *
     * @param buckets Base pointer to bucket array.
     * @param idx     Bucket slot index.
     * @return Const pointer to duo_hash_bucket header.
     */
    static inline const struct duo_hash_bucket* get_bucket(const void* buckets, size_t idx) noexcept {
        return reinterpret_cast<const struct duo_hash_bucket*>(static_cast<const char*>(buckets) + idx * k_bucketsz);
    }

    /**
     * @brief Computes mutable element pointer embedded inside a bucket.
     *
     * @param b Pointer to bucket.
     * @return Pointer to element payload.
     */
    static inline T* get_item(struct duo_hash_bucket* b) noexcept {
        return reinterpret_cast<T*>(reinterpret_cast<char*>(b) + k_key_offset);
    }

    /**
     * @brief Computes const element pointer embedded inside a bucket.
     *
     * @param b Pointer to bucket.
     * @return Const pointer to element payload.
     */
    static inline const T* get_item(const struct duo_hash_bucket* b) noexcept {
        return reinterpret_cast<const T*>(reinterpret_cast<const char*>(b) + k_key_offset);
    }

    /**
     * @brief Accessor retrieving mutable bucket at slot index from active set.
     *
     * @param idx Bucket slot index.
     * @return Pointer to bucket header.
     */
    inline struct duo_hash_bucket* bucket_at(size_t idx) noexcept {
        return get_bucket(m_inner.raw->buckets, idx);
    }

    /**
     * @brief Accessor retrieving const bucket at slot index from active set.
     *
     * @param idx Bucket slot index.
     * @return Const pointer to bucket header.
     */
    inline const struct duo_hash_bucket* bucket_at(size_t idx) const noexcept {
        return get_bucket(m_inner.raw->buckets, idx);
    }

    /**
     * @brief Computes 48-bit clipped hash code for element using HashFn and table seeds.
     *
     * @param item Element reference.
     * @return 48-bit masked hash signature.
     */
    inline uint64_t calc_hash(const T& item) const noexcept {
        return invoke_hash_fn(HashFn{}, item, m_inner.raw->seed0, m_inner.raw->seed1, 0) & 0xFFFFFFFFFFFFULL;
    }

    /**
     * @brief Destroys element payload in a bucket slot and clears header in set.
     *
     * @param b Pointer to bucket.
     */
    static inline void destroy_bucket_payload(struct duo_hash_bucket* b) noexcept {
        if constexpr (!duo::is_trivially_destructible<T>::value) {
            get_item(b)->~T();
        }
        b->dib  = 0;
        b->hash = 0;
    }

    /**
     * @brief Move-constructs element payload from src bucket into dest bucket in set.
     *
     * @param dest Destination bucket pointer.
     * @param src  Source bucket pointer.
     */
    static inline void move_bucket_payload(struct duo_hash_bucket* dest, struct duo_hash_bucket* src) noexcept {
        if constexpr (duo::is_trivially_copyable<T>::value) {
            DUO_MEMCPY(reinterpret_cast<char*>(dest) + k_key_offset,
                   reinterpret_cast<char*>(src) + k_key_offset,
                   sizeof(T));
        } else {
            duo::construct_at(get_item(dest), duo::move(*get_item(src)));
            get_item(src)->~T();
        }
    }

    /**
     * @brief Inserts an element into target bucket memory displacing richer entries using Robin Hood hashing.
     *
     * @tparam ItemType Element forward reference type.
     * @param target_buckets Pointer to contiguous bucket memory.
     * @param target_mask    Bitmask (capacity - 1).
     * @param cur_hash       Hash code of element.
     * @param cur_item       Element forward reference.
     */
    template <typename ItemType>
    static inline void insert_displace_into(void* target_buckets, size_t target_mask,
                                            uint64_t cur_hash, ItemType&& cur_item) noexcept {
        uint16_t cur_dib = 1;
        if constexpr (duo::is_trivially_copyable<T>::value) {
            alignas(alignof(struct duo_hash_bucket)) unsigned char entry_buf[k_bucketsz];
            struct duo_hash_bucket* entry = reinterpret_cast<struct duo_hash_bucket*>(entry_buf);
            entry->hash = cur_hash;
            entry->dib  = 1;
            T item_obj(duo::forward<ItemType>(cur_item));
            DUO_MEMCPY(reinterpret_cast<char*>(entry) + k_key_offset, &item_obj, sizeof(T));

            size_t i = cur_hash & target_mask;
            while (true) {
                struct duo_hash_bucket* b = get_bucket(target_buckets, i);
                if (b->dib == 0) {
                    DUO_MEMCPY(b, entry, k_bucketsz);
                    return;
                }
                if (b->dib < entry->dib) {
                    alignas(alignof(struct duo_hash_bucket)) unsigned char tmp[k_bucketsz];
                    DUO_MEMCPY(tmp, b, k_bucketsz);
                    DUO_MEMCPY(b, entry, k_bucketsz);
                    DUO_MEMCPY(entry, tmp, k_bucketsz);
                }
                i = (i + 1) & target_mask;
                entry->dib++;
            }
        } else {
            T item_val(duo::forward<ItemType>(cur_item));

            size_t i = cur_hash & target_mask;
            while (true) {
                struct duo_hash_bucket* b = get_bucket(target_buckets, i);
                if (b->dib == 0) {
                    b->hash = cur_hash;
                    b->dib  = cur_dib;
                    duo::construct_at(get_item(b), duo::move(item_val));
                    return;
                }
                if (b->dib < cur_dib) {
                    uint64_t tmp_hash = b->hash;
                    uint16_t tmp_dib  = b->dib;
                    b->hash = cur_hash;
                    b->dib  = cur_dib;
                    cur_hash = tmp_hash;
                    cur_dib  = tmp_dib;

                    T tmp_item(duo::move(*get_item(b)));
                    get_item(b)->~T();
                    duo::construct_at(get_item(b), duo::move(item_val));
                    item_val.~T();
                    duo::construct_at(&item_val, duo::move(tmp_item));
                }
                i = (i + 1) & target_mask;
                cur_dib++;
            }
        }
    }

    /**
     * @brief Internal reallocation and Robin Hood re-insertion algorithm for capacity changes in set.
     *
     * @param new_cap Target bucket capacity (power of two >= 16).
     * @return true on successful reallocation, false on memory allocation failure.
     */
    bool rehash_impl(size_t new_cap) {
        if (!m_inner.raw) return false;
        size_t cap = 16;
        while (cap < new_cap) cap <<= 1;
        new_cap = cap;

        void* new_buckets = DUO_MALLOC(k_bucketsz * new_cap);
        if (!new_buckets) return false;
        DUO_MEMSET(new_buckets, 0, k_bucketsz * new_cap);

        size_t new_mask = new_cap - 1;
        size_t old_nbuckets = m_inner.raw->nbuckets;
        void* old_buckets = m_inner.raw->buckets;

        for (size_t i = 0; i < old_nbuckets; ++i) {
            struct duo_hash_bucket* old_b = get_bucket(old_buckets, i);
            if (old_b->dib == 0) continue;
            insert_displace_into(new_buckets, new_mask, old_b->hash, duo::move(*get_item(old_b)));
            destroy_bucket_payload(old_b);
        }

        DUO_FREE(old_buckets);
        m_inner.raw->buckets  = new_buckets;
        m_inner.raw->nbuckets = new_cap;
        m_inner.raw->mask     = new_mask;
        m_inner.raw->growat   = static_cast<size_t>(new_cap * (m_inner.raw->loadfactor / 100.0));
        m_inner.raw->shrinkat = static_cast<size_t>(new_cap * DUO_HASHMAP_SHRINK_AT);
        return true;
    }

    /**
     * @brief Checks if set capacity threshold has been reached and grows capacity by power exponent if needed.
     *
     * @return true if capacity is sufficient or grow succeeded, false on OOM.
     */
    inline bool grow_if_needed() noexcept {
        if (m_inner.raw->count >= m_inner.raw->growat) {
            size_t new_cap = m_inner.raw->nbuckets * (static_cast<size_t>(1) << m_inner.raw->growpower);
            if (!rehash_impl(new_cap)) {
                m_inner.raw->oom = true;
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Internal Robin Hood insertion implementation for HashSet with uniqueness check.
     *
     * @tparam ItemType Element forward reference type.
     * @param item               Element to insert.
     * @param precomputed_hash   Optional precomputed 64-bit hash code.
     * @param has_hash           True if precomputed_hash is supplied, false to compute via calc_hash.
     * @return true on successful insertion, false if item already exists or on OOM.
     */
    template <typename ItemType>
    inline bool insert_impl(ItemType&& item, uint64_t precomputed_hash = 0, bool has_hash = false) {
        if (!m_inner.raw && !init()) return false;
        uint64_t hash = has_hash ? duo_hashmap_clip_hash(precomputed_hash) : calc_hash(item);

        if (m_inner.raw->count > 0 && get_with_hash(item, hash)) {
            return false;
        }

        if (!grow_if_needed()) return false;

        insert_displace_into(m_inner.raw->buckets, m_inner.raw->mask, hash, duo::forward<ItemType>(item));
        m_inner.raw->count++;
        return true;
    }

    /**
     * @brief Performs backward-shift deletion starting from slot index i in set.
     *
     * Shifts occupied buckets backwards until an empty bucket or bucket with DIB <= 1 is found.
     *
     * @param i Starting slot index where an element was removed.
     */
    inline void backward_shift(size_t i) noexcept {
        struct duo_hash_bucket* b = bucket_at(i);
        while (true) {
            size_t next_i = (i + 1) & m_inner.raw->mask;
            struct duo_hash_bucket* next_b = bucket_at(next_i);
            if (next_b->dib <= 1) {
                b->dib  = 0;
                b->hash = 0;
                break;
            }

            b->hash = next_b->hash;
            b->dib  = next_b->dib - 1;
            move_bucket_payload(b, next_b);

            i = next_i;
            b = next_b;
        }
    }

    /**
     * @brief Shrinks bucket capacity by half if element count falls below shrink threshold.
     */
    inline void shrink_if_needed() noexcept {
        if (m_inner.raw->nbuckets > m_inner.raw->cap && m_inner.raw->count <= m_inner.raw->shrinkat) {
            rehash_impl(m_inner.raw->nbuckets / 2);
        }
    }

    /**
     * @brief Internal extraction implementation transferring element ownership without destruction in set.
     *
     * @param item               Element to extract.
     * @param out_item           Optional destination pointer to receive moved element.
     * @param precomputed_hash   Optional precomputed 64-bit hash.
     * @param has_hash           True if precomputed_hash is supplied, false to compute via calc_hash.
     * @return true if element was found and extracted, false otherwise.
     */
    inline bool extract_impl(const T& item, T* out_item = nullptr, uint64_t precomputed_hash = 0, bool has_hash = false) noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return false;
        uint64_t hash = has_hash ? duo_hashmap_clip_hash(precomputed_hash) : calc_hash(item);
        size_t i = hash & m_inner.raw->mask;

        while (true) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return false;
            if (b->hash == hash && EqualFn{}(item, *get_item(b))) {
                if (out_item) {
                    *out_item = duo::move(*get_item(b));
                }
                destroy_bucket_payload(b);
                m_inner.raw->count--;

                backward_shift(i);
                shrink_if_needed();
                return true;
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Internal backward-shift deletion without tombstones for element removal in set.
     *
     * Delegates directly to extract_impl without out-pointer, avoiding duplicate backward-shift logic.
     *
     * @param item               Element to locate and remove.
     * @param precomputed_hash   Optional precomputed 64-bit hash.
     * @param has_hash           True if precomputed_hash is supplied, false to compute via calc_hash.
     * @return true if element was found and removed, false if not present.
     */
    inline bool erase_impl(const T& item, uint64_t precomputed_hash = 0, bool has_hash = false) noexcept {
        return extract_impl(item, nullptr, precomputed_hash, has_hash);
    }

    /**
     * @brief Resets bucket buffer allocation and table metadata to clean state in set.
     *
     * @param update_cap If true, keeps current capacity; if false, shrinks back to initial capacity.
     */
    inline void reset_table_buffers(bool update_cap) noexcept {
        m_inner.raw->count = 0;
        if (update_cap) {
            m_inner.raw->cap = m_inner.raw->nbuckets;
        } else if (m_inner.raw->nbuckets != m_inner.raw->cap) {
            void* new_buckets = DUO_MALLOC(k_bucketsz * m_inner.raw->cap);
            if (new_buckets) {
                DUO_FREE(m_inner.raw->buckets);
                m_inner.raw->buckets = new_buckets;
                m_inner.raw->nbuckets = m_inner.raw->cap;
            }
        }
        DUO_MEMSET(m_inner.raw->buckets, 0, k_bucketsz * m_inner.raw->nbuckets);
        m_inner.raw->mask = m_inner.raw->nbuckets - 1;
        m_inner.raw->growat = static_cast<size_t>(m_inner.raw->nbuckets * (m_inner.raw->loadfactor / 100.0));
        m_inner.raw->shrinkat = static_cast<size_t>(m_inner.raw->nbuckets * DUO_HASHMAP_SHRINK_AT);
    }

    /**
     * @brief Internal clear implementation destroying active elements and resetting table buffers.
     *
     * @param update_cap If true, resets base capacity to current capacity; if false, reallocates initial capacity.
     */
    inline void clear_impl(bool update_cap = false) noexcept {
        if (!m_inner.raw) return;
        for (size_t i = 0; i < m_inner.raw->nbuckets; ++i) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib != 0) {
                destroy_bucket_payload(b);
            }
        }
        reset_table_buffers(update_cap);
    }

    /**
     * @brief Calculates shrink target capacity and rehashes table after bulk deletions in set.
     */
    inline void shrink_to_target_cap() noexcept {
        size_t target_cap = m_inner.raw->nbuckets;
        while (target_cap > m_inner.raw->cap && m_inner.raw->count <= static_cast<size_t>(target_cap * DUO_HASHMAP_SHRINK_AT)) {
            target_cap /= 2;
        }
        if (target_cap < m_inner.raw->cap) {
            target_cap = m_inner.raw->cap;
        }
        rehash_impl(target_cap);
    }

    /**
     * @brief Internal atomic predicate filter scanning and removing elements matching predicate in set.
     *
     * Resolves upstream tidwall/hashmap.c Issue #41 by invalidating matching buckets
     * and performing a single rehash/re-pack pass to maintain Robin Hood invariant.
     *
     * @tparam Predicate Callable accepting `(const T&)`.
     * @param pred Filter predicate returning true to remove, false to keep.
     * @return Count of removed elements.
     */
    template <typename Predicate>
    inline size_t filter_impl(Predicate&& pred) {
        if (!m_inner.raw || m_inner.raw->count == 0) return 0;
        size_t removed = 0;
        for (size_t i = 0; i < m_inner.raw->nbuckets; ++i) {
            struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib != 0) {
                if (pred(*get_item(b))) {
                    destroy_bucket_payload(b);
                    removed++;
                }
            }
        }
        if (removed == 0) return 0;

        m_inner.raw->count -= removed;
        if (m_inner.raw->count == 0) {
            clear_impl(false);
            return removed;
        }

        shrink_to_target_cap();
        return removed;
    }

public:
    // ------------------------------------------------------------------------
    // Iterator Suite
    // ------------------------------------------------------------------------
    /**
     * @class const_iterator
     * @brief Immutable forward iterator scanning occupied buckets of HashSet.
     */
    class const_iterator {
    protected:
        const duo_hashset_t* m_set{nullptr}; /**< Non-owning pointer to set table. */
        size_t               m_index{0};     /**< Current bucket slot index. */

        /**
         * @brief Advances cursor to next occupied bucket (dib != 0) or table end.
         */
        void advance_to_valid() noexcept {
            if (!m_set) return;
            while (m_index < m_set->nbuckets) {
                struct duo_hash_bucket* b = duo_hashmap_bucket_at((const duo_hashmap_t*)m_set, m_index);
                if (b->dib != 0) {
                    return;
                }
                m_index++;
            }
        }

    public:
        using iterator_category = forward_iterator_tag;
        using value_type        = const T;
        using difference_type   = ptrdiff_t;
        using pointer           = const T*;
        using reference         = const T&;

        /** @brief Default constructor creating an unattached end iterator. */
        inline const_iterator() noexcept = default;

        /**
         * @brief Parameterized constructor initializing iterator at specified bucket slot index.
         *
         * @param set Pointer to hash set instance.
         * @param idx Starting bucket index.
         */
        inline const_iterator(const duo_hashset_t* set, size_t idx) noexcept 
            : m_set(set), m_index(idx) {
            advance_to_valid();
        }

        /** @brief Dereferences iterator returning const reference to element. */
        inline const T& operator*() const noexcept {
            return *static_cast<const T*>(duo_hashset_probe_const(m_set, m_index));
        }

        /** @brief Member access operator returning const pointer to element. */
        inline const T* operator->() const noexcept {
            return static_cast<const T*>(duo_hashset_probe_const(m_set, m_index));
        }

        /** @brief Prefix increment advancing to next occupied bucket. */
        inline const_iterator& operator++() noexcept {
            if (m_set && m_index < m_set->nbuckets) {
                m_index++;
                advance_to_valid();
            }
            return *this;
        }

        /** @brief Postfix increment advancing to next occupied bucket. */
        inline const_iterator operator++(int) noexcept {
            const_iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        /** @brief Equality comparison testing if two iterators point to same bucket or are both at end. */
        inline bool operator==(const const_iterator& o) const noexcept {
            size_t cap_a = m_set ? m_set->nbuckets : 0;
            size_t cap_b = o.m_set ? o.m_set->nbuckets : 0;
            bool end_a = (!m_set || m_index >= cap_a);
            bool end_b = (!o.m_set || o.m_index >= cap_b);
            if (end_a && end_b) return true;
            return m_set == o.m_set && m_index == o.m_index;
        }

        /** @brief Inequality comparison testing whether two iterators differ. */
        inline bool operator!=(const const_iterator& o) const noexcept {
            return !(*this == o);
        }
    };

    using iterator = const_iterator;

    // ------------------------------------------------------------------------
    // Constructors & Destructor
    // ------------------------------------------------------------------------

    /**
     * @brief Constructs an empty hash set without allocating bucket memory.
     */
    inline HashSet() noexcept : m_inner{nullptr} {}

    /**
     * @brief Constructs a hash set with initial capacity and optional 64-bit seeds (matching duo_hashset_new).
     *
     * @param initial_cap Initial bucket capacity hint (default 16).
     * @param seed0       Primary 64-bit hash seed.
     * @param seed1       Secondary 64-bit hash seed.
     */
    inline explicit HashSet(size_t initial_cap, uint64_t seed0 = 0, uint64_t seed1 = 0) noexcept {
        init(initial_cap, seed0, seed1);
    }


    /**
     * @brief Destroys all stored elements and releases table memory (matching duo_hashset_free).
     */
    inline ~HashSet() noexcept {
        if (m_inner.raw) {
            clear_impl(false);
            duo_hashset_free(m_inner.raw);
            m_inner.raw = nullptr;
        }
    }

    // Move Semantics

    /**
     * @brief Move constructor transfers ownership of table handle without heap reallocations.
     */
    inline HashSet(HashSet&& other) noexcept : m_inner{other.m_inner.raw} {
        other.m_inner.raw = nullptr;
    }

    /**
     * @brief Move assignment operator transfers table ownership, destroying existing elements.
     */
    inline HashSet& operator=(HashSet&& other) noexcept {
        if (this != &other) {
            if (m_inner.raw) {
                clear_impl(false);
                duo_hashset_free(m_inner.raw);
            }
            m_inner.raw = other.m_inner.raw;
            other.m_inner.raw = nullptr;
        }
        return *this;
    }

    // Copy Semantics

    /**
     * @brief Deep copy constructor cloning all active set elements.
     */
    inline HashSet(const HashSet& other) : m_inner{nullptr} {
        if (other.m_inner.raw) {
            init(other.m_inner.raw->cap, other.m_inner.raw->seed0, other.m_inner.raw->seed1);
            for (const auto& item : other) {
                insert(item);
            }
        }
    }

    /**
     * @brief Deep copy assignment operator using copy-and-swap idiom.
     */
    inline HashSet& operator=(const HashSet& other) {
        if (this != &other) {
            HashSet tmp(other);
            swap(tmp);
        }
        return *this;
    }

    // ------------------------------------------------------------------------
    // Table Initialization & Lifecycle
    // ------------------------------------------------------------------------

    /**
     * @brief Initializes table memory with given capacity and seeds (matching duo_hashset_new).
     *
     * @param cap   Capacity hint (rounded up to power-of-two >= 16).
     * @param seed0 Primary 64-bit hash seed.
     * @param seed1 Secondary 64-bit hash seed.
     * @return true on successful allocation, false on OOM.
     */
    inline bool init(size_t cap = 16, uint64_t seed0 = 0, uint64_t seed1 = 0) noexcept {
        if (m_inner.raw) {
            duo_hashset_free(m_inner.raw);
            m_inner.raw = nullptr;
        }
        void (*k_free)(void*) = duo::is_trivially_destructible<T>::value ? nullptr : &key_destructor_trampoline;

        m_inner.raw = duo_hashset_new(
            sizeof(T),
            cap,
            seed0,
            seed1,
            &hash_trampoline,
            &compare_trampoline,
            k_free,
            nullptr
        );
        return m_inner.raw != nullptr;
    }


    // ------------------------------------------------------------------------
    // Insertion & Modification
    // ------------------------------------------------------------------------

    /**
     * @brief Inserts an element into the set (matching pure C duo_hashset_insert).
     *
     * @param item Element to insert.
     * @return true on success, false if already present or on OOM.
     */
    inline bool insert(const T& item) {
        return insert_impl(item);
    }

    /**
     * @brief Inserts a moved element into the set.
     */
    inline bool insert(T&& item) {
        return insert_impl(duo::move(item));
    }

    /**
     * @brief Inserts an element using a precomputed 64-bit hash (matching pure C duo_hashset_insert_with_hash).
     *
     * @param item Element forward reference.
     * @param hash Precomputed 64-bit hash.
     * @return true on successful insertion, false if already present or OOM.
     */
    template <typename ItemType>
    inline bool insert_with_hash(ItemType&& item, uint64_t hash) {
        return insert_impl(duo::forward<ItemType>(item), hash, true);
    }

    /**
     * @brief In-place constructs an element and inserts it into the set.
     */
    template <typename... Args>
    inline bool emplace(Args&&... args) {
        return insert_impl(T(duo::forward<Args>(args)...));
    }

    // ------------------------------------------------------------------------
    // Lookup & Inspection
    // ------------------------------------------------------------------------

    /**
     * @brief Retrieves an immutable pointer to stored element (matching pure C duo_hashset_get).
     *
     * @param key Element to search for.
     * @return Const pointer to stored element, or nullptr if not found.
     */
    inline const T* get(const T& key) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        uint64_t hash = calc_hash(key);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            const struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return nullptr;
            if (b->hash == hash && EqualFn{}(key, *get_item(b))) {
                return get_item(b);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Retrieves an immutable pointer to the stored element using a precomputed hash (matching pure C duo_hashset_get_with_hash).
     *
     * @param key  Element to search for.
     * @param hash Precomputed 64-bit hash.
     * @return Const pointer to stored element, or nullptr if not found.
     */
    inline const T* get_with_hash(const T& key, uint64_t hash) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        hash = duo_hashmap_clip_hash(hash);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            const struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return nullptr;
            if (b->hash == hash && EqualFn{}(key, *get_item(b))) {
                return get_item(b);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Const accessor matching pure C duo_hashset_get_const.
     */
    inline const T* get_const(const T& key) const noexcept {
        return get(key);
    }

    /**
     * @brief Precomputed hash const accessor matching pure C duo_hashset_get_const_with_hash.
     */
    inline const T* get_const_with_hash(const T& key, uint64_t hash) const noexcept {
        return get_with_hash(key, hash);
    }

    /**
     * @brief Tests whether an element exists in the set (matching pure C duo_hashset_contains).
     *
     * @param key Element to search for.
     * @return true if found, false otherwise.
     */
    inline bool contains(const T& key) const noexcept {
        return get(key) != nullptr;
    }

    /**
     * @brief Tests whether an element exists in the set using a precomputed 64-bit hash (matching pure C duo_hashset_contains_with_hash).
     *
     * @param key  Element to search for.
     * @param hash Precomputed 64-bit hash.
     * @return true if found, false otherwise.
     */
    inline bool contains_with_hash(const T& key, uint64_t hash) const noexcept {
        return get_with_hash(key, hash) != nullptr;
    }

    /**
     * @brief Returns 1 if element exists in the set, 0 otherwise.
     */
    inline size_t count(const T& key) const noexcept {
        return contains(key) ? 1 : 0;
    }

    /**
     * @brief Copies stored element into destination buffer out_val.
     */
    inline bool get_copy(const T& key, T& out_val) const {
        const T* ptr = get(key);
        if (!ptr) return false;
        out_val = *ptr;
        return true;
    }

    /**
     * @brief Searches for element, returning an iterator to matching slot, or cend() if not found.
     *
     * @param key Element to search for.
     * @return Const iterator to matching element, or cend() if not found.
     */
    inline const_iterator find(const T& key) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return cend();
        uint64_t hash = calc_hash(key);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            const struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return cend();
            if (b->hash == hash && EqualFn{}(key, *get_item(b))) {
                return const_iterator(m_inner.raw, i);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Finds an element by key using a precomputed hash.
     *
     * @param key  Element to search for.
     * @param hash Precomputed 64-bit hash.
     * @return Const iterator to matching element, or cend() if not found.
     */
    inline const_iterator find_with_hash(const T& key, uint64_t hash) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return cend();
        hash = duo_hashmap_clip_hash(hash);
        size_t i = hash & m_inner.raw->mask;
        while (true) {
            const struct duo_hash_bucket* b = bucket_at(i);
            if (b->dib == 0) return cend();
            if (b->hash == hash && EqualFn{}(key, *get_item(b))) {
                return const_iterator(m_inner.raw, i);
            }
            i = (i + 1) & m_inner.raw->mask;
        }
    }

    /**
     * @brief Returns an immutable pointer to the element at bucket index modulo capacity (matching pure C duo_hashset_probe).
     *
     * @param index Bucket index (modulo table capacity).
     * @return Const pointer to element in bucket, or nullptr if bucket is empty.
     */
    inline const T* probe(size_t index) const noexcept {
        if (!m_inner.raw || m_inner.raw->count == 0) return nullptr;
        size_t i = index & m_inner.raw->mask;
        const struct duo_hash_bucket* b = bucket_at(i);
        return b->dib != 0 ? get_item(b) : nullptr;
    }

    /**
     * @brief Const overload of probe returning pointer to element at slot index (matching pure C duo_hashset_probe_const).
     */
    inline const T* probe_const(size_t index) const noexcept {
        return probe(index);
    }

    /**
     * @brief Direct pointer to raw bucket at slot index (matching pure C duo_hashmap_bucket_at).
     *
     * @param index Bucket slot index.
     * @return Pointer to bucket at index, or nullptr if table uninitialized.
     */
    inline struct duo_hash_bucket* bucket_at_index(size_t index) noexcept {
        return m_inner.raw ? duo_hashmap_bucket_at(m_inner.raw, index) : nullptr;
    }

    /**
     * @brief Direct pointer to raw bucket at slot index (const overload matching duo_hashmap_bucket_at).
     *
     * @param index Bucket slot index.
     * @return Const pointer to bucket at index, or nullptr if table uninitialized.
     */
    inline const struct duo_hash_bucket* bucket_at_index(size_t index) const noexcept {
        return m_inner.raw ? duo_hashmap_bucket_at(m_inner.raw, index) : nullptr;
    }

    /**
     * @brief Retrieves item pointer from a raw bucket (matching pure C duo_hashmap_bucket_key).
     *
     * @param b Raw bucket pointer.
     * @return Pointer to element inside bucket, or nullptr if b is null.
     */
    inline T* bucket_item(struct duo_hash_bucket* b) noexcept {
        return b ? get_item(b) : nullptr;
    }

    /**
     * @brief Retrieves const item pointer from a raw bucket (const overload matching duo_hashmap_bucket_key).
     *
     * @param b Raw bucket pointer.
     * @return Const pointer to element inside bucket, or nullptr if b is null.
     */
    inline const T* bucket_item(const struct duo_hash_bucket* b) const noexcept {
        return b ? get_item(b) : nullptr;
    }

    /**
     * @brief Retrieves const item pointer from a raw bucket (matching pure C duo_hashmap_bucket_key_const).
     *
     * @param b Raw bucket pointer.
     * @return Const pointer to element inside bucket, or nullptr if b is null.
     */
    inline const T* bucket_item_const(const struct duo_hash_bucket* b) const noexcept {
        return b ? get_item(b) : nullptr;
    }

    // ------------------------------------------------------------------------
    // Erasure & Clearing
    // ------------------------------------------------------------------------

    /**
     * @brief Removes an element matching key using backward-shift deletion (matching pure C duo_hashset_remove).
     *
     * @param key Element to remove.
     * @return true if element was found and removed, false if not found.
     */
    inline bool erase(const T& key) noexcept {
        return erase_impl(key);
    }

    /**
     * @brief Removes an element matching key using a precomputed 64-bit hash (matching pure C duo_hashset_remove_with_hash).
     *
     * @param key  Element to remove.
     * @param hash Precomputed 64-bit hash.
     * @return true if element was found and removed, false if not found.
     */
    inline bool erase_with_hash(const T& key, uint64_t hash) noexcept {
        return erase_impl(key, hash, true);
    }

    /**
     * @brief Removes an element matching key (semantic alias for erase matching pure C duo_hashset_remove).
     */
    inline bool remove(const T& key) noexcept {
        return erase(key);
    }

    /**
     * @brief Removes an element matching key using a precomputed hash (matching pure C duo_hashset_remove_with_hash).
     */
    inline bool remove_with_hash(const T& key, uint64_t hash) noexcept {
        return erase_with_hash(key, hash);
    }

    /**
     * @brief Extracts an element without invoking destructor, transferring ownership to out_item if provided (matching pure C duo_hashset_extract).
     *
     * @param key      Element to extract.
     * @param out_item Optional destination buffer to move element into.
     * @return true if found and extracted, false otherwise.
     */
    inline bool extract(const T& key, T* out_item = nullptr) noexcept {
        return extract_impl(key, out_item, 0, false);
    }

    /**
     * @brief Extracts element, transferring ownership into out_item.
     */
    inline bool extract(const T& key, T& out_item) noexcept {
        return extract_impl(key, &out_item, 0, false);
    }

    /**
     * @brief Extracts an element using a precomputed hash without invoking destructor (matching pure C duo_hashset_extract_with_hash).
     */
    inline bool extract_with_hash(const T& key, uint64_t hash, T* out_item = nullptr) noexcept {
        return extract_impl(key, out_item, hash, true);
    }

    /**
     * @brief Extracts element using a precomputed hash, transferring ownership into out_item.
     */
    inline bool extract_with_hash(const T& key, uint64_t hash, T& out_item) noexcept {
        return extract_impl(key, &out_item, hash, true);
    }

    /**
     * @brief Erases all elements, optionally resetting capacity to default (matching pure C duo_hashset_clear).
     *
     * @param update_cap If true, shrinks capacity to initial table size; if false, retains bucket allocation.
     */
    inline void clear(bool update_cap = false) noexcept {
        clear_impl(update_cap);
    }

    // ------------------------------------------------------------------------
    // Issue #41 Predicate Filter
    // ------------------------------------------------------------------------

    /**
     * @brief Atomically filters set in-place using predicate callback (matching pure C duo_hashset_filter).
     *
     * Removes all elements for which pred(item) returns true, re-packing survivors in a single pass.
     *
     * @param pred Callable returning true to delete, false to retain.
     * @return Total count of deleted elements.
     */
    template <typename Predicate>
    inline size_t filter(Predicate&& pred) {
        return filter_impl(duo::forward<Predicate>(pred));
    }

    // ------------------------------------------------------------------------
    // Iteration & Higher-Order Scans
    // ------------------------------------------------------------------------

    /**
     * @brief Iterates over all active elements, invoking callback on each item.
     *
     * @param cb Callable accepting `(const T& item)`. Aborts iteration if returning false.
     * @return true if entire table was traversed, false if aborted early.
     */
    template <typename Callback>
    inline bool for_each(Callback&& cb) const {
        if (!m_inner.raw) return true;
        for (auto it = cbegin(); it != cend(); ++it) {
            if (!cb(*it)) return false;
        }
        return true;
    }

    /**
     * @brief Higher-order scan alias executing callback on all active elements (matching pure C duo_hashset_scan).
     *
     * @param cb Callable accepting `(const T& item)`.
     * @return true if all items were visited, false if iteration was terminated early.
     */
    template <typename Callback>
    inline bool scan(Callback&& cb) const {
        return for_each(duo::forward<Callback>(cb));
    }

    /**
     * @brief Cursor-based step-by-step element iterator matching pure C duo_hashset_iter.
     *
     * Caller initializes `size_t i = 0` before first call.
     *
     * @param i        Cursor index (incremented across calls).
     * @param out_item Receives pointer to yielded element.
     * @return true if element yielded, false if iteration complete.
     */
    inline bool iter(size_t& i, const T*& out_item) const noexcept {
        if (!m_inner.raw) return false;
        void* item_ptr = nullptr;
        if (duo_hashset_iter(const_cast<duo_hashset_t*>(m_inner.raw), &i, &item_ptr)) {
            out_item = static_cast<const T*>(item_ptr);
            return true;
        }
        return false;
    }

    /**
     * @brief Returns a mutable iterator pointing to first occupied bucket slot.
     *
     * @return iterator pointing to first valid element or end().
     */
    inline iterator begin() noexcept { return const_iterator(m_inner.raw, 0); }

    /**
     * @brief Returns a mutable iterator representing the end sentinel past all buckets.
     *
     * @return End iterator.
     */
    inline iterator end() noexcept   { return const_iterator(m_inner.raw, m_inner.raw ? m_inner.raw->nbuckets : 0); }

    /**
     * @brief Returns an immutable const iterator pointing to first occupied bucket slot.
     *
     * @return const_iterator pointing to first valid element or end().
     */
    inline const_iterator begin() const noexcept { return const_iterator(m_inner.raw, 0); }

    /**
     * @brief Returns an immutable const iterator representing the end sentinel past all buckets.
     *
     * @return Const end iterator.
     */
    inline const_iterator end() const noexcept   { return const_iterator(m_inner.raw, m_inner.raw ? m_inner.raw->nbuckets : 0); }

    /**
     * @brief Returns an immutable const iterator pointing to first occupied bucket slot.
     *
     * @return const_iterator pointing to first valid element or cend().
     */
    inline const_iterator cbegin() const noexcept{ return const_iterator(m_inner.raw, 0); }

    /**
     * @brief Returns an immutable const iterator representing the end sentinel past all buckets.
     *
     * @return Const end iterator.
     */
    inline const_iterator cend() const noexcept  { return const_iterator(m_inner.raw, m_inner.raw ? m_inner.raw->nbuckets : 0); }

    // ------------------------------------------------------------------------
    // Capacity, Resizing & Hashing Configuration
    // ------------------------------------------------------------------------

    /**
     * @brief Returns total number of active elements in set (matching pure C duo_hashset_count).
     */
    inline size_t size() const noexcept         { return m_inner.raw ? duo_hashset_count(m_inner.raw) : 0; }

    /**
     * @brief Returns true if set contains no elements.
     */
    inline bool empty() const noexcept          { return size() == 0; }

    /**
     * @brief Returns total allocated bucket capacity (matching pure C duo_hashset_capacity).
     */
    inline size_t bucket_count() const noexcept { return m_inner.raw ? duo_hashset_capacity(m_inner.raw) : 0; }

    /**
     * @brief Capacity alias returning total allocated bucket slots.
     */
    inline size_t capacity() const noexcept     { return bucket_count(); }

    /**
     * @brief Pure C naming parity alias returning total bucket slots.
     */
    inline size_t nbuckets() const noexcept     { return bucket_count(); }

    /**
     * @brief Returns current load factor (count / nbuckets).
     */
    inline double load_factor() const noexcept {
        if (!m_inner.raw || m_inner.raw->nbuckets == 0) return 0.0;
        return static_cast<double>(m_inner.raw->count) / static_cast<double>(m_inner.raw->nbuckets);
    }

    /**
     * @brief Returns target maximum load factor triggering capacity resize.
     */
    inline double max_load_factor() const noexcept {
        if (!m_inner.raw) return DUO_HASHMAP_LOAD_FACTOR;
        return m_inner.raw->loadfactor / 100.0;
    }

    /**
     * @brief Configures maximum load factor threshold triggering growth (matching pure C duo_hashset_set_load_factor).
     *
     * @param factor Target load factor between 0.50 (50%) and 0.95 (95%).
     */
    inline void max_load_factor(double factor) noexcept {
        if (m_inner.raw) {
            duo_hashset_set_load_factor(m_inner.raw, factor);
        }
    }

    /**
     * @brief Setter alias configuring maximum load factor threshold.
     *
     * @param factor Target load factor between 0.50 (50%) and 0.95 (95%).
     */
    inline void load_factor(double factor) noexcept {
        max_load_factor(factor);
    }

    /**
     * @brief Configures power-of-two geometric growth multiplier (matching duo_hashset_set_grow_by_power).
     *
     * @param power Multiplier exponent (1 <= power <= 16; default 1 = double).
     */
    inline void grow_by_power(size_t power) noexcept {
        if (m_inner.raw) {
            duo_hashset_set_grow_by_power(m_inner.raw, power);
        }
    }

    /**
     * @brief Returns the current power-of-two growth exponent.
     */
    inline size_t grow_by_power() const noexcept {
        return m_inner.raw ? m_inner.raw->growpower : 1;
    }

    /**
     * @brief Computes 48-bit clipped hash for element using table seeds and functor (matching duo_hashmap_calc_hash).
     */
    inline uint64_t hash(const T& item) const noexcept {
        return calc_hash(item);
    }

    /**
     * @brief Masks a 64-bit hash to 48-bit packed bucket signature (matching duo_hashmap_clip_hash).
     */
    static inline uint64_t clip_hash(uint64_t h) noexcept {
        return duo_hashmap_clip_hash(h);
    }

    /**
     * @brief Rehashes the set to hold at least bucket_count slots.
     *
     * @param bucket_count Target bucket capacity hint.
     * @return true on success, false on OOM.
     */
    inline bool rehash(size_t bucket_count) noexcept {
        if (!m_inner.raw && !init(bucket_count)) return false;
        return rehash_impl(bucket_count);
    }

    /**
     * @brief Pre-reserves capacity to hold at least count elements without triggering rehashing.
     *
     * @param count Minimum number of elements to reserve space for.
     * @return true on success, false on OOM.
     */
    inline bool reserve(size_t count) noexcept {
        if (!m_inner.raw) init();
        double factor = max_load_factor();
        size_t needed = static_cast<size_t>(count / factor) + 1;
        return rehash(needed);
    }

    /**
     * @brief Checks if most recent allocation or resize attempt failed due to OOM (matching pure C duo_hashset_oom).
     */
    inline bool oom() const noexcept {
        return m_inner.raw ? duo_hashset_oom(m_inner.raw) : false;
    }

    /**
     * @brief Compares two items for equality using the configured EqualFn (matching pure C duo_hashmap_compare_keys).
     */
    inline bool compare_keys(const T& a, const T& b) const noexcept {
        return EqualFn{}(a, b);
    }


    /**
     * @brief Clamps a load factor within valid range (matching duo_hashmap_clamp_load_factor).
     */
    static inline double clamp_load_factor(double factor, double default_factor = DUO_HASHMAP_LOAD_FACTOR) noexcept {
        return duo_hashmap_clamp_load_factor(factor, default_factor);
    }

    /**
     * @brief Explicit capacity resizing alias matching duo_hashmap_resize.
     */
    inline bool resize(size_t new_cap) noexcept {
        return rehash(new_cap);
    }

    /**
     * @brief Destroys active elements while preserving table structure (matching duo_hashmap_free_elements).
     */
    inline void free_elements() noexcept {
        clear_impl(false);
    }

    /**
     * @brief Swaps contents of this set with another set in O(1) time without heap copies.
     */
    inline void swap(HashSet& other) noexcept {
        duo::swap(m_inner.raw, other.m_inner.raw);
    }

    /**
     * @brief Equality comparison testing if two sets contain identical elements.
     */
    inline bool operator==(const HashSet& other) const {
        if (this == &other) return true;
        if (size() != other.size()) return false;
        for (const auto& item : *this) {
            if (!other.contains(item)) return false;
        }
        return true;
    }

    /**
     * @brief Inequality comparison testing whether two sets differ.
     */
    inline bool operator!=(const HashSet& other) const {
        return !(*this == other);
    }

    // ------------------------------------------------------------------------
    // Dual-ABI C Interoperability
    // ------------------------------------------------------------------------

    /**
     * @brief Returns direct pointer to underlying pure C duo_hashset_t handle.
     */
    inline duo_hashset_t* raw() const noexcept { return m_inner.raw; }

    /**
     * @brief Implicit conversion operator to pure C duo_hashset_t pointer.
     */
    inline operator duo_hashset_t*() const noexcept { return m_inner.raw; }

    /**
     * @brief Returns address of internal duo_hashset_t handle pointer.
     */
    inline duo_hashset_t** raw_ptr() noexcept { return &m_inner.raw; }

    /**
     * @brief Adopts an existing raw pure C duo_hashset_t pointer into an owning C++ HashSet container.
     */
    static inline HashSet adopt(duo_hashset_t* raw_handle) noexcept {
        HashSet s;
        s.m_inner.raw = raw_handle;
        return s;
    }

    DUO_CXX_FFI_OPS(m_inner)
};

/**
 * @brief Freestanding swap overload for HashSet supporting ADL.
 *
 * @tparam T Element type.
 * @tparam H Hash functor type.
 * @tparam E Equality functor type.
 * @param a First set.
 * @param b Second set.
 */
template <typename T, typename H, typename E>
inline void swap(HashSet<T, H, E>& a, HashSet<T, H, E>& b) noexcept {
    a.swap(b);
}

// Static ABI assertions for HashSet
static_assert(sizeof(c_hashset_t<int>) == sizeof(void*), "c_hashset_t<T> must be 1 pointer (8 bytes)!");
static_assert(sizeof(HashSet<int>) == sizeof(void*), "HashSet<T> must be 1 pointer (8 bytes)!");
static_assert(is_standard_layout<c_hashset_t<int>>::value, "c_hashset_t<T> must be standard layout!");
static_assert(is_standard_layout<HashSet<int>>::value, "HashSet<T> must be standard layout!");
static_assert(is_trivially_copyable<c_hashset_t<int>>::value, "c_hashset_t<T> must be trivially copyable!");

} // namespace duo

#endif /* DUOSTL_HASH_HPP */
