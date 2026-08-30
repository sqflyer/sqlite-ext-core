# Freestanding 64-Bit MurmurHash2 Engine (`sqlite3_hash.hpp`)

High-performance, zero-dependency, freestanding 64-bit MurmurHash2 (MurmurHash64A) implementation engineered specifically for the SQLite extension core. Provides **unaligned-memory safe hashing**, **Kirsch-Mitzenmacher double hashing for Bloom filters**, **high-entropy 64-bit combiner algorithms**, and **normalized floating-point hash stability**.

> **Architecture Reference**: For an in-depth systems analysis of the bitwise avalanche mixing constants ($M = \text{0xc6a4a7935bd1e995}$, $r = 47$), unaligned byte reconstruction, Kirsch-Mitzenmacher mathematical proofs, collision resistance characteristics, and assembly-level instruction pipelines, see [`docs/HASH_ARCHITECTURE.md`](HASH_ARCHITECTURE.md).

---

## 1. Overview & Key Capabilities

In embedded database engines and SQLite extension architecture, hashing is executed across hot inner loops: B-Tree index traversal, Swiss Table slot probing, in-memory MemKV cache rings, and Bloom filter membership tests. 

`sqlite3_hash.hpp` provides a unified, zero-overhead hashing namespace (`SqliteHashUtil`) with the following core guarantees:

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           SQLITE HASH ARCHITECTURE                              │
│                                                                                 │
│  [ Arbitrary Binary / Text / Integers / Doubles / Row Keys / Tuples ]           │
│                                │                                                │
│                                ▼                                                │
│                 SqliteHashUtil::murmur_hash2_64()                               │
│     (64-bit MurmurHash64A, Unaligned-Safe, Freestanding, 0 Heap Allocs)         │
│                                │                                                │
│        ┌───────────────────────┼───────────────────────┐                        │
│        ▼                       ▼                       ▼                        │
│  Single Hash             Hash Combining          Bloom Filter Indexing          │
│  SqliteHashUtil::hash()  SqliteHashUtil::combine() SqliteHashUtil::bloom_hash_index()
│  (Values, Keys, Strings) (Composite Tuples/Rows)  (Kirsch-Mitzenmacher Double)  │
│        │                       │                       │                        │
│        ▼                       ▼                       ▼                        │
│  [ Swiss Tables ]       [ Multi-Column Keys ]   [ In-Memory Bloom Filter ]      │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### Core Features

- **Freestanding & Header-Only (`-nostdlib++` Compatible)**: Zero standard library dependencies; requires only standard fixed-width integer headers (`<stdint.h>`, `<stddef.h>`).
- **Unaligned Memory Safe**: Safely ingests data from packed SQLite VDBE record headers, mmap buffers, or network streams without triggering unaligned memory traps on ARM or strict-alignment CPUs.
- **Normalized Floating Point Hashing**: Normalizes IEEE 754 positive and negative zero (`+0.0` and `-0.0`) so that mathematically equal floating point numbers produce identical hash digests.
- **Kirsch-Mitzenmacher Double Hashing**: Generates $k$ distinct Bloom filter bit indices from a single 64-bit hash without computing multiple independent hash functions.
- **Avalanche Combining Function**: High-entropy 64-bit combiner for composite keys and multi-column rows ($N \ge 2$).

---

## 2. API Reference

All hashing functions are contained within the header-only `SqliteHashUtil` namespace:

### Primary Hashing Functions

```cpp
namespace SqliteHashUtil {
    // Canonical 64-bit seed constant for MurmurHash2
    static constexpr uint64_t DEFAULT_SEED  = 0xc6a4a7935bd1e995ULL;
    static constexpr uint64_t MURMUR2_64_M  = 0xc6a4a7935bd1e995ULL;
    static constexpr int      MURMUR2_64_R  = 47;
    static constexpr uint64_t COMBINE_MAGIC = 0x517cc1b727220a95ULL;

    // 64-bit MurmurHash2 algorithm over arbitrary binary payloads
    inline uint64_t murmur_hash2_64(const void* key, int len, uint64_t seed = DEFAULT_SEED) noexcept;

    // Convenience alias for murmur_hash2_64
    inline uint64_t hash(const void* ptr, int len, uint64_t seed = DEFAULT_SEED) noexcept;

    // Incrementally mixes an accumulator seed with a new buffer
    inline uint64_t mix(uint64_t seed, const void* ptr, int len) noexcept;

    // Hashes a 64-bit signed integer
    inline uint64_t hash_int64(int64_t val, uint64_t seed = DEFAULT_SEED) noexcept;

    // Hashes a double-precision float with +0.0 / -0.0 normalization
    inline uint64_t hash_double(double val, uint64_t seed = DEFAULT_SEED) noexcept;

    // High-entropy 64-bit combiner for composite hash values
    inline uint64_t combine(uint64_t seed, uint64_t val) noexcept;

    // Kirsch-Mitzenmacher Bloom filter probe index generator
    inline size_t bloom_hash_index(uint64_t hash64, uint32_t i, size_t num_bits) noexcept;
}
```

---

## 3. Usage Examples

### Example 1: Hashing Strings & Binary Buffers

```cpp
#include "sqlite3_hash.hpp"
#include <stdio.h>
#include <string.h>

void hash_string_example() {
    const char* email = "user@example.com";
    int len = static_cast<int>(strlen(email));

    // Compute 64-bit hash
    uint64_t digest = SqliteHashUtil::hash(email, len);
    printf("Email hash: 0x%016llx\n", static_cast<unsigned long long>(digest));
}
```

### Example 2: Multi-Column Composite Row Hashing

```cpp
#include "sqlite3_hash.hpp"

// Computes a deterministic 64-bit hash for a composite tuple (user_id, dept_id, salary)
uint64_t hash_composite_record(int64_t user_id, int64_t dept_id, double salary) {
    uint64_t h = SqliteHashUtil::DEFAULT_SEED;

    h = SqliteHashUtil::combine(h, SqliteHashUtil::hash_int64(user_id));
    h = SqliteHashUtil::combine(h, SqliteHashUtil::hash_int64(dept_id));
    h = SqliteHashUtil::combine(h, SqliteHashUtil::hash_double(salary));

    return h;
}
```

### Example 3: Bloom Filter Probing (Kirsch-Mitzenmacher)

```cpp
#include "sqlite3_hash.hpp"
#include <vector>

class SimpleBloomFilter {
private:
    std::vector<bool> m_bits;
    size_t            m_size;
    uint32_t          m_num_probes; // k hash functions

public:
    SimpleBloomFilter(size_t bits, uint32_t probes)
        : m_bits(bits, false), m_size(bits), m_num_probes(probes) {}

    void insert(const void* data, int len) {
        uint64_t h = SqliteHashUtil::hash(data, len);
        for (uint32_t i = 0; i < m_num_probes; ++i) {
            size_t bit_idx = SqliteHashUtil::bloom_hash_index(h, i, m_size);
            m_bits[bit_idx] = true;
        }
    }

    bool might_contain(const void* data, int len) const {
        uint64_t h = SqliteHashUtil::hash(data, len);
        for (uint32_t i = 0; i < m_num_probes; ++i) {
            size_t bit_idx = SqliteHashUtil::bloom_hash_index(h, i, m_size);
            if (!m_bits[bit_idx]) return false; // Definitely not present!
        }
        return true; // Possibly present
    }
};
```

---

## 4. Integration with Core Data Structures

`sqlite3_hash.hpp` serves as the foundational hashing engine across all `sqlite-ext-core` subsystems:

1. **`SqliteValueOwned` & `SqliteValueView` (`sqlite3_value.hpp`)**:
   - Implements `.hash()` across integer, real, text, blob, and subtype fields.
   - Powers `SqliteValueHash` transparent functor for STL and Swiss Tables.
2. **`SqliteValueTuple<N>` & `SqliteValueVec<N>` (`sqlite3_value_containers.hpp`)**:
   - In-situ 16-byte fixed tuple hashing ($N=1..8$) and adaptive vector hashing ($N \ge 1$).
   - Powers transparent `SqliteRowHash` and `SqliteRowEqual`.
3. **`SqliteRowOwnedWrapper` (`sqlite3_row.hpp`)**:
   - Zero-allocation slice hashing across transient stack-allocated tuples and statement buffers.

---

## 5. Performance Guidelines

1. **Prefer `SqliteHashUtil::hash_int64` / `hash_double` for Scalars**:
   Direct scalar helpers pass fixed sizes directly to the optimizer, allowing the compiler to unroll loops and eliminate switch-cases entirely.
2. **Always Use `SqliteHashUtil::combine()` for Composite Rows**:
   Do not XOR hashes together (`h1 ^ h2` causes identical column values to cancel out to 0). Use `SqliteHashUtil::combine(h1, h2)` to ensure full avalanche entropy.
3. **Use `SqliteHashUtil::bloom_hash_index()` to Avoid Re-Hashing**:
   Instead of running $k$ distinct hash functions over strings or binary blobs, compute `SqliteHashUtil::hash()` once and derive all $k$ indices via double hashing.
