#ifndef DUO_ALLOC_H
#define DUO_ALLOC_H

/* ============================================================================
 * duo_alloc.h - Freestanding Memory Allocation & Core Foundation (C11)
 *
 * Part of DuoSTL: Zero-dependency embedded container library for SQLite extensions
 * and high-performance native systems.
 *
 * Overview:
 *   This header defines the foundational memory management layer used by all
 *   DuoSTL containers—both linear (Vector, HeapArray, String) and
 *   associative (HashMap, HashSet).
 *
 * Pluggable Memory Allocators:
 *   By default, DuoSTL routes allocations to the standard C library runtime
 *   (malloc, realloc, free). Embedders can retarget all allocations to custom
 *   memory pools, arena allocators, or SQLite's memory subsystem (e.g. sqlite3_malloc64)
 *   by predefining the DUO_MALLOC, DUO_REALLOC, and DUO_FREE macros before
 *   including any DuoSTL header:
 *
 *     #define DUO_MALLOC(sz)       sqlite3_malloc64(sz)
 *     #define DUO_REALLOC(ptr, sz) sqlite3_realloc64(ptr, sz)
 *     #define DUO_FREE(ptr)        sqlite3_free(ptr)
 *     #include "duo_alloc.h"
 *
 * Dual-ABI Compatibility:
 *   Compatible with C11 (ISO/IEC 9899:2011) and C++17 (ISO/IEC 14882:2017).
 * ============================================================================ */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. CONFIGURABLE ALLOCATOR MACROS
 * ============================================================================ */
#ifndef DUO_MALLOC
  #if defined(SQLITE_CORE) || defined(SQLITE_API) || defined(_SQLITE3_H_) || defined(SQLITE3_H) || defined(SQLITE_VERSION)
    /**
     * @def DUO_MALLOC(sz)
     * @brief Allocates an uninitialized memory block using SQLite's tracked memory manager.
     */
    #define DUO_MALLOC(sz)       sqlite3_malloc64((sqlite3_uint64)(sz))

    /**
     * @def DUO_REALLOC(ptr, sz)
     * @brief Reallocates an existing memory block using SQLite's tracked memory manager.
     */
    #define DUO_REALLOC(ptr, sz) sqlite3_realloc64((ptr), (sqlite3_uint64)(sz))

    /**
     * @def DUO_FREE(ptr)
     * @brief Deallocates a previously allocated memory block via SQLite.
     */
    #define DUO_FREE(ptr)        sqlite3_free(ptr)
  #else
    #include <stdlib.h>
    /**
     * @def DUO_MALLOC(sz)
     * @brief Allocates an uninitialized contiguous memory block of @p sz bytes.
     * @param sz Number of bytes to allocate.
     * @return Pointer to allocated memory, or NULL if allocation fails.
     */
    #define DUO_MALLOC(sz)       malloc(sz)

    /**
     * @def DUO_REALLOC(ptr, sz)
     * @brief Reallocates an existing memory block to a new capacity of @p sz bytes.
     * @param ptr Pointer to the previously allocated block (or NULL to allocate fresh).
     * @param sz New requested size in bytes.
     * @return Pointer to resized memory block, or NULL on failure (original block remains intact).
     */
    #define DUO_REALLOC(ptr, sz) realloc(ptr, sz)

    /**
     * @def DUO_FREE(ptr)
     * @brief Deallocates a previously allocated memory block.
     * @param ptr Pointer to block to release (safe if NULL).
     */
    #define DUO_FREE(ptr)        free(ptr)
  #endif
#endif

/* ============================================================================
 * 2. COMPILE-TIME STATIC ASSERTION MACRO
 * ============================================================================ */
#if defined(__cplusplus)
  /**
   * @def DUO_STATIC_ASSERT(cond, msg)
   * @brief Dual-mode compile-time static assertion (C++ static_assert / C11 _Static_assert).
   * @param cond Constant boolean expression evaluated at compile time.
   * @param msg String literal explanation displayed on assertion failure.
   */
  #define DUO_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  #define DUO_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
  #define DUO_STATIC_ASSERT(cond, msg)
#endif

/* ============================================================================
 * 3. FREESTANDING MEMORY TRANSFER MACROS
 *
 * Direct memory copy, move, and set operations delegating to standard primitives
 * (memcpy, memmove, memset).
 * ============================================================================ */
#ifndef DUO_MEMCPY
#define DUO_MEMCPY(dst, src, sz) memcpy((dst), (src), (sz))
#endif

#ifndef DUO_MEMMOVE
#define DUO_MEMMOVE(dst, src, sz) memmove((dst), (src), (sz))
#endif

#ifndef DUO_MEMSET
#define DUO_MEMSET(dst, val, sz) memset((dst), (val), (sz))
#endif

/* ============================================================================
 * 4. MATHEMATICAL ALLOCATION & STACK TIER ALIGNMENT UTILITIES
 * ============================================================================ */

/**
 * @brief Computes a fine-grained sub-octave capacity tier (~1.25x growth, at most 25% slack)
 * via branchless bit-twiddling.
 *
 * @details
 * Partitions each power-of-two octave [B, 2B] into 4 linear steps of size S = B / 4 (25% increment).
 *
 * 1. Clamps values below (min_base >> 2) to the minimum tier (min_base >> 2).
 * 2. Rejects values exceeding max_val (returns 0 to signal dynamic heap fallback).
 * 3. Smears the highest set bit downwards on (n - 1) across 1, 2, 4, 8 bit shifts to produce
 *    an all-ones mask 2^(k+1) - 1.
 * 4. Extracts the base power-of-two floor B = 2^k via `(x >> 1) + 1`, clamped to min_base.
 * 5. Computes step size S = B / 4 (`B >> 2`) and rounds up branchlessly:
 *    `(n + S - 1) & ~(S - 1)`.
 *
 * @param n Requested count (bytes, words, or elements).
 * @param max_val Maximum value eligible for stack tier (returns 0 if n > max_val).
 * @param min_base Base power-of-two floor threshold (e.g. 32 for bytes, 4 for 64-bit words).
 * @return Aligned tier capacity, or 0 if n > max_val.
 */
static inline size_t duo_sub_octave_tier(size_t n, size_t max_val, size_t min_base) {
    if (n > max_val) {
        return 0;
    }
    size_t min_tier = min_base >> 2;
    if (n <= min_tier) {
        return min_tier;
    }

    /* Smear highest set bit downwards to create all-ones mask: 2^(k+1) - 1 */
    size_t x = n - 1;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;

    /* Base power-of-two floor B = 2^k (clamped to min_base) */
    size_t B = (x >> 1) + 1;
    if (B < min_base) B = min_base;

    /* Step size S = B / 4 (25% of base power-of-two); round up to multiple of S */
    size_t S = B >> 2;
    return (n + S - 1) & ~(S - 1);
}

/**
 * @brief Computes the next capacity tier via 3-tier adaptive geometric growth:
 * - < 256:   2.0x (or 4 if starting from 0, for rapid small-buffer expansion)
 * - < 4096:  1.5x (cap + cap/2, strictly below Golden Ratio 1.618 to enable heap coalescing)
 * - >= 4096: 1.25x (cap + cap/4, caps slack at <= 25% for large multi-page allocations)
 *
 * @param cur_cap Current capacity of the dynamic container.
 * @return Next geometric capacity tier, or (size_t)-1 on integer overflow.
 */
static inline size_t duo_geometric_grow_cap(size_t cur_cap) {
    size_t new_cap;
    if (cur_cap < 256) {
        new_cap = (cur_cap < 4) ? 4 : (cur_cap << 1);    /* 2.0x (minimum base 4) */
    } else if (cur_cap < 4096) {
        new_cap = cur_cap + (cur_cap >> 1);               /* 1.5x */
    } else {
        new_cap = cur_cap + (cur_cap >> 2);               /* 1.25x */
    }

    /* Integer overflow check */
    return (new_cap < cur_cap) ? (size_t)-1 : new_cap;
}

/* ============================================================================
 * 4.5. MACRO-SYNTHESIZED COMPARISON OPERATORS (C11)
 * ============================================================================ */
#ifndef DUO_C_DERIVE_CMP_OPS
/**
 * @def DUO_C_DERIVE_CMP_OPS(Prefix, Type, CmpFn)
 * @brief Generates full 6-way relational comparison suite (_eq, _ne, _lt, _le, _gt, _ge)
 * from a 3-way comparator function returning <0, 0, or >0.
 */
#define DUO_C_DERIVE_CMP_OPS(Prefix, Type, CmpFn) \
    static inline bool Prefix##_eq(Type a, Type b) { return (CmpFn)(a, b) == 0; } \
    static inline bool Prefix##_ne(Type a, Type b) { return (CmpFn)(a, b) != 0; } \
    static inline bool Prefix##_lt(Type a, Type b) { return (CmpFn)(a, b) < 0; } \
    static inline bool Prefix##_le(Type a, Type b) { return (CmpFn)(a, b) <= 0; } \
    static inline bool Prefix##_gt(Type a, Type b) { return (CmpFn)(a, b) > 0; } \
    static inline bool Prefix##_ge(Type a, Type b) { return (CmpFn)(a, b) >= 0; }
#endif

/* ============================================================================
 * 5. STANDALONE 64-BIT HASHING ENGINE (xxHash3)
 * ============================================================================ */
#define DUO_XXH_PRIME_1 11400714785074694791ULL
#define DUO_XXH_PRIME_2 14029467366897019727ULL
#define DUO_XXH_PRIME_3 1609587929392839161ULL
#define DUO_XXH_PRIME_4 9650029242287828579ULL
#define DUO_XXH_PRIME_5 2870177450012600261ULL

static inline uint64_t duo_xxh_read64(const void* memptr) {
    uint64_t val;
    DUO_MEMCPY(&val, memptr, sizeof(val));
    return val;
}

static inline uint32_t duo_xxh_read32(const void* memptr) {
    uint32_t val;
    DUO_MEMCPY(&val, memptr, sizeof(val));
    return val;
}

static inline uint64_t duo_xxh_rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static inline uint64_t duo_xxh3_impl(const void* data, size_t len, uint64_t seed) {
    const uint8_t* p = (const uint8_t*)data;
    const uint8_t* const end = p + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t* const limit = end - 32;
        uint64_t v1 = seed + DUO_XXH_PRIME_1 + DUO_XXH_PRIME_2;
        uint64_t v2 = seed + DUO_XXH_PRIME_2;
        uint64_t v3 = seed + 0;
        uint64_t v4 = seed - DUO_XXH_PRIME_1;

        do {
            v1 += duo_xxh_read64(p) * DUO_XXH_PRIME_2;
            v1 = duo_xxh_rotl64(v1, 31);
            v1 *= DUO_XXH_PRIME_1;

            v2 += duo_xxh_read64(p + 8) * DUO_XXH_PRIME_2;
            v2 = duo_xxh_rotl64(v2, 31);
            v2 *= DUO_XXH_PRIME_1;

            v3 += duo_xxh_read64(p + 16) * DUO_XXH_PRIME_2;
            v3 = duo_xxh_rotl64(v3, 31);
            v3 *= DUO_XXH_PRIME_1;

            v4 += duo_xxh_read64(p + 24) * DUO_XXH_PRIME_2;
            v4 = duo_xxh_rotl64(v4, 31);
            v4 *= DUO_XXH_PRIME_1;

            p += 32;
        } while (p <= limit);

        h64 = duo_xxh_rotl64(v1, 1) + duo_xxh_rotl64(v2, 7) + duo_xxh_rotl64(v3, 12) + 
            duo_xxh_rotl64(v4, 18);

        v1 *= DUO_XXH_PRIME_2;
        v1 = duo_xxh_rotl64(v1, 31);
        v1 *= DUO_XXH_PRIME_1;
        h64 ^= v1;
        h64 = h64 * DUO_XXH_PRIME_1 + DUO_XXH_PRIME_4;

        v2 *= DUO_XXH_PRIME_2;
        v2 = duo_xxh_rotl64(v2, 31);
        v2 *= DUO_XXH_PRIME_1;
        h64 ^= v2;
        h64 = h64 * DUO_XXH_PRIME_1 + DUO_XXH_PRIME_4;

        v3 *= DUO_XXH_PRIME_2;
        v3 = duo_xxh_rotl64(v3, 31);
        v3 *= DUO_XXH_PRIME_1;
        h64 ^= v3;
        h64 = h64 * DUO_XXH_PRIME_1 + DUO_XXH_PRIME_4;

        v4 *= DUO_XXH_PRIME_2;
        v4 = duo_xxh_rotl64(v4, 31);
        v4 *= DUO_XXH_PRIME_1;
        h64 ^= v4;
        h64 = h64 * DUO_XXH_PRIME_1 + DUO_XXH_PRIME_4;
    }
    else {
        h64 = seed + DUO_XXH_PRIME_5;
    }

    h64 += (uint64_t)len;

    while (p + 8 <= end) {
        uint64_t k1 = duo_xxh_read64(p);
        k1 *= DUO_XXH_PRIME_2;
        k1 = duo_xxh_rotl64(k1, 31);
        k1 *= DUO_XXH_PRIME_1;
        h64 ^= k1;
        h64 = duo_xxh_rotl64(h64, 27) * DUO_XXH_PRIME_1 + DUO_XXH_PRIME_4;
        p += 8;
    }

    if (p + 4 <= end) {
        h64 ^= (uint64_t)(duo_xxh_read32(p)) * DUO_XXH_PRIME_1;
        h64 = duo_xxh_rotl64(h64, 23) * DUO_XXH_PRIME_2 + DUO_XXH_PRIME_3;
        p += 4;
    }

    while (p < end) {
        h64 ^= (*p) * DUO_XXH_PRIME_5;
        h64 = duo_xxh_rotl64(h64, 11) * DUO_XXH_PRIME_1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= DUO_XXH_PRIME_2;
    h64 ^= h64 >> 29;
    h64 *= DUO_XXH_PRIME_3;
    h64 ^= h64 >> 32;

    return h64;
}

#undef DUO_XXH_PRIME_1
#undef DUO_XXH_PRIME_2
#undef DUO_XXH_PRIME_3
#undef DUO_XXH_PRIME_4
#undef DUO_XXH_PRIME_5

/**
 * @brief Computes a 64-bit xxHash3 digest (default high-performance algorithm).
 *
 * Fast, alignment-safe, and free from undefined behavior.
 *
 * @param data  Pointer to the input buffer.
 * @param len   Length of the input buffer in bytes.
 * @param seed0 64-bit seed.
 * @param seed1 Unused (present for hash signature uniformity).
 * @return 64-bit hash value.
 */
static inline uint64_t duo_hash_xxhash3(const void *data, size_t len, uint64_t seed0, uint64_t seed1) {
    (void)seed1;
    return duo_xxh3_impl(data, len, seed0);
}

#ifdef __cplusplus
}
#endif

#endif /* DUO_ALLOC_H */

