#ifndef SQLITE3_HASH_HPP
#define SQLITE3_HASH_HPP

#include <stdint.h>
#include <stddef.h>

/**
 * @file sqlite3_hash.hpp
 * @brief Zero-dependency, freestanding 64-bit MurmurHash2 implementation for SQLite extension core.
 *
 * Provides high-performance 64-bit MurmurHash2 (MurmurHash64A) hashing, high-entropy composite
 * combining, and Kirsch-Mitzenmacher double hashing for Bloom filters. Completely freestanding
 * (-nostdlib++ compatible) and unaligned-memory safe across all CPU architectures.
 */

namespace SqliteHashUtil {

    /** 
     * @brief Canonical 64-bit seed constant for MurmurHash2.
     * 
     * Initial state constant chosen for optimal pseudo-random dispersion across 64-bit hash spaces.
     */
    static constexpr uint64_t DEFAULT_SEED = 0xc6a4a7935bd1e995ULL;

    /** 
     * @brief Multiplier constant (M) for 64-bit MurmurHash2.
     * 
     * 64-bit prime multiplier (0xc6a4a7935bd1e995) creating an irreversible mixing permutation over Z/2^64Z.
     */
    static constexpr uint64_t MURMUR2_64_M = 0xc6a4a7935bd1e995ULL;

    /** 
     * @brief Bitwise shift/rotation constant (R) for 64-bit MurmurHash2.
     * 
     * Shift amount (47) that maximizes bit diffusion between upper and lower bit lanes during XOR-shifts.
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
     *
     * Processes 8 bytes per iteration using unaligned-safe byte reconstruction, absorbs trailing
     * 1..7 bytes via a fallthrough Duff-style switch, and applies a triple-stage avalanche finalizer.
     *
     * @param key Pointer to the data buffer (can be unaligned, NULL-safe).
     * @param len Number of bytes to hash (if <= 0, returns seed digest).
     * @param seed 64-bit initialization seed (defaults to DEFAULT_SEED).
     * @return 64-bit unsigned hash digest.
     */
    inline uint64_t murmur_hash2_64(const void* key, int len, uint64_t seed = DEFAULT_SEED) noexcept {
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
     * @brief Hashes a contiguous buffer of bytes using 64-bit MurmurHash2.
     * 
     * @param ptr Pointer to the data buffer.
     * @param len Number of bytes to hash.
     * @param seed 64-bit initialization seed.
     * @return 64-bit hash digest.
     */
    inline uint64_t hash(const void* ptr, int len, uint64_t seed = DEFAULT_SEED) noexcept {
        return murmur_hash2_64(ptr, len, seed);
    }

    /**
     * @brief Mixes/combines a buffer into an accumulator seed using MurmurHash2.
     * 
     * @param seed Current accumulator state seed.
     * @param ptr Pointer to the additional buffer.
     * @param len Byte length of additional buffer.
     * @return 64-bit mixed hash digest.
     */
    inline uint64_t mix(uint64_t seed, const void* ptr, int len) noexcept {
        return murmur_hash2_64(ptr, len, seed);
    }

    /**
     * @brief Hashes a 64-bit signed integer with MurmurHash2 avalanche mixing.
     * 
     * @param val 64-bit integer value.
     * @param seed 64-bit initialization seed.
     * @return 64-bit hash digest.
     */
    inline uint64_t hash_int64(int64_t val, uint64_t seed = DEFAULT_SEED) noexcept {
        return murmur_hash2_64(&val, sizeof(val), seed);
    }

    /**
     * @brief Hashes a double-precision float with normalized zero (+0.0 vs -0.0).
     * 
     * Normalizes negative zero (-0.0) to positive zero (+0.0) to preserve SQL equality invariants.
     * 
     * @param val Double-precision floating point number.
     * @param seed 64-bit initialization seed.
     * @return 64-bit hash digest.
     */
    inline uint64_t hash_double(double val, uint64_t seed = DEFAULT_SEED) noexcept {
        double d = (val == 0.0) ? 0.0 : val;
        return murmur_hash2_64(&d, sizeof(d), seed);
    }

    /**
     * @brief Combines two 64-bit hash values with high entropy dispersal.
     * 
     * Uses non-commutative asymmetric bit-shifts and an irrational golden ratio constant
     * to eliminate bit-cancellation in symmetric/repeated composite columns.
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
     * Simulates k independent hash functions from a single 64-bit MurmurHash2 digest by splitting
     * into lower 32-bit (h1) and upper 32-bit (h2) halves without asymptotic loss in false positive rates:
     * 
     * Computes: g_i(x) = (h1 + i * h2) % num_bits
     * 
     * @param hash64 The single precomputed 64-bit MurmurHash2.
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
