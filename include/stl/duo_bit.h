#ifndef DUO_BIT_H
#define DUO_BIT_H

/* ============================================================================
 * duo_bit.h - Dual-ABI Bit Manipulation Engine, Bit Spans & Bit Vectors (C11)
 *
 * Part of DuoSTL: Zero-dependency embedded container library for SQLite extensions
 * and high-performance native systems.
 *
 * This header provides the pure C foundation for bit manipulation:
 *   - Hardware CTZ and POPCNT intrinsics
 *   - LSB-first packed bit operations on 64-bit word buffers
 *   - Non-owning bit slices: duo_bitview_t (const) and duo_bitspan_t (mutable)
 *   - Owning fixed-size heap bit array: duo_bitarray_t
 *   - Stack / fixed bit array macros: duo_fixed_bitarray_t
 *   - Dynamic growable SBO bit vector: duo_bitvec_t (128 inline bits)
 *   - Stack / fixed growable bit vector: duo_fixed_bitvec_t
 *
 * All structs are standard-layout, zero-overhead, and ABI-compatible with their
 * C++ counterparts defined in duo_bit.hpp.
 * ============================================================================ */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifndef __cplusplus
  #ifndef bool
    typedef _Bool bool;
  #endif
  #ifndef true
    #define true 1
  #endif
  #ifndef false
    #define false 0
  #endif
#endif
#include <string.h>
#include "duo_alloc.h"
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 13. BIT MANIPULATION PRIMITIVES, BIT SPANS & BIT ARRAYS (C11 ABI)
 *
 * duo_bitspan_t:  Non-owning mutable bit slice   { size_t bit_count; uint64_t* words; }
 * duo_bitview_t:  Non-owning const bit slice     { size_t bit_count; const uint64_t* words; }
 * duo_bitarray_t: Owning heap-allocated bit array { size_t bit_count; uint64_t* words; }
 *
 * All 3 structs are exactly 16 bytes on 64-bit architectures, standard-layout,
 * packing bits into 64-bit words (uint64_t) LSB-first.
 * ============================================================================ */

#define DUO_BIT_NPOS ((size_t)-1)

typedef struct duo_bitspan {
    size_t    bit_count; /**< Number of valid bits in span (Offset 0). */
    uint64_t* words;     /**< Word-aligned pointer to bit buffer (Offset 8). */
} duo_bitspan_t;

typedef struct duo_bitview {
    size_t          bit_count; /**< Number of valid bits in view (Offset 0). */
    const uint64_t* words;     /**< Word-aligned const pointer to bit buffer (Offset 8). */
} duo_bitview_t;

typedef struct duo_bitarray {
    size_t    bit_count; /**< Fixed number of bits (Offset 0). */
    uint64_t* words;     /**< Heap-allocated word buffer via DUO_MALLOC (Offset 8). */
} duo_bitarray_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__cplusplus)
_Static_assert(sizeof(duo_bitspan_t) == 16, "duo_bitspan_t must be exactly 16 bytes");
_Static_assert(sizeof(duo_bitview_t) == 16, "duo_bitview_t must be exactly 16 bytes");
_Static_assert(sizeof(duo_bitarray_t) == 16, "duo_bitarray_t must be exactly 16 bytes");
#endif

/**
 * @brief Computes the number of 64-bit words required to store @p bit_count bits.
 */
static inline size_t duo_bit_words_for_bits(size_t bit_count) {
    return (bit_count + 63) / 64;
}

/**
 * @brief Zeroes out any unused tail bits in the final 64-bit word past @p bit_count.
 */
static inline void duo_bit_sanitize_tail(uint64_t* words, size_t bit_count) {
    if (words && bit_count > 0) {
        size_t rem = bit_count % 64;
        if (rem != 0) {
            words[bit_count / 64] &= ((1ULL << rem) - 1ULL);
        }
    }
}

/**
 * @brief Software fallback for population count (Hamming weight) of a 64-bit word using SWAR.
 * @param w 64-bit word.
 * @return Number of set bits (0 to 64).
 */
static inline int duo_bit_popcount64_fallback(uint64_t w) {
    w = w - ((w >> 1) & 0x5555555555555555ULL);
    w = (w & 0x3333333333333333ULL) + ((w >> 2) & 0x3333333333333333ULL);
    return (int)((((w + (w >> 4)) & 0x0F0F0F0F0F0F0F0FULL) * 0x0101010101010101ULL) >> 56);
}

/**
 * @brief Software fallback for Count Trailing Zeros (CTZ) of a 64-bit word using binary search.
 * @param w Non-zero 64-bit word (requires w != 0).
 * @return Number of trailing zeros (0 to 63).
 */
static inline int duo_bit_ctz64_fallback(uint64_t w) {
    int c = 0;
    if ((w & 0xFFFFFFFFULL) == 0) { c += 32; w >>= 32; }
    if ((w & 0xFFFFULL) == 0) { c += 16; w >>= 16; }
    if ((w & 0xFFULL) == 0) { c += 8; w >>= 8; }
    if ((w & 0xFULL) == 0) { c += 4; w >>= 4; }
    if ((w & 0x3ULL) == 0) { c += 2; w >>= 2; }
    if ((w & 0x1ULL) == 0) { c += 1; }
    return c;
}

/**
 * @brief Hardware-accelerated population count (Hamming weight) of a 64-bit word.
 * Dispatches to hardware intrinsic (__popcnt64 / __builtin_popcountll) when available,
 * or duo_bit_popcount64_fallback otherwise.
 * @param w 64-bit word.
 * @return Number of set bits (0 to 64).
 */
static inline int duo_bit_popcount64(uint64_t w) {
#if defined(_MSC_VER)
    return (int)__popcnt64(w);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(w);
#else
    return duo_bit_popcount64_fallback(w);
#endif
}

/**
 * @brief Hardware-accelerated Count Trailing Zeros (CTZ) of a 64-bit word.
 * Requires @p w != 0. Dispatches to hardware intrinsic (_BitScanForward64 / __builtin_ctzll)
 * when available, or duo_bit_ctz64_fallback otherwise.
 * @param w Non-zero 64-bit word.
 * @return Number of trailing zeros (0 to 63).
 */
static inline int duo_bit_ctz64(uint64_t w) {
#if defined(_MSC_VER)
    unsigned long idx = 0;
    _BitScanForward64(&idx, w);
    return (int)idx;
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(w);
#else
    return duo_bit_ctz64_fallback(w);
#endif
}

/**
 * @brief Gets the bit at @p idx from a 64-bit word array.
 */
static inline bool duo_bit_get(const uint64_t* words, size_t idx) {
    return (bool)((words[idx / 64] >> (idx % 64)) & 1ULL);
}

/**
 * @brief Sets the bit at @p idx in a 64-bit word array to @p val.
 */
static inline void duo_bit_set(uint64_t* words, size_t idx, bool val) {
    if (val) {
        words[idx / 64] |= (1ULL << (idx % 64));
    } else {
        words[idx / 64] &= ~(1ULL << (idx % 64));
    }
}

/**
 * @brief Inverts the bit at @p idx in a 64-bit word array.
 */
static inline void duo_bit_flip(uint64_t* words, size_t idx) {
    words[idx / 64] ^= (1ULL << (idx % 64));
}

/**
 * @brief Semantic alias for duo_bit_get.
 */
static inline bool duo_bit_test(const uint64_t* words, size_t idx) {
    return duo_bit_get(words, idx);
}

/**
 * @brief Counts the total number of set bits (ones) across @p bit_count bits.
 */
static inline size_t duo_bit_count_ones(const uint64_t* words, size_t bit_count) {
    if (!words || bit_count == 0) return 0;
    size_t num_words = (bit_count + 63) / 64;
    size_t total = 0;
    for (size_t i = 0; i < num_words; ++i) {
        uint64_t w = words[i];
        if (i == num_words - 1) {
            size_t rem = bit_count % 64;
            if (rem != 0) {
                w &= ((1ULL << rem) - 1ULL);
            }
        }
        total += (size_t)duo_bit_popcount64(w);
    }
    return total;
}

/**
 * @brief Counts the total number of cleared bits (zeros) across @p bit_count bits.
 */
static inline size_t duo_bit_count_zeros(const uint64_t* words, size_t bit_count) {
    return bit_count - duo_bit_count_ones(words, bit_count);
}

/**
 * @brief Returns true if all @p bit_count bits are set to 1.
 */
static inline bool duo_bit_all(const uint64_t* words, size_t bit_count) {
    if (bit_count == 0) return true;
    return duo_bit_count_ones(words, bit_count) == bit_count;
}

/**
 * @brief Returns true if at least one of the @p bit_count bits is set to 1.
 */
static inline bool duo_bit_any(const uint64_t* words, size_t bit_count) {
    if (bit_count == 0) return false;
    return duo_bit_count_ones(words, bit_count) > 0;
}

/**
 * @brief Returns true if none of the @p bit_count bits are set to 1.
 */
static inline bool duo_bit_none(const uint64_t* words, size_t bit_count) {
    if (bit_count == 0) return true;
    return duo_bit_count_ones(words, bit_count) == 0;
}

/**
 * @brief Finds the index of the first set bit (1), or DUO_BIT_NPOS if none are set.
 */
static inline size_t duo_bit_find_first(const uint64_t* words, size_t bit_count) {
    if (!words || bit_count == 0) return DUO_BIT_NPOS;
    size_t num_words = (bit_count + 63) / 64;
    for (size_t i = 0; i < num_words; ++i) {
        uint64_t w = words[i];
        if (i == num_words - 1) {
            size_t rem = bit_count % 64;
            if (rem != 0) {
                w &= ((1ULL << rem) - 1ULL);
            }
        }
        if (w != 0) {
            size_t bit = i * 64 + (size_t)duo_bit_ctz64(w);
            return (bit < bit_count) ? bit : DUO_BIT_NPOS;
        }
    }
    return DUO_BIT_NPOS;
}

/**
 * @brief Finds the index of the next set bit strictly after @p prev_idx, or DUO_BIT_NPOS if none.
 */
static inline size_t duo_bit_find_next(const uint64_t* words, size_t bit_count, size_t prev_idx) {
    if (!words || bit_count == 0 || prev_idx >= bit_count - 1) return DUO_BIT_NPOS;
    size_t start_bit = prev_idx + 1;
    size_t start_word = start_bit / 64;
    size_t num_words = (bit_count + 63) / 64;

    uint64_t w = words[start_word];
    w &= (~0ULL << (start_bit % 64));
    if (start_word == num_words - 1) {
        size_t rem = bit_count % 64;
        if (rem != 0) {
            w &= ((1ULL << rem) - 1ULL);
        }
    }
    if (w != 0) {
        size_t bit = start_word * 64 + (size_t)duo_bit_ctz64(w);
        return (bit < bit_count) ? bit : DUO_BIT_NPOS;
    }

    for (size_t i = start_word + 1; i < num_words; ++i) {
        w = words[i];
        if (i == num_words - 1) {
            size_t rem = bit_count % 64;
            if (rem != 0) {
                w &= ((1ULL << rem) - 1ULL);
            }
        }
        if (w != 0) {
            size_t bit = i * 64 + (size_t)duo_bit_ctz64(w);
            return (bit < bit_count) ? bit : DUO_BIT_NPOS;
        }
    }
    return DUO_BIT_NPOS;
}

/**
 * @brief Sets all @p bit_count bits to 1, sanitizing unused tail bits.
 */
static inline void duo_bit_set_all(uint64_t* words, size_t bit_count) {
    if (!words || bit_count == 0) return;
    size_t num_words = (bit_count + 63) / 64;
    DUO_MEMSET(words, 0xFF, num_words * sizeof(uint64_t));
    duo_bit_sanitize_tail(words, bit_count);
}

/**
 * @brief Clears all @p bit_count bits to 0.
 */
static inline void duo_bit_clear_all(uint64_t* words, size_t bit_count) {
    if (!words || bit_count == 0) return;
    size_t num_words = (bit_count + 63) / 64;
    DUO_MEMSET(words, 0, num_words * sizeof(uint64_t));
}

/**
 * @brief Inverts all @p bit_count bits, sanitizing unused tail bits.
 */
static inline void duo_bit_flip_all(uint64_t* words, size_t bit_count) {
    if (!words || bit_count == 0) return;
    size_t num_words = (bit_count + 63) / 64;
    for (size_t i = 0; i < num_words; ++i) {
        words[i] = ~words[i];
    }
    duo_bit_sanitize_tail(words, bit_count);
}

/**
 * @brief Bitwise AND: dst &= src across @p bit_count bits, sanitizing tail bits.
 */
static inline void duo_bit_and(uint64_t* dst, const uint64_t* src, size_t bit_count) {
    if (!dst || !src || bit_count == 0) return;
    size_t num_words = (bit_count + 63) / 64;
    for (size_t i = 0; i < num_words; ++i) {
        dst[i] &= src[i];
    }
    duo_bit_sanitize_tail(dst, bit_count);
}

/**
 * @brief Bitwise OR: dst |= src across @p bit_count bits, sanitizing tail bits.
 */
static inline void duo_bit_or(uint64_t* dst, const uint64_t* src, size_t bit_count) {
    if (!dst || !src || bit_count == 0) return;
    size_t num_words = (bit_count + 63) / 64;
    for (size_t i = 0; i < num_words; ++i) {
        dst[i] |= src[i];
    }
    duo_bit_sanitize_tail(dst, bit_count);
}

/**
 * @brief Bitwise XOR: dst ^= src across @p bit_count bits, sanitizing tail bits.
 */
static inline void duo_bit_xor(uint64_t* dst, const uint64_t* src, size_t bit_count) {
    if (!dst || !src || bit_count == 0) return;
    size_t num_words = (bit_count + 63) / 64;
    for (size_t i = 0; i < num_words; ++i) {
        dst[i] ^= src[i];
    }
    duo_bit_sanitize_tail(dst, bit_count);
}

/**
 * @brief Bitwise NOT: dst = ~src across @p bit_count bits, sanitizing tail bits.
 */
static inline void duo_bit_not(uint64_t* dst, const uint64_t* src, size_t bit_count) {
    if (!dst || !src || bit_count == 0) return;
    size_t num_words = (bit_count + 63) / 64;
    for (size_t i = 0; i < num_words; ++i) {
        dst[i] = ~src[i];
    }
    duo_bit_sanitize_tail(dst, bit_count);
}

/**
 * @brief Compares two bit buffers of length @p bit_count for bitwise equality.
 */
static inline bool duo_bit_equal(const uint64_t* a, const uint64_t* b, size_t bit_count) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (bit_count == 0) return true;
    size_t num_words = (bit_count + 63) / 64;
    return memcmp(a, b, num_words * sizeof(uint64_t)) == 0;
}

/**
 * @brief Lexicographically compares two bit buffers of lengths @p bit_count_a and @p bit_count_b.
 * Returns -1 if a < b, 1 if a > b, 0 if identical.
 */
static inline int duo_bit_cmp(const uint64_t* a, size_t bit_count_a, const uint64_t* b, size_t bit_count_b) {
    if (a == b && bit_count_a == bit_count_b) return 0;
    size_t min_bits = bit_count_a < bit_count_b ? bit_count_a : bit_count_b;
    size_t full_words = min_bits / 64;

    for (size_t i = 0; i < full_words; ++i) {
        if (a[i] != b[i]) {
            uint64_t diff = a[i] ^ b[i];
            int bit_idx = duo_bit_ctz64(diff);
            return ((a[i] >> bit_idx) & 1ULL) ? 1 : -1;
        }
    }

    size_t rem = min_bits % 64;
    if (rem != 0) {
        uint64_t mask = (1ULL << rem) - 1ULL;
        uint64_t wa = a[full_words] & mask;
        uint64_t wb = b[full_words] & mask;
        if (wa != wb) {
            uint64_t diff = wa ^ wb;
            int bit_idx = duo_bit_ctz64(diff);
            return ((wa >> bit_idx) & 1ULL) ? 1 : -1;
        }
    }

    return (bit_count_a < bit_count_b) ? -1 : ((bit_count_a > bit_count_b) ? 1 : 0);
}

/**
 * @brief Computes 64-bit xxHash3 digest of a bit buffer of @p bit_count bits with tail sanitization.
 */
static inline uint64_t duo_bit_hash(const uint64_t* words, size_t bit_count, uint64_t seed0, uint64_t seed1) {
    if (!words || bit_count == 0) {
        return duo_hash_xxhash3(NULL, 0, seed0 ^ bit_count, seed1);
    }
    size_t num_bytes = (bit_count + 7) / 8;
    return duo_hash_xxhash3(words, num_bytes, seed0 ^ bit_count, seed1);
}

/* ----------------------------------------------------------------------------
 * duo_bitspan_t & duo_bitview_t Constructors and Methods
 * ---------------------------------------------------------------------------- */

/**
 * @brief Constructs a mutable bit span viewing an existing word buffer.
 * @param words Word-aligned pointer to uint64_t buffer.
 * @param bit_count Number of valid bits in the span.
 * @return Constructed duo_bitspan_t.
 */
static inline duo_bitspan_t duo_bitspan_make(uint64_t* words, size_t bit_count) {
    duo_bitspan_t s;
    s.bit_count = bit_count;
    s.words = words;
    return s;
}

/**
 * @brief Constructs an immutable bit view viewing an existing word buffer.
 * @param words Word-aligned const pointer to uint64_t buffer.
 * @param bit_count Number of valid bits in the view.
 * @return Constructed duo_bitview_t.
 */
static inline duo_bitview_t duo_bitview_make(const uint64_t* words, size_t bit_count) {
    duo_bitview_t v;
    v.bit_count = bit_count;
    v.words = words;
    return v;
}

/**
 * @brief Converts a mutable bit span into an immutable bit view in O(1).
 * @param s Source bit span.
 * @return Const bit view referencing the same bit buffer.
 */
static inline duo_bitview_t duo_bitspan_to_view(duo_bitspan_t s) {
    duo_bitview_t v;
    v.bit_count = s.bit_count;
    v.words = s.words;
    return v;
}

/**
 * @brief Returns the total bit capacity of bit span @p s.
 * @param s Bit span.
 * @return Total number of valid bits.
 */
static inline size_t duo_bitspan_size(duo_bitspan_t s) { return s.bit_count; }

/**
 * @brief Returns the number of 64-bit words backing bit span @p s.
 * @param s Bit span.
 * @return Word count (ceil(bit_count / 64)).
 */
static inline size_t duo_bitspan_words(duo_bitspan_t s) { return (s.bit_count + 63) / 64; }

/**
 * @brief Returns true if bit span @p s contains 0 bits.
 * @param s Bit span.
 * @return true if empty, false otherwise.
 */
static inline bool   duo_bitspan_empty(duo_bitspan_t s) { return s.bit_count == 0; }

/**
 * @brief Gets the bit at index @p idx from bit span @p s.
 * @param s Bit span.
 * @param idx Zero-based bit index.
 * @return Boolean bit value (true for 1, false for 0).
 */
static inline bool   duo_bitspan_get(duo_bitspan_t s, size_t idx) { return duo_bit_get(s.words, idx); }

/**
 * @brief Sets the bit at index @p idx in bit span @p s to @p val.
 * @param s Bit span.
 * @param idx Zero-based bit index.
 * @param val Boolean value to set.
 */
static inline void   duo_bitspan_set(duo_bitspan_t s, size_t idx, bool val) { duo_bit_set(s.words, idx, val); }

/**
 * @brief Inverts the bit at index @p idx in bit span @p s.
 * @param s Bit span.
 * @param idx Zero-based bit index.
 */
static inline void   duo_bitspan_flip(duo_bitspan_t s, size_t idx) { duo_bit_flip(s.words, idx); }

/**
 * @brief Semantic alias for duo_bitspan_get.
 * @param s Bit span.
 * @param idx Zero-based bit index.
 * @return Boolean bit value.
 */
static inline bool   duo_bitspan_test(duo_bitspan_t s, size_t idx) { return duo_bit_test(s.words, idx); }

/**
 * @brief Sets all bits in bit span @p s to 1, sanitizing tail bits.
 * @param s Bit span.
 */
static inline void   duo_bitspan_set_all(duo_bitspan_t s) { duo_bit_set_all(s.words, s.bit_count); }

/**
 * @brief Clears all bits in bit span @p s to 0.
 * @param s Bit span.
 */
static inline void   duo_bitspan_clear_all(duo_bitspan_t s) { duo_bit_clear_all(s.words, s.bit_count); }

/**
 * @brief Inverts all bits in bit span @p s, sanitizing tail bits.
 * @param s Bit span.
 */
static inline void   duo_bitspan_flip_all(duo_bitspan_t s) { duo_bit_flip_all(s.words, s.bit_count); }

/**
 * @brief Counts set bits (ones) in bit span @p s using hardware POPCNT.
 * @param s Bit span.
 * @return Population count (Hamming weight).
 */
static inline size_t duo_bitspan_count_ones(duo_bitspan_t s) { return duo_bit_count_ones(s.words, s.bit_count); }

/**
 * @brief Counts cleared bits (zeros) in bit span @p s.
 * @param s Bit span.
 * @return Number of zero bits.
 */
static inline size_t duo_bitspan_count_zeros(duo_bitspan_t s) { return duo_bit_count_zeros(s.words, s.bit_count); }

/**
 * @brief Returns true if all bits in bit span @p s are set to 1.
 * @param s Bit span.
 * @return true if all bits are 1, false otherwise.
 */
static inline bool   duo_bitspan_all(duo_bitspan_t s) { return duo_bit_all(s.words, s.bit_count); }

/**
 * @brief Returns true if at least one bit in bit span @p s is set to 1.
 * @param s Bit span.
 * @return true if any bit is 1, false otherwise.
 */
static inline bool   duo_bitspan_any(duo_bitspan_t s) { return duo_bit_any(s.words, s.bit_count); }

/**
 * @brief Returns true if none of the bits in bit span @p s are set to 1.
 * @param s Bit span.
 * @return true if no bits are 1, false otherwise.
 */
static inline bool   duo_bitspan_none(duo_bitspan_t s) { return duo_bit_none(s.words, s.bit_count); }

/**
 * @brief Finds the index of the first set bit in bit span @p s, or DUO_BIT_NPOS if none.
 * @param s Bit span.
 * @return Zero-based bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_bitspan_find_first(duo_bitspan_t s) { return duo_bit_find_first(s.words, s.bit_count); }

/**
 * @brief Finds the index of the next set bit strictly after @p prev_idx in bit span @p s.
 * @param s Bit span.
 * @param prev_idx Previous set bit index.
 * @return Next set bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_bitspan_find_next(duo_bitspan_t s, size_t prev_idx) { return duo_bit_find_next(s.words, s.bit_count, prev_idx); }

/**
 * @brief In-place bitwise AND: dst &= src across common bits.
 * @param dst Destination mutable bit span.
 * @param src Source immutable bit view.
 */
static inline void duo_bitspan_and(duo_bitspan_t dst, duo_bitview_t src) {
    size_t count = dst.bit_count < src.bit_count ? dst.bit_count : src.bit_count;
    duo_bit_and(dst.words, src.words, count);
}

/**
 * @brief In-place bitwise OR: dst |= src across common bits.
 * @param dst Destination mutable bit span.
 * @param src Source immutable bit view.
 */
static inline void duo_bitspan_or(duo_bitspan_t dst, duo_bitview_t src) {
    size_t count = dst.bit_count < src.bit_count ? dst.bit_count : src.bit_count;
    duo_bit_or(dst.words, src.words, count);
}

/**
 * @brief In-place bitwise XOR: dst ^= src across common bits.
 * @param dst Destination mutable bit span.
 * @param src Source immutable bit view.
 */
static inline void duo_bitspan_xor(duo_bitspan_t dst, duo_bitview_t src) {
    size_t count = dst.bit_count < src.bit_count ? dst.bit_count : src.bit_count;
    duo_bit_xor(dst.words, src.words, count);
}

/**
 * @brief In-place bitwise NOT: dst = ~src across common bits, sanitizing tail bits.
 * @param dst Destination mutable bit span.
 * @param src Source immutable bit view.
 */
static inline void duo_bitspan_not(duo_bitspan_t dst, duo_bitview_t src) {
    size_t count = dst.bit_count < src.bit_count ? dst.bit_count : src.bit_count;
    duo_bit_not(dst.words, src.words, count);
}

/**
 * @brief Lexicographical comparison of two bit spans.
 */
static inline int duo_bitspan_cmp(duo_bitspan_t a, duo_bitspan_t b) {
    return duo_bit_cmp(a.words, a.bit_count, b.words, b.bit_count);
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_bitspan_t:
 * duo_bitspan_eq, duo_bitspan_ne, duo_bitspan_lt, duo_bitspan_le,
 * duo_bitspan_gt, duo_bitspan_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_bitspan, duo_bitspan_t, duo_bitspan_cmp)

/**
 * @brief Computes 64-bit xxHash3 digest of bit span @p s.
 */
static inline uint64_t duo_bitspan_hash(duo_bitspan_t s) {
    return duo_bit_hash(s.words, s.bit_count, 0, 0);
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of bit span @p s.
 */
static inline uint64_t duo_bitspan_hash_seed(duo_bitspan_t s, uint64_t seed0, uint64_t seed1) {
    return duo_bit_hash(s.words, s.bit_count, seed0, seed1);
}

/**
 * @brief Returns the total bit count of bit view @p v.
 * @param v Bit view.
 * @return Total number of valid bits.
 */
static inline size_t duo_bitview_size(duo_bitview_t v) { return v.bit_count; }

/**
 * @brief Returns the number of 64-bit words backing bit view @p v.
 * @param v Bit view.
 * @return Word count (ceil(bit_count / 64)).
 */
static inline size_t duo_bitview_words(duo_bitview_t v) { return (v.bit_count + 63) / 64; }

/**
 * @brief Returns true if bit view @p v contains 0 bits.
 * @param v Bit view.
 * @return true if empty, false otherwise.
 */
static inline bool   duo_bitview_empty(duo_bitview_t v) { return v.bit_count == 0; }

/**
 * @brief Gets the bit at index @p idx from bit view @p v.
 * @param v Bit view.
 * @param idx Zero-based bit index.
 * @return Boolean bit value (true for 1, false for 0).
 */
static inline bool   duo_bitview_get(duo_bitview_t v, size_t idx) { return duo_bit_get(v.words, idx); }

/**
 * @brief Semantic alias for duo_bitview_get.
 * @param v Bit view.
 * @param idx Zero-based bit index.
 * @return Boolean bit value.
 */
static inline bool   duo_bitview_test(duo_bitview_t v, size_t idx) { return duo_bit_test(v.words, idx); }

/**
 * @brief Counts set bits (ones) in bit view @p v using hardware POPCNT.
 * @param v Bit view.
 * @return Population count (Hamming weight).
 */
static inline size_t duo_bitview_count_ones(duo_bitview_t v) { return duo_bit_count_ones(v.words, v.bit_count); }

/**
 * @brief Counts cleared bits (zeros) in bit view @p v.
 * @param v Bit view.
 * @return Number of zero bits.
 */
static inline size_t duo_bitview_count_zeros(duo_bitview_t v) { return duo_bit_count_zeros(v.words, v.bit_count); }

/**
 * @brief Returns true if all bits in bit view @p v are set to 1.
 * @param v Bit view.
 * @return true if all bits are 1, false otherwise.
 */
static inline bool   duo_bitview_all(duo_bitview_t v) { return duo_bit_all(v.words, v.bit_count); }

/**
 * @brief Returns true if at least one bit in bit view @p v is set to 1.
 * @param v Bit view.
 * @return true if any bit is 1, false otherwise.
 */
static inline bool   duo_bitview_any(duo_bitview_t v) { return duo_bit_any(v.words, v.bit_count); }

/**
 * @brief Returns true if none of the bits in bit view @p v are set to 1.
 * @param v Bit view.
 * @return true if no bits are 1, false otherwise.
 */
static inline bool   duo_bitview_none(duo_bitview_t v) { return duo_bit_none(v.words, v.bit_count); }

/**
 * @brief Finds the index of the first set bit in bit view @p v, or DUO_BIT_NPOS if none.
 * @param v Bit view.
 * @return Zero-based bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_bitview_find_first(duo_bitview_t v) { return duo_bit_find_first(v.words, v.bit_count); }

/**
 * @brief Finds the index of the next set bit strictly after @p prev_idx in bit view @p v.
 * @param v Bit view.
 * @param prev_idx Previous set bit index.
 * @return Next set bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_bitview_find_next(duo_bitview_t v, size_t prev_idx) { return duo_bit_find_next(v.words, v.bit_count, prev_idx); }

/**
 * @brief Lexicographical comparison of two bit views.
 */
static inline int duo_bitview_cmp(duo_bitview_t a, duo_bitview_t b) {
    return duo_bit_cmp(a.words, a.bit_count, b.words, b.bit_count);
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_bitview_t:
 * duo_bitview_eq, duo_bitview_ne, duo_bitview_lt, duo_bitview_le,
 * duo_bitview_gt, duo_bitview_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_bitview, duo_bitview_t, duo_bitview_cmp)

/**
 * @brief Computes 64-bit xxHash3 digest of bit view @p v.
 */
static inline uint64_t duo_bitview_hash(duo_bitview_t v) {
    return duo_bit_hash(v.words, v.bit_count, 0, 0);
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of bit view @p v.
 */
static inline uint64_t duo_bitview_hash_seed(duo_bitview_t v, uint64_t seed0, uint64_t seed1) {
    return duo_bit_hash(v.words, v.bit_count, seed0, seed1);
}

/* ----------------------------------------------------------------------------
 * duo_bitarray_t Constructors and Methods
 * ---------------------------------------------------------------------------- */

/**
 * @brief Initializes a heap-allocated bit array of @p bit_count bits.
 * If @p init_val is true, all bits are set to 1; otherwise all are cleared to 0.
 * @param a Pointer to uninitialized duo_bitarray_t.
 * @param bit_count Total number of bits to allocate.
 * @param init_val Initial value for all bits (true for 1, false for 0).
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bitarray_init(duo_bitarray_t* a, size_t bit_count, bool init_val) {
    if (!a) return false;
    a->bit_count = bit_count;
    if (bit_count == 0) {
        a->words = NULL;
        return true;
    }
    size_t num_words = (bit_count + 63) / 64;
    a->words = (uint64_t*)DUO_MALLOC(num_words * sizeof(uint64_t));
    if (!a->words) {
        a->bit_count = 0;
        return false;
    }
    if (init_val) {
        DUO_MEMSET(a->words, 0xFF, num_words * sizeof(uint64_t));
        duo_bit_sanitize_tail(a->words, bit_count);
    } else {
        DUO_MEMSET(a->words, 0, num_words * sizeof(uint64_t));
    }
    return true;
}

/**
 * @brief Deallocates the heap buffer of bit array @p a and resets its bit count to 0.
 * @param a Pointer to duo_bitarray_t to destroy.
 */
static inline void duo_bitarray_destroy(duo_bitarray_t* a) {
    if (!a) return;
    if (a->words) {
        DUO_FREE(a->words);
        a->words = NULL;
    }
    a->bit_count = 0;
}

/**
 * @brief Deep clones bit array @p src into @p dst.
 * @param dst Pointer to uninitialized destination duo_bitarray_t.
 * @param src Pointer to source duo_bitarray_t to clone.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bitarray_clone(duo_bitarray_t* dst, const duo_bitarray_t* src) {
    if (!dst || !src) return false;
    if (src->bit_count == 0) {
        dst->words = NULL;
        dst->bit_count = 0;
        return true;
    }
    size_t num_words = (src->bit_count + 63) / 64;
    dst->words = (uint64_t*)DUO_MALLOC(num_words * sizeof(uint64_t));
    if (!dst->words) {
        dst->bit_count = 0;
        return false;
    }
    dst->bit_count = src->bit_count;
    DUO_MEMCPY(dst->words, src->words, num_words * sizeof(uint64_t));
    return true;
}

/**
 * @brief Swaps the buffers and sizes of two bit arrays in O(1).
 * @param a Pointer to first duo_bitarray_t.
 * @param b Pointer to second duo_bitarray_t.
 */
static inline void duo_bitarray_swap(duo_bitarray_t* a, duo_bitarray_t* b) {
    if (!a || !b) return;
    duo_bitarray_t tmp = *a;
    *a = *b;
    *b = tmp;
}

/**
 * @brief Returns total number of bits in bit array @p a.
 * @param a Pointer to bit array.
 * @return Total bit count.
 */
static inline size_t duo_bitarray_size(const duo_bitarray_t* a) { return a ? a->bit_count : 0; }

/**
 * @brief Returns number of 64-bit words backing bit array @p a.
 * @param a Pointer to bit array.
 * @return Word count (ceil(bit_count / 64)).
 */
static inline size_t duo_bitarray_words(const duo_bitarray_t* a) { return a ? (a->bit_count + 63) / 64 : 0; }

/**
 * @brief Returns true if bit array @p a contains 0 bits.
 * @param a Pointer to bit array.
 * @return true if empty, false otherwise.
 */
static inline bool   duo_bitarray_empty(const duo_bitarray_t* a) { return !a || a->bit_count == 0; }

/**
 * @brief Gets bit at index @p idx from bit array @p a.
 * @param a Pointer to bit array.
 * @param idx Zero-based bit index.
 * @return Boolean bit value (true for 1, false for 0).
 */
static inline bool   duo_bitarray_get(const duo_bitarray_t* a, size_t idx) { return duo_bit_get(a->words, idx); }

/**
 * @brief Sets bit at index @p idx in bit array @p a to @p val.
 * @param a Pointer to bit array.
 * @param idx Zero-based bit index.
 * @param val Boolean value to set.
 */
static inline void   duo_bitarray_set(duo_bitarray_t* a, size_t idx, bool val) { duo_bit_set(a->words, idx, val); }

/**
 * @brief Inverts bit at index @p idx in bit array @p a.
 * @param a Pointer to bit array.
 * @param idx Zero-based bit index.
 */
static inline void   duo_bitarray_flip(duo_bitarray_t* a, size_t idx) { duo_bit_flip(a->words, idx); }

/**
 * @brief Semantic alias for duo_bitarray_get.
 * @param a Pointer to bit array.
 * @param idx Zero-based bit index.
 * @return Boolean bit value.
 */
static inline bool   duo_bitarray_test(const duo_bitarray_t* a, size_t idx) { return duo_bit_test(a->words, idx); }

/**
 * @brief Sets all bits in bit array @p a to 1, sanitizing tail bits.
 * @param a Pointer to bit array.
 */
static inline void   duo_bitarray_set_all(duo_bitarray_t* a) { duo_bit_set_all(a->words, a->bit_count); }

/**
 * @brief Clears all bits in bit array @p a to 0.
 * @param a Pointer to bit array.
 */
static inline void   duo_bitarray_clear_all(duo_bitarray_t* a) { duo_bit_clear_all(a->words, a->bit_count); }

/**
 * @brief Inverts all bits in bit array @p a, sanitizing tail bits.
 * @param a Pointer to bit array.
 */
static inline void   duo_bitarray_flip_all(duo_bitarray_t* a) { duo_bit_flip_all(a->words, a->bit_count); }

/**
 * @brief Counts set bits (ones) in bit array @p a using hardware POPCNT.
 * @param a Pointer to bit array.
 * @return Population count (Hamming weight).
 */
static inline size_t duo_bitarray_count_ones(const duo_bitarray_t* a) { return duo_bit_count_ones(a->words, a->bit_count); }

/**
 * @brief Counts cleared bits (zeros) in bit array @p a.
 * @param a Pointer to bit array.
 * @return Number of zero bits.
 */
static inline size_t duo_bitarray_count_zeros(const duo_bitarray_t* a) { return duo_bit_count_zeros(a->words, a->bit_count); }

/**
 * @brief Returns true if all bits in bit array @p a are set to 1.
 * @param a Pointer to bit array.
 * @return true if all bits are 1, false otherwise.
 */
static inline bool   duo_bitarray_all(const duo_bitarray_t* a) { return duo_bit_all(a->words, a->bit_count); }

/**
 * @brief Returns true if at least one bit in bit array @p a is set to 1.
 * @param a Pointer to bit array.
 * @return true if any bit is 1, false otherwise.
 */
static inline bool   duo_bitarray_any(const duo_bitarray_t* a) { return duo_bit_any(a->words, a->bit_count); }

/**
 * @brief Returns true if none of the bits in bit array @p a are set to 1.
 * @param a Pointer to bit array.
 * @return true if no bits are 1, false otherwise.
 */
static inline bool   duo_bitarray_none(const duo_bitarray_t* a) { return duo_bit_none(a->words, a->bit_count); }

/**
 * @brief Finds index of first set bit in bit array @p a, or DUO_BIT_NPOS if none.
 * @param a Pointer to bit array.
 * @return Zero-based bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_bitarray_find_first(const duo_bitarray_t* a) { return duo_bit_find_first(a->words, a->bit_count); }

/**
 * @brief Finds index of next set bit strictly after @p prev_idx in bit array @p a.
 * @param a Pointer to bit array.
 * @param prev_idx Previous set bit index.
 * @return Next set bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_bitarray_find_next(const duo_bitarray_t* a, size_t prev_idx) { return duo_bit_find_next(a->words, a->bit_count, prev_idx); }

/**
 * @brief Returns a non-owning mutable bit span viewing bit array @p a.
 * @param a Pointer to bit array.
 * @return Constructed duo_bitspan_t.
 */
static inline duo_bitspan_t duo_bitarray_as_span(duo_bitarray_t* a) {
    return duo_bitspan_make(a ? a->words : NULL, a ? a->bit_count : 0);
}

/**
 * @brief Returns a non-owning const bit view viewing bit array @p a.
 * @param a Pointer to bit array.
 * @return Constructed duo_bitview_t.
 */
static inline duo_bitview_t duo_bitarray_as_view(const duo_bitarray_t* a) {
    return duo_bitview_make(a ? a->words : NULL, a ? a->bit_count : 0);
}

/**
 * @brief In-place bitwise AND: dst &= src across common bits.
 * @param dst Destination bit array.
 * @param src Source bit array.
 */
static inline void duo_bitarray_and(duo_bitarray_t* dst, const duo_bitarray_t* src) {
    if (!dst || !src) return;
    size_t count = dst->bit_count < src->bit_count ? dst->bit_count : src->bit_count;
    duo_bit_and(dst->words, src->words, count);
}

/**
 * @brief In-place bitwise OR: dst |= src across common bits.
 * @param dst Destination bit array.
 * @param src Source bit array.
 */
static inline void duo_bitarray_or(duo_bitarray_t* dst, const duo_bitarray_t* src) {
    if (!dst || !src) return;
    size_t count = dst->bit_count < src->bit_count ? dst->bit_count : src->bit_count;
    duo_bit_or(dst->words, src->words, count);
}

/**
 * @brief In-place bitwise XOR: dst ^= src across common bits.
 * @param dst Destination bit array.
 * @param src Source bit array.
 */
static inline void duo_bitarray_xor(duo_bitarray_t* dst, const duo_bitarray_t* src) {
    if (!dst || !src) return;
    size_t count = dst->bit_count < src->bit_count ? dst->bit_count : src->bit_count;
    duo_bit_xor(dst->words, src->words, count);
}

/**
 * @brief In-place bitwise NOT: dst = ~src across common bits, sanitizing tail bits.
 * @param dst Destination bit array.
 * @param src Source bit array.
 */
static inline void duo_bitarray_not(duo_bitarray_t* dst, const duo_bitarray_t* src) {
    if (!dst || !src) return;
    size_t count = dst->bit_count < src->bit_count ? dst->bit_count : src->bit_count;
    duo_bit_not(dst->words, src->words, count);
}

/**
 * @brief Lexicographical comparison of two bit arrays.
 */
static inline int duo_bitarray_cmp(const duo_bitarray_t* a, const duo_bitarray_t* b) {
    const uint64_t* wa = a ? a->words : NULL;
    size_t sa = a ? a->bit_count : 0;
    const uint64_t* wb = b ? b->words : NULL;
    size_t sb = b ? b->bit_count : 0;
    return duo_bit_cmp(wa, sa, wb, sb);
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_bitarray_t*:
 * duo_bitarray_eq, duo_bitarray_ne, duo_bitarray_lt, duo_bitarray_le,
 * duo_bitarray_gt, duo_bitarray_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_bitarray, const duo_bitarray_t*, duo_bitarray_cmp)

/**
 * @brief Computes 64-bit xxHash3 digest of bit array @p a.
 */
static inline uint64_t duo_bitarray_hash(const duo_bitarray_t* a) {
    return a ? duo_bit_hash(a->words, a->bit_count, 0, 0) : duo_bit_hash(NULL, 0, 0, 0);
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of bit array @p a.
 */
static inline uint64_t duo_bitarray_hash_seed(const duo_bitarray_t* a, uint64_t seed0, uint64_t seed1) {
    return a ? duo_bit_hash(a->words, a->bit_count, seed0, seed1) : duo_bit_hash(NULL, 0, seed0, seed1);
}

/* ----------------------------------------------------------------------------
 * 14. STACK & FIXED-BUFFER BIT ARRAY ALIAS & MACROS
 *
 * FixedBitArray is a semantic alias of BitSpan (duo_bitspan_t) backed by stack
 * or static storage with zero heap allocations. Matches FixedArray = Span<T>.
 * ---------------------------------------------------------------------------- */

typedef duo_bitspan_t duo_fixed_bitarray_t;

/**
 * @def DUO_C_STACK_BITARRAY(Name, BitCount)
 * @brief Allocates a zero-initialized 64-bit word buffer on the stack and binds a duo_bitspan_t to it.
 * Zero heap allocations.
 * @param Name Identifier of the created duo_bitspan_t variable.
 * @param BitCount Total number of bits to allocate.
 */
#define DUO_C_STACK_BITARRAY(Name, BitCount) \
    uint64_t Name##_raw_words_[((BitCount) + 63) / 64] = { 0 }; \
    duo_bitspan_t Name = { (BitCount), Name##_raw_words_ }

/**
 * @def DUO_C_STACK_BITARRAY_INIT(Name, BitCount, InitVal)
 * @brief Allocates an initialized 64-bit word buffer on the stack and initializes all bits to @p InitVal.
 * @param Name Identifier of the created duo_bitspan_t variable.
 * @param BitCount Total number of bits to allocate.
 * @param InitVal Initial boolean bit value (true for all 1s, false for all 0s).
 */
#define DUO_C_STACK_BITARRAY_INIT(Name, BitCount, InitVal) \
    uint64_t Name##_raw_words_[((BitCount) + 63) / 64]; \
    duo_bitspan_t Name = { (BitCount), Name##_raw_words_ }; \
    if (InitVal) { duo_bit_set_all(Name##_raw_words_, (BitCount)); } else { duo_bit_clear_all(Name##_raw_words_, (BitCount)); }

/* ============================================================================
 * 15. DYNAMIC GROWABLE SBO BITVECTOR (duo_bitvec_t)
 *
 * 24-byte compact Small Buffer Optimization (SBO) dynamic bit vector.
 * Inline storage holds 128 bits (2 x uint64_t words) with zero heap allocations.
 * Seamless geometric doubling on heap promotion, and shrink_to_fit demotion.
 *
 * Binary Layout (24 bytes on 64-bit platforms):
 *   Offset 0..7:   size_t bit_count (Bit 63: 0 = SBO inline, 1 = Heap allocated)
 *   Offset 8..23:
 *     - When SBO (Bit 63 == 0): uint64_t sbo_words[2] (128 bits inline)
 *     - When Heap (Bit 63 == 1): { uint64_t* heap_words; size_t capacity; }
 * ============================================================================ */

#define DUO_BITVEC_INLINE_CAP ((size_t)128)
#define DUO_BITVEC_HEAP_FLAG  (((size_t)1) << (sizeof(size_t) * 8 - 1))

typedef struct duo_bitvec {
    size_t bit_count; /**< Offset 0: bit count with highest bit indicating Heap (1) vs SBO (0) */
    union {
        uint64_t sbo_words[2]; /**< Inline storage for up to 128 bits (16 bytes, 64-bit aligned) */
        struct {
            uint64_t* heap_words; /**< Heap buffer of 64-bit words */
            size_t    capacity;   /**< Total heap capacity in bits */
        } m_heap;
    };
} duo_bitvec_t;

DUO_STATIC_ASSERT(sizeof(duo_bitvec_t) == 24, "duo_bitvec_t must be exactly 24 bytes on 64-bit systems!");

/**
 * @brief Returns true if @p v is currently stored in inline SBO mode (<= 128 bits).
 * @param v Pointer to bit vector.
 * @return true if SBO mode, false if heap allocated.
 */
static inline bool duo_bitvec_is_sbo(const duo_bitvec_t* v) {
    return (v->bit_count & DUO_BITVEC_HEAP_FLAG) == 0;
}

/**
 * @brief Returns the current number of valid bits in @p v.
 * Branch-free single-instruction bit count query.
 * @param v Pointer to bit vector.
 * @return Number of valid bits.
 */
static inline size_t duo_bitvec_size(const duo_bitvec_t* v) {
    return v ? (v->bit_count & ~DUO_BITVEC_HEAP_FLAG) : 0;
}

/**
 * @brief Returns the total bit capacity of @p v.
 * @param v Pointer to bit vector.
 * @return 128 in SBO mode, or heap capacity in bits.
 */
static inline size_t duo_bitvec_capacity(const duo_bitvec_t* v) {
    if (!v) return 0;
    return duo_bitvec_is_sbo(v) ? DUO_BITVEC_INLINE_CAP : v->m_heap.capacity;
}

/**
 * @brief Returns the number of 64-bit words backing the valid bits of @p v.
 * @param v Pointer to bit vector.
 * @return Word count (ceil(size / 64)).
 */
static inline size_t duo_bitvec_words(const duo_bitvec_t* v) {
    size_t sz = duo_bitvec_size(v);
    return (sz + 63) / 64;
}

/**
 * @brief Returns true if @p v contains 0 valid bits.
 * @param v Pointer to bit vector.
 * @return true if empty, false otherwise.
 */
static inline bool duo_bitvec_empty(const duo_bitvec_t* v) {
    return duo_bitvec_size(v) == 0;
}

/**
 * @brief Returns a mutable pointer to the active 64-bit word buffer in @p v.
 * @param v Pointer to bit vector.
 * @return Pointer to sbo_words or heap_words.
 */
static inline uint64_t* duo_bitvec_data(duo_bitvec_t* v) {
    if (!v) return NULL;
    return duo_bitvec_is_sbo(v) ? v->sbo_words : v->m_heap.heap_words;
}

/**
 * @brief Returns an immutable pointer to the active 64-bit word buffer in @p v.
 * @param v Pointer to bit vector.
 * @return Const pointer to sbo_words or heap_words.
 */
static inline const uint64_t* duo_bitvec_data_const(const duo_bitvec_t* v) {
    if (!v) return NULL;
    return duo_bitvec_is_sbo(v) ? v->sbo_words : v->m_heap.heap_words;
}

/**
 * @brief Initializes @p v to an empty inline SBO bit vector (size = 0, cap = 128).
 * Zero heap allocations.
 * @param v Pointer to bit vector.
 */
static inline void duo_bitvec_init(duo_bitvec_t* v) {
    if (!v) return;
    v->bit_count = 0;
    v->sbo_words[0] = 0;
    v->sbo_words[1] = 0;
}

/**
 * @brief Frees any allocated heap memory in @p v and reinitializes it to empty SBO mode.
 * @param v Pointer to bit vector.
 */
static inline void duo_bitvec_destroy(duo_bitvec_t* v) {
    if (!v) return;
    if (!duo_bitvec_is_sbo(v) && v->m_heap.heap_words) {
        DUO_FREE(v->m_heap.heap_words);
    }
    duo_bitvec_init(v);
}

/**
 * @brief Reserves bit capacity for at least @p min_cap_bits bits.
 * Promotes from inline SBO to heap buffer with adaptive 3-tier geometric growth when exceeding 128 bits:
 *   - Tier 1 (< 256): 256 bits (4 words, 2.0x SBO cap).
 *   - Tier 2 (256..4095): 1.5x expansion for heap block coalescing.
 *   - Tier 3 (>= 4096): 1.25x expansion bounding memory overhead.
 * @param v Pointer to bit vector.
 * @param min_cap_bits Minimum required bit capacity.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bitvec_reserve(duo_bitvec_t* v, size_t min_cap_bits) {
    if (!v) return false;
    size_t cur_cap = duo_bitvec_capacity(v);
    if (min_cap_bits <= cur_cap) return true;
    if (min_cap_bits <= DUO_BITVEC_INLINE_CAP) return true;

    size_t next_cap = (cur_cap >= 256) ? duo_geometric_grow_cap(cur_cap) : 256;
    size_t new_cap = (next_cap > min_cap_bits) ? next_cap : min_cap_bits;

    /* Round up to full 64-bit word boundary */
    size_t alloc_words = (new_cap + 63) / 64;
    new_cap = alloc_words * 64;

    size_t alloc_bytes = alloc_words * sizeof(uint64_t);
    uint64_t* new_words = (uint64_t*)DUO_MALLOC(alloc_bytes);
    if (!new_words) return false;
    DUO_MEMSET(new_words, 0, alloc_bytes);

    size_t cur_sz = duo_bitvec_size(v);
    size_t cur_words = (cur_sz + 63) / 64;
    if (cur_words > 0) {
        const uint64_t* old_data = duo_bitvec_data_const(v);
        DUO_MEMCPY(new_words, old_data, cur_words * sizeof(uint64_t));
    }

    if (!duo_bitvec_is_sbo(v) && v->m_heap.heap_words) {
        DUO_FREE(v->m_heap.heap_words);
    }

    v->bit_count = cur_sz | DUO_BITVEC_HEAP_FLAG;
    v->m_heap.heap_words = new_words;
    v->m_heap.capacity = new_cap;
    return true;
}

/**
 * @brief Initializes @p v with pre-reserved bit capacity.
 * @param v Pointer to bit vector.
 * @param cap_bits Initial required bit capacity.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bitvec_init_with_capacity(duo_bitvec_t* v, size_t cap_bits) {
    duo_bitvec_init(v);
    if (cap_bits <= DUO_BITVEC_INLINE_CAP) return true;
    return duo_bitvec_reserve(v, cap_bits);
}

/**
 * @brief Resets bit count to 0 without releasing memory or changing SBO/heap mode.
 * @param v Pointer to bit vector.
 */
static inline void duo_bitvec_clear(duo_bitvec_t* v) {
    if (!v) return;
    if (duo_bitvec_is_sbo(v)) {
        v->bit_count = 0;
        v->sbo_words[0] = 0;
        v->sbo_words[1] = 0;
    } else {
        v->bit_count = DUO_BITVEC_HEAP_FLAG;
        if (v->m_heap.heap_words) {
            v->m_heap.heap_words[0] = 0;
        }
    }
}

/**
 * @brief Appends a single bit to the end of @p v, growing dynamically if needed.
 * @param v Pointer to bit vector.
 * @param val Boolean value of the bit to append.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bitvec_push_back(duo_bitvec_t* v, bool val) {
    if (!v) return false;
    size_t sz = duo_bitvec_size(v);
    size_t cap = duo_bitvec_capacity(v);
    if (sz >= cap) {
        if (!duo_bitvec_reserve(v, sz + 1)) {
            return false;
        }
    }
    uint64_t* data = duo_bitvec_data(v);
    duo_bit_set(data, sz, val);
    size_t new_sz = sz + 1;
    v->bit_count = duo_bitvec_is_sbo(v) ? new_sz : (new_sz | DUO_BITVEC_HEAP_FLAG);
    return true;
}

/**
 * @brief Removes the last bit from @p v, sanitizing the popped bit to 0.
 * @param v Pointer to bit vector.
 * @return true on success, false if the vector was already empty.
 */
static inline bool duo_bitvec_pop_back(duo_bitvec_t* v) {
    if (!v) return false;
    size_t sz = duo_bitvec_size(v);
    if (sz == 0) return false;
    size_t new_sz = sz - 1;
    uint64_t* data = duo_bitvec_data(v);
    duo_bit_set(data, new_sz, false);
    v->bit_count = duo_bitvec_is_sbo(v) ? new_sz : (new_sz | DUO_BITVEC_HEAP_FLAG);
    return true;
}

/**
 * @brief Demotes a heap-allocated bit vector back into inline SBO storage if size <= 128,
 * or shrinks heap buffer capacity to fit current size.
 * @param v Pointer to bit vector.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bitvec_shrink_to_fit(duo_bitvec_t* v) {
    if (!v || duo_bitvec_is_sbo(v)) return true;
    size_t sz = duo_bitvec_size(v);
    if (sz <= DUO_BITVEC_INLINE_CAP) {
        uint64_t tmp[2] = { 0, 0 };
        size_t words = (sz + 63) / 64;
        if (words > 0 && v->m_heap.heap_words) {
            DUO_MEMCPY(tmp, v->m_heap.heap_words, words * sizeof(uint64_t));
        }
        if (v->m_heap.heap_words) {
            DUO_FREE(v->m_heap.heap_words);
        }
        v->bit_count = sz; /* MSB is 0 -> SBO! */
        v->sbo_words[0] = tmp[0];
        v->sbo_words[1] = tmp[1];
        return true;
    }
    size_t needed_words = (sz + 63) / 64;
    size_t needed_cap = needed_words * 64;
    if (needed_cap < v->m_heap.capacity) {
        uint64_t* new_words = (uint64_t*)DUO_MALLOC(needed_words * sizeof(uint64_t));
        if (!new_words) return false;
        DUO_MEMCPY(new_words, v->m_heap.heap_words, needed_words * sizeof(uint64_t));
        DUO_FREE(v->m_heap.heap_words);
        v->m_heap.heap_words = new_words;
        v->m_heap.capacity = needed_cap;
    }
    return true;
}

/**
 * @brief Resizes @p v to contain @p new_size bits.
 * Newly added bits are initialized to @p init_val. Excess bits are removed with tail sanitization.
 * @param v Pointer to bit vector.
 * @param new_size New bit count.
 * @param init_val Initial value for newly appended bits.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bitvec_resize(duo_bitvec_t* v, size_t new_size, bool init_val) {
    if (!v) return false;
    size_t cur_sz = duo_bitvec_size(v);
    if (new_size == cur_sz) return true;

    if (new_size > cur_sz) {
        if (!duo_bitvec_reserve(v, new_size)) return false;
        uint64_t* data = duo_bitvec_data(v);
        for (size_t i = cur_sz; i < new_size; ++i) {
            duo_bit_set(data, i, init_val);
        }
    } else {
        uint64_t* data = duo_bitvec_data(v);
        for (size_t i = new_size; i < cur_sz; ++i) {
            duo_bit_set(data, i, false);
        }
    }

    v->bit_count = duo_bitvec_is_sbo(v) ? new_size : (new_size | DUO_BITVEC_HEAP_FLAG);
    return true;
}

/**
 * @brief Creates a deep clone of @p src into @p dst.
 * @param dst Destination bit vector.
 * @param src Source bit vector.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bitvec_clone(duo_bitvec_t* dst, const duo_bitvec_t* src) {
    if (!dst || !src) return false;
    duo_bitvec_init(dst);
    size_t sz = duo_bitvec_size(src);
    const uint64_t* src_words = duo_bitvec_data_const(src);
    size_t words = (sz + 63) / 64;
    if (sz <= DUO_BITVEC_INLINE_CAP) {
        dst->bit_count = sz;
        if (words > 0) {
            DUO_MEMCPY(dst->sbo_words, src_words, words * sizeof(uint64_t));
        }
        return true;
    }
    if (!duo_bitvec_reserve(dst, sz)) return false;
    dst->bit_count = sz | DUO_BITVEC_HEAP_FLAG;
    if (words > 0) {
        DUO_MEMCPY(dst->m_heap.heap_words, src_words, words * sizeof(uint64_t));
    }
    return true;
}

/**
 * @brief Swaps the internal storage of two bit vectors in O(1) time.
 * @param a First bit vector.
 * @param b Second bit vector.
 */
static inline void duo_bitvec_swap(duo_bitvec_t* a, duo_bitvec_t* b) {
    if (!a || !b || a == b) return;
    duo_bitvec_t tmp = *a;
    *a = *b;
    *b = tmp;
}

/**
 * @brief Gets the bit at index @p idx in @p v.
 * @param v Pointer to bit vector.
 * @param idx Bit index (0 <= idx < size).
 * @return Boolean bit value, or false if idx is out of bounds.
 */
static inline bool duo_bitvec_get(const duo_bitvec_t* v, size_t idx) {
    if (!v || idx >= duo_bitvec_size(v)) return false;
    return duo_bit_get(duo_bitvec_data_const(v), idx);
}

/**
 * @brief Semantic alias for duo_bitvec_get.
 */
static inline bool duo_bitvec_test(const duo_bitvec_t* v, size_t idx) {
    return duo_bitvec_get(v, idx);
}

/**
 * @brief Sets the bit at index @p idx in @p v to @p val.
 * @param v Pointer to bit vector.
 * @param idx Bit index (0 <= idx < size).
 * @param val New boolean bit value.
 */
static inline void duo_bitvec_set(duo_bitvec_t* v, size_t idx, bool val) {
    if (!v || idx >= duo_bitvec_size(v)) return;
    duo_bit_set(duo_bitvec_data(v), idx, val);
}

/**
 * @brief Inverts the bit at index @p idx in @p v.
 * @param v Pointer to bit vector.
 * @param idx Bit index (0 <= idx < size).
 */
static inline void duo_bitvec_flip(duo_bitvec_t* v, size_t idx) {
    if (!v || idx >= duo_bitvec_size(v)) return;
    duo_bit_flip(duo_bitvec_data(v), idx);
}

/**
 * @brief Sets all valid bits in @p v to 1, with tail bits sanitized.
 * @param v Pointer to bit vector.
 */
static inline void duo_bitvec_set_all(duo_bitvec_t* v) {
    if (!v) return;
    duo_bit_set_all(duo_bitvec_data(v), duo_bitvec_size(v));
}

/**
 * @brief Clears all valid bits in @p v to 0.
 * @param v Pointer to bit vector.
 */
static inline void duo_bitvec_clear_all(duo_bitvec_t* v) {
    if (!v) return;
    duo_bit_clear_all(duo_bitvec_data(v), duo_bitvec_size(v));
}

/**
 * @brief Inverts all valid bits in @p v, sanitizing tail bits.
 * @param v Pointer to bit vector.
 */
static inline void duo_bitvec_flip_all(duo_bitvec_t* v) {
    if (!v) return;
    duo_bit_flip_all(duo_bitvec_data(v), duo_bitvec_size(v));
}

/**
 * @brief Counts set bits (ones) in @p v using hardware POPCNT.
 * @param v Pointer to bit vector.
 * @return Total set bits count.
 */
static inline size_t duo_bitvec_count_ones(const duo_bitvec_t* v) {
    if (!v) return 0;
    return duo_bit_count_ones(duo_bitvec_data_const(v), duo_bitvec_size(v));
}

/**
 * @brief Counts cleared bits (zeros) in @p v.
 * @param v Pointer to bit vector.
 * @return Total cleared bits count.
 */
static inline size_t duo_bitvec_count_zeros(const duo_bitvec_t* v) {
    if (!v) return 0;
    return duo_bit_count_zeros(duo_bitvec_data_const(v), duo_bitvec_size(v));
}

/**
 * @brief Returns true if all valid bits in @p v are set to 1.
 * @param v Pointer to bit vector.
 * @return true if all bits are 1, false otherwise.
 */
static inline bool duo_bitvec_all(const duo_bitvec_t* v) {
    if (!v) return true;
    return duo_bit_all(duo_bitvec_data_const(v), duo_bitvec_size(v));
}

/**
 * @brief Returns true if at least one valid bit in @p v is set to 1.
 * @param v Pointer to bit vector.
 * @return true if any bit is 1, false otherwise.
 */
static inline bool duo_bitvec_any(const duo_bitvec_t* v) {
    if (!v) return false;
    return duo_bit_any(duo_bitvec_data_const(v), duo_bitvec_size(v));
}

/**
 * @brief Returns true if none of the valid bits in @p v are set to 1.
 * @param v Pointer to bit vector.
 * @return true if all bits are 0, false otherwise.
 */
static inline bool duo_bitvec_none(const duo_bitvec_t* v) {
    if (!v) return true;
    return duo_bit_none(duo_bitvec_data_const(v), duo_bitvec_size(v));
}

/**
 * @brief Finds the index of the first set bit in @p v, or DUO_BIT_NPOS if none.
 * @param v Pointer to bit vector.
 * @return Zero-based bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_bitvec_find_first(const duo_bitvec_t* v) {
    if (!v) return DUO_BIT_NPOS;
    return duo_bit_find_first(duo_bitvec_data_const(v), duo_bitvec_size(v));
}

/**
 * @brief Finds the index of the next set bit strictly after @p prev_idx in @p v.
 * @param v Pointer to bit vector.
 * @param prev_idx Previous set bit index.
 * @return Next set bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_bitvec_find_next(const duo_bitvec_t* v, size_t prev_idx) {
    if (!v) return DUO_BIT_NPOS;
    return duo_bit_find_next(duo_bitvec_data_const(v), duo_bitvec_size(v), prev_idx);
}

/**
 * @brief Borrows a non-owning mutable duo_bitspan_t view over the valid bits of @p v.
 * @param v Pointer to bit vector.
 * @return duo_bitspan_t view.
 */
static inline duo_bitspan_t duo_bitvec_as_span(duo_bitvec_t* v) {
    return duo_bitspan_make(duo_bitvec_data(v), duo_bitvec_size(v));
}

/**
 * @brief Borrows a non-owning immutable duo_bitview_t view over the valid bits of @p v.
 * @param v Pointer to bit vector.
 * @return duo_bitview_t view.
 */
static inline duo_bitview_t duo_bitvec_as_view(const duo_bitvec_t* v) {
    return duo_bitview_make(duo_bitvec_data_const(v), duo_bitvec_size(v));
}

/**
 * @brief In-place bitwise AND: dst &= src across common bits.
 * @param dst Destination bit vector.
 * @param src Source bit view.
 */
static inline void duo_bitvec_and(duo_bitvec_t* dst, duo_bitview_t src) {
    if (!dst) return;
    size_t d_sz = duo_bitvec_size(dst);
    size_t count = d_sz < src.bit_count ? d_sz : src.bit_count;
    duo_bit_and(duo_bitvec_data(dst), src.words, count);
}

/**
 * @brief In-place bitwise OR: dst |= src across common bits.
 * @param dst Destination bit vector.
 * @param src Source bit view.
 */
static inline void duo_bitvec_or(duo_bitvec_t* dst, duo_bitview_t src) {
    if (!dst) return;
    size_t d_sz = duo_bitvec_size(dst);
    size_t count = d_sz < src.bit_count ? d_sz : src.bit_count;
    duo_bit_or(duo_bitvec_data(dst), src.words, count);
}

/**
 * @brief In-place bitwise XOR: dst ^= src across common bits.
 * @param dst Destination bit vector.
 * @param src Source bit view.
 */
static inline void duo_bitvec_xor(duo_bitvec_t* dst, duo_bitview_t src) {
    if (!dst) return;
    size_t d_sz = duo_bitvec_size(dst);
    size_t count = d_sz < src.bit_count ? d_sz : src.bit_count;
    duo_bit_xor(duo_bitvec_data(dst), src.words, count);
}

/**
 * @brief In-place bitwise NOT: dst = ~src across common bits, sanitizing tail bits.
 * @param dst Destination bit vector.
 * @param src Source bit view.
 */
static inline void duo_bitvec_not(duo_bitvec_t* dst, duo_bitview_t src) {
    if (!dst) return;
    size_t d_sz = duo_bitvec_size(dst);
    size_t count = d_sz < src.bit_count ? d_sz : src.bit_count;
    duo_bit_not(duo_bitvec_data(dst), src.words, count);
}

/**
 * @brief Lexicographical comparison of two bit vectors.
 */
static inline int duo_bitvec_cmp(const duo_bitvec_t* a, const duo_bitvec_t* b) {
    const uint64_t* wa = a ? duo_bitvec_data_const(a) : NULL;
    size_t sa = a ? duo_bitvec_size(a) : 0;
    const uint64_t* wb = b ? duo_bitvec_data_const(b) : NULL;
    size_t sb = b ? duo_bitvec_size(b) : 0;
    return duo_bit_cmp(wa, sa, wb, sb);
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_bitvec_t*:
 * duo_bitvec_eq, duo_bitvec_ne, duo_bitvec_lt, duo_bitvec_le,
 * duo_bitvec_gt, duo_bitvec_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_bitvec, const duo_bitvec_t*, duo_bitvec_cmp)

/**
 * @brief Computes 64-bit xxHash3 digest of bit vector @p v.
 */
static inline uint64_t duo_bitvec_hash(const duo_bitvec_t* v) {
    return v ? duo_bit_hash(duo_bitvec_data_const(v), duo_bitvec_size(v), 0, 0) : duo_bit_hash(NULL, 0, 0, 0);
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of bit vector @p v.
 */
static inline uint64_t duo_bitvec_hash_seed(const duo_bitvec_t* v, uint64_t seed0, uint64_t seed1) {
    return v ? duo_bit_hash(duo_bitvec_data_const(v), duo_bitvec_size(v), seed0, seed1) : duo_bit_hash(NULL, 0, seed0, seed1);
}


/* ============================================================================
 * 16. STACK & FIXED-CAPACITY GROWABLE BITVECTOR (duo_fixed_bitvec_t)
 *
 * 24-byte non-allocating growable bit vector operating over user-provided
 * or stack-allocated 64-bit word storage.
 *
 * Binary Layout (24 bytes on 64-bit platforms):
 *   Offset 0..7:   size_t bit_count (Current number of valid bits)
 *   Offset 8..15:  size_t bit_capacity (Total allocated capacity in bits)
 *   Offset 16..23: uint64_t* words (Pointer to external/stack 64-bit words)
 * ============================================================================ */

typedef struct duo_fixed_bitvec {
    size_t    bit_count;    /**< Offset 0: Number of valid bits */
    size_t    bit_capacity; /**< Offset 8: Total bit capacity */
    uint64_t* words;        /**< Offset 16: Pointer to backing 64-bit words */
} duo_fixed_bitvec_t;

DUO_STATIC_ASSERT(sizeof(duo_fixed_bitvec_t) == 24, "duo_fixed_bitvec_t must be exactly 24 bytes on 64-bit systems!");

/**
 * @brief Initializes a fixed bit vector over external storage with bit_count = 0.
 * Zeroes the words buffer up to bit_capacity.
 * @param v Pointer to fixed bit vector.
 * @param words Pointer to pre-allocated uint64_t buffer.
 * @param bit_capacity Maximum capacity in bits.
 */
static inline void duo_fixed_bitvec_init(duo_fixed_bitvec_t* v, uint64_t* words, size_t bit_capacity) {
    if (!v) return;
    v->bit_count = 0;
    v->bit_capacity = bit_capacity;
    v->words = words;
    if (words && bit_capacity > 0) {
        duo_bit_clear_all(words, bit_capacity);
    }
}

/**
 * @brief Initializes a fixed bit vector with pre-existing valid bits.
 * @param v Pointer to fixed bit vector.
 * @param words Pointer to pre-allocated uint64_t buffer.
 * @param bit_count Initial number of active bits.
 * @param bit_capacity Maximum capacity in bits.
 */
static inline void duo_fixed_bitvec_init_from(duo_fixed_bitvec_t* v, uint64_t* words, size_t bit_count, size_t bit_capacity) {
    if (!v) return;
    v->bit_count = bit_count <= bit_capacity ? bit_count : bit_capacity;
    v->bit_capacity = bit_capacity;
    v->words = words;
    if (words && v->bit_count > 0) {
        duo_bit_sanitize_tail(words, v->bit_count);
    }
}

/**
 * @brief Returns the current number of valid bits in @p v. Branch-free Offset 0 read.
 * @param v Pointer to fixed bit vector.
 * @return Number of active bits.
 */
static inline size_t duo_fixed_bitvec_size(const duo_fixed_bitvec_t* v) {
    return v ? v->bit_count : 0;
}

/**
 * @brief Returns the maximum bit capacity of @p v.
 * @param v Pointer to fixed bit vector.
 * @return Maximum bit capacity.
 */
static inline size_t duo_fixed_bitvec_capacity(const duo_fixed_bitvec_t* v) {
    return v ? v->bit_capacity : 0;
}

/**
 * @brief Returns true if @p v contains 0 valid bits.
 * @param v Pointer to fixed bit vector.
 * @return true if empty, false otherwise.
 */
static inline bool duo_fixed_bitvec_empty(const duo_fixed_bitvec_t* v) {
    return !v || v->bit_count == 0;
}

/**
 * @brief Returns true if @p v has reached its maximum bit capacity.
 * @param v Pointer to fixed bit vector.
 * @return true if full, false otherwise.
 */
static inline bool duo_fixed_bitvec_full(const duo_fixed_bitvec_t* v) {
    return v && v->bit_count >= v->bit_capacity;
}

/**
 * @brief Returns the number of 64-bit words required for the active bits.
 * @param v Pointer to fixed bit vector.
 * @return Word count (ceil(bit_count / 64)).
 */
static inline size_t duo_fixed_bitvec_words(const duo_fixed_bitvec_t* v) {
    return v ? duo_bit_words_for_bits(v->bit_count) : 0;
}

/**
 * @brief Returns the number of 64-bit words required for the total capacity.
 * @param v Pointer to fixed bit vector.
 * @return Capacity word count (ceil(bit_capacity / 64)).
 */
static inline size_t duo_fixed_bitvec_capacity_words(const duo_fixed_bitvec_t* v) {
    return v ? duo_bit_words_for_bits(v->bit_capacity) : 0;
}

/**
 * @brief Returns pointer to mutable backing word buffer.
 * @param v Pointer to fixed bit vector.
 * @return Pointer to uint64_t words or NULL.
 */
static inline uint64_t* duo_fixed_bitvec_data(duo_fixed_bitvec_t* v) {
    return v ? v->words : NULL;
}

/**
 * @brief Returns pointer to const backing word buffer.
 * @param v Pointer to fixed bit vector.
 * @return Const pointer to uint64_t words or NULL.
 */
static inline const uint64_t* duo_fixed_bitvec_data_const(const duo_fixed_bitvec_t* v) {
    return v ? (const uint64_t*)v->words : NULL;
}

/**
 * @brief Returns the boolean bit value at @p idx.
 * @param v Pointer to fixed bit vector.
 * @param idx Zero-based bit index.
 * @return Boolean bit value (true for 1, false for 0 or out of bounds).
 */
static inline bool duo_fixed_bitvec_get(const duo_fixed_bitvec_t* v, size_t idx) {
    if (!v || !v->words || idx >= v->bit_count) return false;
    return duo_bit_get(v->words, idx);
}

/**
 * @brief Sets the bit value at @p idx to @p val.
 * @param v Pointer to fixed bit vector.
 * @param idx Zero-based bit index.
 * @param val Boolean value to set.
 */
static inline void duo_fixed_bitvec_set(duo_fixed_bitvec_t* v, size_t idx, bool val) {
    if (!v || !v->words || idx >= v->bit_count) return;
    duo_bit_set(v->words, idx, val);
}

/**
 * @brief Inverts the bit value at @p idx.
 * @param v Pointer to fixed bit vector.
 * @param idx Zero-based bit index.
 */
static inline void duo_fixed_bitvec_flip(duo_fixed_bitvec_t* v, size_t idx) {
    if (!v || !v->words || idx >= v->bit_count) return;
    duo_bit_flip(v->words, idx);
}

/**
 * @brief Appends a bit @p val to the end of @p v if space allows.
 * Never allocates heap memory. Returns true on success, false if full.
 * @param v Pointer to fixed bit vector.
 * @param val Boolean bit value to append.
 * @return true on success, false if capacity exceeded.
 */
static inline bool duo_fixed_bitvec_push_back(duo_fixed_bitvec_t* v, bool val) {
    if (!v || !v->words || v->bit_count >= v->bit_capacity) return false;
    size_t idx = v->bit_count;
    duo_bit_set(v->words, idx, val);
    v->bit_count++;
    duo_bit_sanitize_tail(v->words, v->bit_count);
    return true;
}

/**
 * @brief Semantic alias for duo_fixed_bitvec_push_back.
 * @param v Pointer to fixed bit vector.
 * @param val Boolean bit value to append.
 * @return true on success, false if capacity exceeded.
 */
static inline bool duo_fixed_bitvec_try_push_back(duo_fixed_bitvec_t* v, bool val) {
    return duo_fixed_bitvec_push_back(v, val);
}

/**
 * @brief Removes the last bit from @p v.
 * Returns true on success, false if empty.
 * @param v Pointer to fixed bit vector.
 * @return true on success, false if empty.
 */
static inline bool duo_fixed_bitvec_pop_back(duo_fixed_bitvec_t* v) {
    if (!v || !v->words || v->bit_count == 0) return false;
    v->bit_count--;
    duo_bit_sanitize_tail(v->words, v->bit_count);
    return true;
}

/**
 * @brief Semantic alias for duo_fixed_bitvec_pop_back.
 * @param v Pointer to fixed bit vector.
 * @return true on success, false if empty.
 */
static inline bool duo_fixed_bitvec_try_pop_back(duo_fixed_bitvec_t* v) {
    return duo_fixed_bitvec_pop_back(v);
}

/**
 * @brief Sets all active bits to 1.
 * @param v Pointer to fixed bit vector.
 */
static inline void duo_fixed_bitvec_set_all(duo_fixed_bitvec_t* v) {
    if (!v || !v->words || v->bit_count == 0) return;
    duo_bit_set_all(v->words, v->bit_count);
}

/**
 * @brief Clears all active bits to 0.
 * @param v Pointer to fixed bit vector.
 */
static inline void duo_fixed_bitvec_clear_all(duo_fixed_bitvec_t* v) {
    if (!v || !v->words || v->bit_count == 0) return;
    duo_bit_clear_all(v->words, v->bit_count);
}

/**
 * @brief Flips all active bits.
 * @param v Pointer to fixed bit vector.
 */
static inline void duo_fixed_bitvec_flip_all(duo_fixed_bitvec_t* v) {
    if (!v || !v->words || v->bit_count == 0) return;
    duo_bit_flip_all(v->words, v->bit_count);
}

/**
 * @brief Resets bit count to 0 without freeing backing storage.
 * @param v Pointer to fixed bit vector.
 */
static inline void duo_fixed_bitvec_clear(duo_fixed_bitvec_t* v) {
    if (!v) return;
    v->bit_count = 0;
    if (v->words && v->bit_capacity > 0) {
        duo_bit_sanitize_tail(v->words, 0);
    }
}

/**
 * @brief Resizes @p v to @p new_size bits.
 * If new_size > bit_capacity, rejects resize and returns false.
 * Newly added bits are filled with @p fill_value.
 * @param v Pointer to fixed bit vector.
 * @param new_size Desired new bit count.
 * @param fill_value Boolean value used to initialize newly expanded bits.
 * @return true on success, false if new_size exceeds fixed capacity.
 */
static inline bool duo_fixed_bitvec_resize(duo_fixed_bitvec_t* v, size_t new_size, bool fill_value) {
    if (!v || !v->words) return false;
    if (new_size > v->bit_capacity) return false;
    if (new_size > v->bit_count) {
        for (size_t i = v->bit_count; i < new_size; ++i) {
            duo_bit_set(v->words, i, fill_value);
        }
    }
    v->bit_count = new_size;
    duo_bit_sanitize_tail(v->words, new_size);
    return true;
}

/**
 * @brief Returns the population count (number of 1 bits) in @p v.
 * @param v Pointer to fixed bit vector.
 * @return Count of set bits.
 */
static inline size_t duo_fixed_bitvec_count_ones(const duo_fixed_bitvec_t* v) {
    if (!v || !v->words || v->bit_count == 0) return 0;
    return duo_bit_count_ones(v->words, v->bit_count);
}

/**
 * @brief Returns the number of 0 bits in @p v.
 * @param v Pointer to fixed bit vector.
 * @return Count of cleared bits.
 */
static inline size_t duo_fixed_bitvec_count_zeros(const duo_fixed_bitvec_t* v) {
    if (!v || !v->words || v->bit_count == 0) return 0;
    return duo_bit_count_zeros(v->words, v->bit_count);
}

/**
 * @brief Returns true if all active bits are 1.
 * @param v Pointer to fixed bit vector.
 * @return true if all active bits are 1, false otherwise.
 */
static inline bool duo_fixed_bitvec_all(const duo_fixed_bitvec_t* v) {
    if (!v || !v->words || v->bit_count == 0) return true;
    return duo_fixed_bitvec_count_ones(v) == v->bit_count;
}

/**
 * @brief Returns true if any active bit is 1.
 * @param v Pointer to fixed bit vector.
 * @return true if at least one active bit is 1, false otherwise.
 */
static inline bool duo_fixed_bitvec_any(const duo_fixed_bitvec_t* v) {
    if (!v || !v->words || v->bit_count == 0) return false;
    return duo_fixed_bitvec_count_ones(v) > 0;
}

/**
 * @brief Returns true if no active bits are 1.
 * @param v Pointer to fixed bit vector.
 * @return true if all active bits are 0, false otherwise.
 */
static inline bool duo_fixed_bitvec_none(const duo_fixed_bitvec_t* v) {
    return !duo_fixed_bitvec_any(v);
}

/**
 * @brief Finds the index of the first set bit (1) in @p v.
 * Returns DUO_BIT_NPOS if not found.
 * @param v Pointer to fixed bit vector.
 * @return Zero-based bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_fixed_bitvec_find_first(const duo_fixed_bitvec_t* v) {
    if (!v || !v->words || v->bit_count == 0) return DUO_BIT_NPOS;
    return duo_bit_find_first(v->words, v->bit_count);
}

/**
 * @brief Finds the index of the next set bit (1) strictly after index @p prev_idx.
 * Returns DUO_BIT_NPOS if not found.
 * @param v Pointer to fixed bit vector.
 * @param prev_idx Previous set bit index.
 * @return Next set bit index or DUO_BIT_NPOS.
 */
static inline size_t duo_fixed_bitvec_find_next(const duo_fixed_bitvec_t* v, size_t prev_idx) {
    if (!v || !v->words || v->bit_count == 0) return DUO_BIT_NPOS;
    return duo_bit_find_next(v->words, v->bit_count, prev_idx);
}

/**
 * @brief Borrows a mutable duo_bitspan_t view over the active bits.
 * @param v Pointer to fixed bit vector.
 * @return Constructed duo_bitspan_t.
 */
static inline duo_bitspan_t duo_fixed_bitvec_as_span(duo_fixed_bitvec_t* v) {
    return duo_bitspan_make(v ? v->words : NULL, v ? v->bit_count : 0);
}

/**
 * @brief Borrows an immutable duo_bitview_t view over the active bits.
 * @param v Pointer to fixed bit vector.
 * @return Constructed duo_bitview_t.
 */
static inline duo_bitview_t duo_fixed_bitvec_as_view(const duo_fixed_bitvec_t* v) {
    return duo_bitview_make(v ? (const uint64_t*)v->words : NULL, v ? v->bit_count : 0);
}

/**
 * @brief In-place bitwise AND: dst &= src across common bits.
 * @param dst Destination fixed bit vector.
 * @param src Source immutable bit view.
 */
static inline void duo_fixed_bitvec_and(duo_fixed_bitvec_t* dst, duo_bitview_t src) {
    if (!dst || !dst->words) return;
    size_t count = dst->bit_count < src.bit_count ? dst->bit_count : src.bit_count;
    duo_bit_and(dst->words, src.words, count);
}

/**
 * @brief In-place bitwise OR: dst |= src across common bits.
 * @param dst Destination fixed bit vector.
 * @param src Source immutable bit view.
 */
static inline void duo_fixed_bitvec_or(duo_fixed_bitvec_t* dst, duo_bitview_t src) {
    if (!dst || !dst->words) return;
    size_t count = dst->bit_count < src.bit_count ? dst->bit_count : src.bit_count;
    duo_bit_or(dst->words, src.words, count);
}

/**
 * @brief In-place bitwise XOR: dst ^= src across common bits.
 * @param dst Destination fixed bit vector.
 * @param src Source immutable bit view.
 */
static inline void duo_fixed_bitvec_xor(duo_fixed_bitvec_t* dst, duo_bitview_t src) {
    if (!dst || !dst->words) return;
    size_t count = dst->bit_count < src.bit_count ? dst->bit_count : src.bit_count;
    duo_bit_xor(dst->words, src.words, count);
}

/**
 * @brief In-place bitwise NOT: dst = ~src across common bits.
 * @param dst Destination fixed bit vector.
 * @param src Source immutable bit view.
 */
static inline void duo_fixed_bitvec_not(duo_fixed_bitvec_t* dst, duo_bitview_t src) {
    if (!dst || !dst->words) return;
    size_t count = dst->bit_count < src.bit_count ? dst->bit_count : src.bit_count;
    duo_bit_not(dst->words, src.words, count);
}

/**
 * @brief Lexicographical comparison of two fixed bit vectors.
 */
static inline int duo_fixed_bitvec_cmp(const duo_fixed_bitvec_t* a, const duo_fixed_bitvec_t* b) {
    const uint64_t* wa = a ? duo_fixed_bitvec_data_const(a) : NULL;
    size_t sa = a ? duo_fixed_bitvec_size(a) : 0;
    const uint64_t* wb = b ? duo_fixed_bitvec_data_const(b) : NULL;
    size_t sb = b ? duo_fixed_bitvec_size(b) : 0;
    return duo_bit_cmp(wa, sa, wb, sb);
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_fixed_bitvec_t*:
 * duo_fixed_bitvec_eq, duo_fixed_bitvec_ne, duo_fixed_bitvec_lt, duo_fixed_bitvec_le,
 * duo_fixed_bitvec_gt, duo_fixed_bitvec_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_fixed_bitvec, const duo_fixed_bitvec_t*, duo_fixed_bitvec_cmp)

/**
 * @brief Computes 64-bit xxHash3 digest of fixed bit vector @p v.
 */
static inline uint64_t duo_fixed_bitvec_hash(const duo_fixed_bitvec_t* v) {
    return v ? duo_bit_hash(duo_fixed_bitvec_data_const(v), duo_fixed_bitvec_size(v), 0, 0) : duo_bit_hash(NULL, 0, 0, 0);
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of fixed bit vector @p v.
 */
static inline uint64_t duo_fixed_bitvec_hash_seed(const duo_fixed_bitvec_t* v, uint64_t seed0, uint64_t seed1) {
    return v ? duo_bit_hash(duo_fixed_bitvec_data_const(v), duo_fixed_bitvec_size(v), seed0, seed1) : duo_bit_hash(NULL, 0, seed0, seed1);
}

/**
 * @def DUO_C_STACK_BITVEC(Name, BitCapacity)
 * @brief Allocates an uninitialized fixed-capacity buffer directly on the stack frame
 * and binds a duo_fixed_bitvec_t to it with initial bit_count = 0 and bit_capacity = BitCapacity.
 * Zero heap allocations.
 * @param Name Identifier of the created duo_fixed_bitvec_t variable.
 * @param BitCapacity Compile-time maximum bit capacity.
 */
#define DUO_C_STACK_BITVEC(Name, BitCapacity) \
    uint64_t Name##_raw_words_[((BitCapacity) + 63) / 64] = { 0 }; \
    duo_fixed_bitvec_t Name; \
    duo_fixed_bitvec_init(&Name, Name##_raw_words_, (BitCapacity))

/**
 * @def DUO_C_STACK_BITVEC_INIT(Name, BitCapacity, InitVal)
 * @brief Allocates an initialized fixed-capacity buffer on the stack frame,
 * binding a duo_fixed_bitvec_t with bit_count = BitCapacity and all bits initialized to InitVal.
 * @param Name Identifier of the created duo_fixed_bitvec_t variable.
 * @param BitCapacity Compile-time bit capacity.
 * @param InitVal Initial boolean value (true for all 1s, false for all 0s).
 */
#define DUO_C_STACK_BITVEC_INIT(Name, BitCapacity, InitVal) \
    uint64_t Name##_raw_words_[((BitCapacity) + 63) / 64]; \
    duo_fixed_bitvec_t Name; \
    duo_fixed_bitvec_init_from(&Name, Name##_raw_words_, (BitCapacity), (BitCapacity)); \
    if (InitVal) { \
        duo_bit_set_all(Name##_raw_words_, (BitCapacity)); \
    } else { \
        duo_bit_clear_all(Name##_raw_words_, (BitCapacity)); \
    }

/**
 * @brief Computes the fine-grained stack tier capacity in 64-bit words (up to 128 words / 1024B)
 * via bit manipulation with ~1.25x growth increments, or 0 if words exceeds 128.
 *
 * @details
 * Mirrors duo_stack_tier_bytes() operating directly on 64-bit words (each word = 8 bytes).
 * Uses branchless bit-twiddling:
 * 1. Early Guards: Clamps words <= 1 to 1; rejects words > 128 (returns 0 for heap fallback).
 * 2. Power-of-two floor B = 2^k via bit-smearing on (words - 1), clamped to min 4.
 * 3. Sub-octave step size S = B / 4 (B >> 2, min 1), rounding words up to multiple of S:
 *    `(words + S - 1) & ~(S - 1)`.
 *
 * Produces the exact 24 word tiers matching the 24 byte tiers divided by 8:
 *   1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128.
 */
static inline size_t duo_stack_tier_words(size_t words) {
    return duo_sub_octave_tier(words, 128, 4);
}

/**
 * @brief Function pointer signature for fixed bit vector stack dispatcher callbacks.
 */
typedef int (*duo_bitvec_visitor_fn)(duo_fixed_bitvec_t* vec, void* ctx);

/**
 * @brief Zero-heap scoped stack dispatcher for fixed bit vectors with transparent dynamic heap fallback.
 *
 * Uses fine-grained tiered stack buffers (~1.25x growth: 1..128 words, 64b..8192b) via a switch statement
 * if bit_capacity <= 8192 bits (128 uint64_t words, 1024 bytes). If requested capacity exceeds 8192,
 * transparently allocates via DUO_MALLOC and frees via DUO_FREE on scope exit.
 *
 * @param bit_capacity Requested bit capacity.
 * @param fn Callback function invoked with the initialized duo_fixed_bitvec_t and user context.
 * @param ctx User-supplied context pointer passed to callback fn.
 * @return Return value from callback fn, or -1 if dynamic allocation fails or fn is NULL.
 */
static inline int duo_with_stack_bitvec(size_t bit_capacity, duo_bitvec_visitor_fn fn, void* ctx) {
    if (!fn) {
        return -1;
    }
    size_t words = (bit_capacity + 63) / 64;
    switch (duo_stack_tier_words(words)) {
#define DUO_BITVEC_STACK_TIER_CASE_(Words) \
        case Words: { \
            uint64_t stack_buf[Words]; \
            duo_fixed_bitvec_t v; \
            duo_fixed_bitvec_init(&v, (bit_capacity > 0) ? stack_buf : NULL, bit_capacity); \
            return fn(&v, ctx); \
        }
        DUO_BITVEC_STACK_TIER_CASE_(1)
        DUO_BITVEC_STACK_TIER_CASE_(2)
        DUO_BITVEC_STACK_TIER_CASE_(3)
        DUO_BITVEC_STACK_TIER_CASE_(4)
        DUO_BITVEC_STACK_TIER_CASE_(5)
        DUO_BITVEC_STACK_TIER_CASE_(6)
        DUO_BITVEC_STACK_TIER_CASE_(7)
        DUO_BITVEC_STACK_TIER_CASE_(8)
        DUO_BITVEC_STACK_TIER_CASE_(10)
        DUO_BITVEC_STACK_TIER_CASE_(12)
        DUO_BITVEC_STACK_TIER_CASE_(14)
        DUO_BITVEC_STACK_TIER_CASE_(16)
        DUO_BITVEC_STACK_TIER_CASE_(20)
        DUO_BITVEC_STACK_TIER_CASE_(24)
        DUO_BITVEC_STACK_TIER_CASE_(28)
        DUO_BITVEC_STACK_TIER_CASE_(32)
        DUO_BITVEC_STACK_TIER_CASE_(40)
        DUO_BITVEC_STACK_TIER_CASE_(48)
        DUO_BITVEC_STACK_TIER_CASE_(56)
        DUO_BITVEC_STACK_TIER_CASE_(64)
        DUO_BITVEC_STACK_TIER_CASE_(80)
        DUO_BITVEC_STACK_TIER_CASE_(96)
        DUO_BITVEC_STACK_TIER_CASE_(112)
        DUO_BITVEC_STACK_TIER_CASE_(128)
#undef DUO_BITVEC_STACK_TIER_CASE_
        default: {
            uint64_t* heap_buf = (uint64_t*)DUO_MALLOC((words > 0 ? words : 1) * sizeof(uint64_t));
            if (!heap_buf) {
                return -1;
            }
            duo_fixed_bitvec_t v;
            duo_fixed_bitvec_init(&v, heap_buf, bit_capacity);
            int res = fn(&v, ctx);
            DUO_FREE(heap_buf);
            return res;
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* DUO_BIT_H */
