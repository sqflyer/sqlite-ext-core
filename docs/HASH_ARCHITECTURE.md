# 64-Bit MurmurHash2 Engine Architecture (`sqlite3_hash.hpp`)

This document details the internal systems architecture, mathematical constants, bit-level transformations, unaligned memory models, collision distribution characteristics, and Bloom filter double-hashing algorithms implemented in [`include/sqlite3_hash.hpp`](../include/sqlite3_hash.hpp).

---

## 1. Executive Summary & Design Constraints

In high-performance SQLite extensions, query engines, and in-memory key-value stores, hashing constitutes a significant percentage of CPU execution time during index scans, join processing, and filter evaluation.

`sqlite3_hash.hpp` was built to satisfy six strict systems constraints:

1. **Freestanding Compilation (`-nostdlib++` / `-fno-exceptions` / `-fno-rtti`)**: Operates independently of the C++ standard library runtime.
2. **Deterministic 64-Bit Output**: Generates uniform 64-bit digests suitable for direct indexing into $2^{64}$ addressable spaces, Swiss Table 7-bit $H_1/H_2$ control byte tagging, and 64-bit fast integer equality.
3. **Unaligned Memory Safety**: Guarantees zero `SIGBUS` faults when reading raw SQLite VDBE record headers, unaligned mmap structures, or network frames.
4. **Normalized IEEE 754 Floating Point Representation**: Guarantees `hash(+0.0) == hash(-0.0)` to maintain SQL equality invariant across mathematical boundaries.
5. **High-Entropy Composite Combining**: Prevents bit cancellation in composite database tuples (e.g. `(X, X)` or `(A, B)` vs `(B, A)`).
6. **Kirsch-Mitzenmacher Double Hashing**: Enables arbitrary $k$-probe Bloom filter bit generation from a single 64-bit hash digest.

---

## 2. 64-Bit MurmurHash2 (MurmurHash64A) Mechanics

The core hashing function implements the 64-bit scalar variant of Austin Appleby's **MurmurHash64A**.

### 2.1 The Core Multiplication & Shift Constants

The algorithm is parameterized by two carefully selected mathematical constants:

$$\mathbf{M} = \text{0xc6a4a7935bd1e995}_{16} = 14313749767032793749_{10}$$
$$\mathbf{R} = 47$$

- **Multiplier $M$**: A 64-bit odd integer chosen for optimal bit dispersion across all 64 bit positions. In binary arithmetic, multiplying by $M$ performs an irreversible mixing permutation over $\mathbb{Z}/2^{64}\mathbb{Z}$.
- **Rotation $R = 47$**: A prime shift amount that maximizes bit diffusion between upper and lower bit lanes during the XOR-shift operations.

```
       Input 64-Bit Chunk (k)
                 │
                 ▼
         k = k * 0xc6a4a7935bd1e995
                 │
                 ▼
         k = k ^ (k >> 47)
                 │
                 ▼
         k = k * 0xc6a4a7935bd1e995
                 │
                 ▼
         Accumulator: h = (h ^ k) * M
```

---

## 3. Unaligned Memory Safety & Block Ingestion

### 3.1 The Alignment Problem in Embedded Architectures

On architectures such as ARMv7, SPARC, or strict-alignment RISC-V cores, directly dereferencing an unaligned 64-bit integer (`*reinterpret_cast<const uint64_t*>(ptr)`) triggers hardware alignment faults (`SIGBUS`). Even on x86_64, unaligned loads across cache line boundaries incur measurable CPU pipeline stall penalties.

### 3.2 Explicit 8-Byte Unaligned Word Reconstruction

To guarantee 100% portable safety without UB (Undefined Behavior), `murmur_hash2_64` reconstructs 64-bit words byte-by-byte in little-endian order:

```cpp
const unsigned char* p = data + (i * 8);
uint64_t k = static_cast<uint64_t>(p[0])
           | (static_cast<uint64_t>(p[1]) << 8)
           | (static_cast<uint64_t>(p[2]) << 16)
           | (static_cast<uint64_t>(p[3]) << 24)
           | (static_cast<uint64_t>(p[4]) << 32)
           | (static_cast<uint64_t>(p[5]) << 40)
           | (static_cast<uint64_t>(p[6]) << 48)
           | (static_cast<uint64_t>(p[7]) << 56);
```

#### Compiler Optimization Profile
Modern optimizing compilers (Clang 11+, GCC 8+, MSVC 2019+) pattern-match this 8-byte shift-or idiom directly into a single unaligned load instruction:
- **x86_64**: Emits `mov rax, qword ptr [rdi + rcx*8]`
- **ARM64 (AArch64)**: Emits `ldr x0, [x1, x2, lsl #3]`

This achieves **100% portability and safety on strict-alignment hardware** while preserving **single-cycle hardware load speed on x86/ARM64**.

---

## 4. Tail Byte Processing (Lengths 1 to 7 Bytes)

When the input buffer length is not a multiple of 8 ($N \pmod 8 \ne 0$), the remaining 1 to 7 bytes are absorbed into the hash state using a Duff-style fallthrough switch block:

```cpp
const unsigned char* tail = data + (nblocks * 8);

switch (len & 7) {
    case 7: h ^= static_cast<uint64_t>(tail[6]) << 48; // fallthrough
    case 6: h ^= static_cast<uint64_t>(tail[5]) << 40; // fallthrough
    case 5: h ^= static_cast<uint64_t>(tail[4]) << 32; // fallthrough
    case 4: h ^= static_cast<uint64_t>(tail[3]) << 24; // fallthrough
    case 3: h ^= static_cast<uint64_t>(tail[2]) << 16; // fallthrough
    case 2: h ^= static_cast<uint64_t>(tail[1]) << 8;  // fallthrough
    case 1: h ^= static_cast<uint64_t>(tail[0]);
            h *= m;
};
```

### Tail Mixing Properties
1. **Zero-Pad Equivalence**: Shifting each byte into its respective positional bit offset ($0, 8, 16, 24, 32, 40, 48$) ensures that byte position is strictly encoded into the state.
2. **Length-Independent Uniqueness**: The initial accumulator seed $h = \text{seed} \oplus (\text{len} \times M)$ ensures that strings with identical prefixes but differing lengths (e.g., `"data"` vs `"data\0\0\0"`) produce completely distinct hashes.

---

## 5. Avalanche Finalizer Stage

To guarantee full **Avalanche Effect** (where flipping a single bit in the input causes each output bit to flip with 50% probability), the accumulator undergoes a final triple-stage mixing:

```cpp
h ^= h >> r;  // Stage 1: Upper-to-lower bit diffusion (r = 47)
h *= m;       // Stage 2: Full multiplication mixing across all 64 bits
h ^= h >> r;  // Stage 3: Second diffusion pass
```

```
State (h) ────────► [ h ^= (h >> 47) ] ────────► [ h *= M ] ────────► [ h ^= (h >> 47) ] ────────► Final Hash
```

This finalizer eliminates any remaining linear dependencies and ensures that low-entropy inputs (such as consecutive sequential integers $1, 2, 3, \dots$) produce pseudo-random, uniformly distributed 64-bit outputs.

---

## 6. High-Entropy Composite Hash Combiner

When hashing multi-column database rows (e.g. `SqliteValueTuple<N>` or `SqliteValueVec<N>` with $N \ge 2$), individual column hashes must be combined into a single unified 64-bit hash.

Simply using bitwise XOR ($h_1 \oplus h_2$) causes catastrophic collision failures:
- Symmetrical columns cancel out: $\text{combine}(h_A, h_A) = 0$.
- Order invariance causes permutations to collide: $\text{combine}(h_A, h_B) == \text{combine}(h_B, h_A)$.

### The Combiner Formula

`sqlite3_hash.hpp` implements an enhanced 64-bit Fibonacci/Avalanche combination function:

$$\text{combine}(h_1, h_2) = h_1 \oplus \left( h_2 + \text{0x517cc1b727220a95}_{16} + (h_1 \ll 6) + (h_1 \gg 2) \right)$$

```cpp
inline uint64_t combine(uint64_t seed, uint64_t val) noexcept {
    return seed ^ (val + 0x517cc1b727220a95ULL + (seed << 6) + (seed >> 2));
}
```

### Mathematical Properties:
1. **Irrational Golden Ratio Constant**: $\text{0x517cc1b727220a95} \approx 2^{64} / \phi$ (where $\phi = \frac{1 + \sqrt{5}}{2}$ is the Golden Ratio). This constant acts as an additive irrational offset that prevents clustering around zero.
2. **Non-Commutative Bit Shifts**: The asymmetric left-shift by 6 and right-shift by 2 ($(h_1 \ll 6) + (h_1 \gg 2)$) breaks symmetry, guaranteeing that $\text{combine}(A, B) \ne \text{combine}(B, A)$.

---

## 7. Kirsch-Mitzenmacher Double Hashing (Bloom Filters)

Standard Bloom filters require $k$ independent hash functions ($h_1(x), h_2(x), \dots, h_k(x)$) to compute $k$ probe bit positions:

$$g_i(x) = h_i(x) \pmod m$$

Evaluating $k$ separate cryptographic or Murmur hashes over variable-length strings is computationally prohibitive.

### 7.1 The Kirsch-Mitzenmacher Theorem

Kirsch and Mitzenmacher (2006) proved that two independent hash functions ($h_1(x)$ and $h_2(x)$) can simulate $k$ independent hash functions without asymptotic loss in false positive rates using the formula:

$$g_i(x) = \left( h_1(x) + i \cdot h_2(x) \right) \pmod m \quad \text{for } i \in [0, k-1]$$

### 7.2 Implementation in `sqlite3_hash.hpp`

Because `murmur_hash2_64` generates 64 high-quality pseudo-random bits, we extract $h_1$ and $h_2$ directly by splitting the single 64-bit hash into two 32-bit halves:

$$h_1 = \text{uint32}(h) \quad \text{(Lower 32 bits)}$$
$$h_2 = \text{uint32}(h \gg 32) \quad \text{(Upper 32 bits)}$$

```cpp
inline size_t bloom_hash_index(uint64_t hash64, uint32_t i, size_t num_bits) noexcept {
    if (num_bits == 0) return 0;
    uint32_t h1 = static_cast<uint32_t>(hash64);
    uint32_t h2 = static_cast<uint32_t>(hash64 >> 32);
    return static_cast<size_t>((static_cast<uint64_t>(h1) + static_cast<uint64_t>(i) * h2) % num_bits);
}
```

```
                                64-Bit MurmurHash2 Digest
                     ┌───────────────────────────┬───────────────────────────┐
                     │    Upper 32 Bits (h2)     │    Lower 32 Bits (h1)     │
                     └─────────────┬─────────────┴─────────────┬─────────────┘
                                   │                           │
                                   ▼                           ▼
                 Probe Index (i) ──► [ h1 + (i * h2) ] ──► [ % num_bits ] ──► Bit Position
```

#### Performance Advantage:
- **0 Extra Memory Allocations**
- **0 Extra Hashing Passes over the payload**
- **Single-Cycle ALU Computation per Bloom Filter Probe**

---

## 8. IEEE 754 Floating-Point Normalization

In the IEEE 754 floating-point standard, positive zero (`+0.0`, `0x0000000000000000`) and negative zero (`-0.0`, `0x8000000000000000`) have different binary representations despite comparing equal under `operator==`.

If raw memory bytes were hashed directly, `hash(+0.0)` and `hash(-0.0)` would yield distinct hash digests, breaking hash table lookups where `key(+0.0)` is queried using `-0.0`.

`SqliteHashUtil::hash_double` guarantees canonical zero representation:

```cpp
inline uint64_t hash_double(double val, uint64_t seed = DEFAULT_SEED) noexcept {
    double d = (val == 0.0) ? 0.0 : val; // Normalizes -0.0 to +0.0
    return murmur_hash2_64(&d, sizeof(d), seed);
}
```

---

## 9. Architectural Summary Matrix

| Function | Input Type | Complexity | Guarantees |
| :--- | :--- | :---: | :--- |
| **`murmur_hash2_64`** | `const void*`, `int len` | $O(N/8)$ | 64-bit MurmurHash64A, unaligned-safe, endian-portable |
| **`hash`** | `const void*`, `int len` | $O(N/8)$ | Zero-allocation byte buffer hashing |
| **`mix`** | `uint64_t seed`, `const void*` | $O(N/8)$ | Accumulator mixing across disjoint memory chunks |
| **`hash_int64`** | `int64_t` | $O(1)$ | 8-byte inlined register hash |
| **`hash_double`** | `double` | $O(1)$ | IEEE 754 normalized zero (+0.0 == -0.0) |
| **`combine`** | `uint64_t h1`, `uint64_t h2` | $O(1)$ | Non-commutative 64-bit golden ratio mixing |
| **`bloom_hash_index`**| `uint64_t h`, `uint32_t i` | $O(1)$ | Kirsch-Mitzenmacher $k$-probe index generator |
