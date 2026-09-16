#ifndef SQLITE3_HASH_HPP
#define SQLITE3_HASH_HPP

#include <stdint.h>
#include <stddef.h>
#include "stl/duo_alloc.h"

/**
 * @file sqlite3_hash.hpp
 * @brief Zero-dependency, freestanding 64-bit hashing implementation using xxHash3.
 *
 * Provides high-performance 64-bit xxHash3 hashing via DuoSTL, high-entropy composite
 * combining, and Kirsch-Mitzenmacher double hashing for Bloom filters. Completely freestanding
 * (-nostdlib++ compatible) and unaligned-memory safe across all CPU architectures.
 */

namespace SqliteHashUtil {

    /** 
     * @brief Canonical 64-bit seed constant for xxHash3 hashing.
     */
    static constexpr uint64_t DEFAULT_SEED = 0ULL;

    /** 
     * @brief Multiplier constant (M) for 64-bit MurmurHash2 backward compatibility.
     */
    static constexpr uint64_t MURMUR2_64_M = 0xc6a4a7935bd1e995ULL;

    /** 
     * @brief Bitwise shift/rotation constant (R) for 64-bit MurmurHash2 backward compatibility.
     */
    static constexpr int MURMUR2_64_R = 47;

    /** 
     * @brief High-entropy irrational golden ratio mixing constant (approx 2^64 / phi).
     * 
     * Used in hash combination to eliminate bit cancellation in repeated or symmetric composite columns.
     */
    static constexpr uint64_t COMBINE_MAGIC = 0x517cc1b727220a95ULL;

    /**
     * @brief 64-bit MurmurHash2 (MurmurHash64A) for arbitrary binary payloads.
     * Preserved for legacy reference.
     */
    inline uint64_t murmur_hash2_64(const void* key, int len, uint64_t seed = 0xc6a4a7935bd1e995ULL) noexcept {
        uint64_t h = seed ^ (static_cast<uint64_t>(len >= 0 ? len : 0) * MURMUR2_64_M);

        if (!key || len <= 0) {
            h ^= h >> MURMUR2_64_R;
            h *= MURMUR2_64_M;
            h ^= h >> MURMUR2_64_R;
            return h;
        }

        const unsigned char* data = static_cast<const unsigned char*>(key);
        const int nblocks = len / 8;

        for (int i = 0; i < nblocks; ++i) {
            const unsigned char* p = data + (i * 8);
            uint64_t k = static_cast<uint64_t>(p[0])
                       | (static_cast<uint64_t>(p[1]) << 8)
                       | (static_cast<uint64_t>(p[2]) << 16)
                       | (static_cast<uint64_t>(p[3]) << 24)
                       | (static_cast<uint64_t>(p[4]) << 32)
                       | (static_cast<uint64_t>(p[5]) << 40)
                       | (static_cast<uint64_t>(p[6]) << 48)
                       | (static_cast<uint64_t>(p[7]) << 56);

            k *= MURMUR2_64_M;
            k ^= k >> MURMUR2_64_R;
            k *= MURMUR2_64_M;

            h ^= k;
            h *= MURMUR2_64_M;
        }

        const unsigned char* tail = data + (nblocks * 8);

        switch (len & 7) {
            case 7: h ^= static_cast<uint64_t>(tail[6]) << 48; // fallthrough
            case 6: h ^= static_cast<uint64_t>(tail[5]) << 40; // fallthrough
            case 5: h ^= static_cast<uint64_t>(tail[4]) << 32; // fallthrough
            case 4: h ^= static_cast<uint64_t>(tail[3]) << 24; // fallthrough
            case 3: h ^= static_cast<uint64_t>(tail[2]) << 16; // fallthrough
            case 2: h ^= static_cast<uint64_t>(tail[1]) << 8;  // fallthrough
            case 1: h ^= static_cast<uint64_t>(tail[0]);
                    h *= MURMUR2_64_M;
        };

        h ^= h >> MURMUR2_64_R;
        h *= MURMUR2_64_M;
        h ^= h >> MURMUR2_64_R;

        return h;
    }

    /**
     * @brief Hashes a contiguous buffer of bytes using 64-bit xxHash3.
     * 
     * Leverages DuoSTL's high-performance, unaligned-safe xxHash3 implementation (`duo_hash_xxhash3`).
     * Handles null pointers or non-positive lengths by computing the xxHash3 digest of an empty string
     * with the given seed.
     * 
     * @param ptr Pointer to the data buffer (can be nullptr if len <= 0).
     * @param len Number of bytes to hash.
     * @param seed 64-bit initialization seed (defaults to DEFAULT_SEED = 0ULL).
     * @return 64-bit xxHash3 digest.
     */
    inline uint64_t hash(const void* ptr, int len, uint64_t seed = DEFAULT_SEED) noexcept {
        if (!ptr || len <= 0) return duo_xxh3_impl("", 0, seed);
        return duo_hash_xxhash3(ptr, static_cast<size_t>(len), seed, 0);
    }

    /**
     * @brief Mixes/hashes an additional buffer into an accumulator seed using 64-bit xxHash3.
     * 
     * If ptr is nullptr or len <= 0, returns the current accumulator seed unchanged.
     * 
     * @param seed Current accumulator state seed.
     * @param ptr Pointer to the additional buffer.
     * @param len Byte length of additional buffer.
     * @return 64-bit mixed xxHash3 digest.
     */
    inline uint64_t mix(uint64_t seed, const void* ptr, int len) noexcept {
        if (!ptr || len <= 0) return seed;
        return duo_hash_xxhash3(ptr, static_cast<size_t>(len), seed, 0);
    }

    /**
     * @brief Hashes a 64-bit signed integer with 64-bit xxHash3.
     * 
     * @param val 64-bit integer value.
     * @param seed 64-bit initialization seed (defaults to DEFAULT_SEED = 0ULL).
     * @return 64-bit xxHash3 digest.
     */
    inline uint64_t hash_int64(int64_t val, uint64_t seed = DEFAULT_SEED) noexcept {
        return duo_hash_xxhash3(&val, sizeof(val), seed, 0);
    }

    /**
     * @brief Hashes a double-precision float with normalized zero (+0.0 vs -0.0) using 64-bit xxHash3.
     * 
     * Normalizes negative zero (-0.0) to positive zero (+0.0) to preserve SQL equality invariants
     * (-0.0 == +0.0 in IEEE 754 and SQLite).
     * 
     * @param val Double-precision floating point number.
     * @param seed 64-bit initialization seed (defaults to DEFAULT_SEED = 0ULL).
     * @return 64-bit xxHash3 digest.
     */
    inline uint64_t hash_double(double val, uint64_t seed = DEFAULT_SEED) noexcept {
        double d = (val == 0.0) ? 0.0 : val;
        return duo_hash_xxhash3(&d, sizeof(d), seed, 0);
    }

    /**
     * @brief Combines two 64-bit hash values with high entropy dispersal.
     * 
     * Uses non-commutative asymmetric bit-shifts and an irrational golden ratio constant
     * (COMBINE_MAGIC) to eliminate bit-cancellation in symmetric or repeated composite columns.
     * 
     * @param seed Primary accumulator seed.
     * @param val Secondary hash value to combine.
     * @return 64-bit combined hash digest.
     */
    inline uint64_t combine(uint64_t seed, uint64_t val) noexcept {
        return seed ^ (val + COMBINE_MAGIC + (seed << 6) + (seed >> 2));
    }

    /**
     * @brief Generates the i-th Bloom filter bit index using Kirsch-Mitzenmacher double hashing.
     * 
     * Simulates k independent hash functions from a single 64-bit hash digest (such as xxHash3)
     * by splitting into lower 32-bit (h1) and upper 32-bit (h2) halves without asymptotic loss in
     * false positive rates:
     * 
     * Computes: g_i(x) = (h1 + i * h2) % num_bits
     * 
     * @param hash64 The single precomputed 64-bit hash digest.
     * @param i The probe index (0 .. k-1).
     * @param num_bits Total bit array capacity.
     * @return Bit index in range [0, num_bits - 1] (returns 0 if num_bits == 0).
     */
    inline size_t bloom_hash_index(uint64_t hash64, uint32_t i, size_t num_bits) noexcept {
        if (num_bits == 0) return 0;
        uint32_t h1 = static_cast<uint32_t>(hash64);
        uint32_t h2 = static_cast<uint32_t>(hash64 >> 32);
        return static_cast<size_t>((static_cast<uint64_t>(h1) + static_cast<uint64_t>(i) * h2) % num_bits);
    }
}

#endif // SQLITE3_HASH_HPP
