#ifndef DUOSTL_BIT_HPP
#define DUOSTL_BIT_HPP

/* ============================================================================
 * duo_bit.hpp - Dual-ABI Bit Manipulation Containers & Views for C++17
 *
 * Part of DuoSTL: Zero-dependency embedded container library for SQLite extensions
 * and high-performance native systems.
 *
 * Design Guarantees:
 *   - Zero C++ runtime dependencies (-nostdlib++)
 *   - Zero C++ exceptions (-fno-exceptions)
 *   - Zero C++ Run-Time Type Information (-fno-rtti)
 *   - 100% standard-layout, binary-compatible with pure C C11 ABI structs (duo_bit.h).
 *
 * Containers & Views:
 *   - BitView: Non-owning immutable bit slice (16 bytes)
 *   - BitSpan: Non-owning mutable bit slice (16 bytes)
 *   - BitArray: Owning heap bit array (16 bytes)
 *   - FixedBitArray: Stack/static bit array alias (= BitSpan)
 *   - BitVec: Dynamic growable SBO bit vector (24 bytes, 128 inline bits)
 *   - FixedBitVec: Stack/fixed-capacity growable bit vector (24 bytes)
 *   - BitIterator & BitReference: Proxy lvalue references for subscript mutation
 * ============================================================================ */

#include "duo_alloc.hpp"

extern "C" {
    #include "duo_bit.h"
}

namespace duo {

/**
 * @class BitReference
 * @brief Proxy lvalue reference object allowing assignment and reading of individual bits.
 *
 * Returned by mutable bit iterators and subscript operator on BitSpan, BitArray, and BitVec.
 */
class BitReference {
    uint64_t* m_words;
    size_t    m_idx;
public:
    /** @brief Constructs an unbound null BitReference. */
    inline BitReference() noexcept : m_words(nullptr), m_idx(0) {}

    /**
     * @brief Constructs a BitReference bound to bit @p i in word buffer @p w.
     * @param w Pointer to the base 64-bit word buffer.
     * @param i Bit index from the start of the buffer.
     */
    inline BitReference(uint64_t* w, size_t i) noexcept : m_words(w), m_idx(i) {}

    inline operator bool() const noexcept {
        return duo_bit_get(m_words, m_idx);
    }

    inline BitReference& operator=(bool val) noexcept {
        duo_bit_set(m_words, m_idx, val);
        return *this;
    }

    inline BitReference& operator=(const BitReference& o) noexcept {
        return *this = static_cast<bool>(o);
    }

    /**
     * @brief Inverts the referenced bit in-place.
     * @return Reference to *this.
     */
    inline BitReference& flip() noexcept {
        duo_bit_flip(m_words, m_idx);
        return *this;
    }

    inline bool operator~() const noexcept {
        return !static_cast<bool>(*this);
    }
};

/**
 * @class BitIterator
 * @brief Random-access iterator over bits packed into 64-bit word chunks.
 *
 * Specializes for mutable (`uint64_t*`) yielding proxy `BitReference` and const (`const uint64_t*`) yielding `bool`.
 * Fully compatible with C++ standard library algorithms (std::find, std::count, std::copy).
 * @tparam WordPtr Pointer type: uint64_t* or const uint64_t*.
 */
template <typename WordPtr>
class BitIterator {
public:
    static constexpr bool is_const_v = is_same<WordPtr, const uint64_t*>::value;
    using iterator_category = random_access_iterator_tag;
    using value_type = bool;
    using difference_type = ptrdiff_t;
    using pointer = void;
    using reference = conditional_t<is_const_v, bool, BitReference>;

private:
    WordPtr m_words;
    size_t  m_idx;

public:
    /** @brief Default constructor creating an unattached iterator at index 0. */
    inline BitIterator() noexcept : m_words(nullptr), m_idx(0) {}

    /**
     * @brief Constructs a bit iterator positioned at bit index @p i.
     * @param w Word buffer pointer.
     * @param i Bit index.
     */
    inline BitIterator(WordPtr w, size_t i) noexcept : m_words(w), m_idx(i) {}

    /** @brief Implicit conversion from mutable iterator to const iterator. */
    template <typename OtherPtr, typename = enable_if_t<is_same<OtherPtr, uint64_t*>::value && is_const_v>>
    inline BitIterator(const BitIterator<OtherPtr>& other) noexcept
        : m_words(other.words()), m_idx(other.index()) {}

    inline reference operator*() const noexcept {
        if constexpr (is_const_v) {
            return duo_bit_get(m_words, m_idx);
        } else {
            return BitReference(const_cast<uint64_t*>(m_words), m_idx);
        }
    }

    inline reference operator[](ptrdiff_t n) const noexcept {
        return *(*this + n);
    }

    inline BitIterator& operator++() noexcept {
        ++m_idx;
        return *this;
    }

    inline BitIterator operator++(int) noexcept {
        BitIterator tmp = *this;
        ++m_idx;
        return tmp;
    }

    inline BitIterator& operator--() noexcept {
        --m_idx;
        return *this;
    }

    inline BitIterator operator--(int) noexcept {
        BitIterator tmp = *this;
        --m_idx;
        return tmp;
    }

    inline BitIterator operator+(ptrdiff_t n) const noexcept {
        return BitIterator(m_words, static_cast<size_t>(static_cast<ptrdiff_t>(m_idx) + n));
    }

    inline BitIterator operator-(ptrdiff_t n) const noexcept {
        return BitIterator(m_words, static_cast<size_t>(static_cast<ptrdiff_t>(m_idx) - n));
    }

    inline BitIterator& operator+=(ptrdiff_t n) noexcept {
        m_idx = static_cast<size_t>(static_cast<ptrdiff_t>(m_idx) + n);
        return *this;
    }

    inline BitIterator& operator-=(ptrdiff_t n) noexcept {
        m_idx = static_cast<size_t>(static_cast<ptrdiff_t>(m_idx) - n);
        return *this;
    }

    inline ptrdiff_t operator-(const BitIterator& o) const noexcept {
        return static_cast<ptrdiff_t>(m_idx) - static_cast<ptrdiff_t>(o.m_idx);
    }

    // Comparison operators between BitIterator and BitIterator
    inline bool operator==(const BitIterator& o) const noexcept { return m_idx == o.m_idx; }
    inline bool operator!=(const BitIterator& o) const noexcept { return m_idx != o.m_idx; }
    inline bool operator<(const BitIterator& o) const noexcept  { return m_idx < o.m_idx; }
    inline bool operator<=(const BitIterator& o) const noexcept { return m_idx <= o.m_idx; }
    inline bool operator>(const BitIterator& o) const noexcept  { return m_idx > o.m_idx; }
    inline bool operator>=(const BitIterator& o) const noexcept { return m_idx >= o.m_idx; }

    /** @brief Returns the current bit index of this iterator. */
    inline size_t index() const noexcept { return m_idx; }
    /** @brief Returns the underlying word pointer. */
    inline WordPtr words() const noexcept { return m_words; }
};

using ConstBitIterator = BitIterator<const uint64_t*>;
using MutableBitIterator = BitIterator<uint64_t*>;

/**
 * @def DUO_CXX_BIT_ITERATOR(WordsExpr, BitCountExpr)
 * @brief Injects standard STL-compatible bit iterator suite, proxy Reference,
 * reverse iterators, and element accessors into mutable bit containers (BitSpan, BitArray, BitVec).
 */
#define DUO_CXX_BIT_ITERATOR(WordsExpr, BitCountExpr) \
    using Reference              = ::duo::BitReference; \
    using reference              = ::duo::BitReference; \
    using const_reference        = bool; \
    using Iterator               = ::duo::BitIterator<uint64_t*>; \
    using iterator               = ::duo::BitIterator<uint64_t*>; \
    using const_iterator         = ::duo::BitIterator<const uint64_t*>; \
    using reverse_iterator       = ::duo::ReverseIterator<iterator>; \
    using const_reverse_iterator = ::duo::ReverseIterator<const_iterator>; \
    inline iterator begin() noexcept               { return iterator((WordsExpr), 0); } \
    inline iterator end() noexcept                 { return iterator((WordsExpr), (BitCountExpr)); } \
    inline const_iterator begin() const noexcept   { return const_iterator((WordsExpr), 0); } \
    inline const_iterator end() const noexcept     { return const_iterator((WordsExpr), (BitCountExpr)); } \
    inline const_iterator cbegin() const noexcept  { return begin(); } \
    inline const_iterator cend() const noexcept    { return end(); } \
    inline reverse_iterator rbegin() noexcept             { return reverse_iterator(end()); } \
    inline reverse_iterator rend() noexcept               { return reverse_iterator(begin()); } \
    inline const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); } \
    inline const_reverse_iterator rend() const noexcept   { return const_reverse_iterator(begin()); } \
    inline const_reverse_iterator crbegin() const noexcept{ return const_reverse_iterator(end()); } \
    inline const_reverse_iterator crend() const noexcept  { return const_reverse_iterator(begin()); } \
    inline bool operator[](size_t idx) const noexcept { return duo_bit_get((WordsExpr), idx); } \
    inline ::duo::BitReference operator[](size_t idx) noexcept { return ::duo::BitReference((WordsExpr), idx); }

/**
 * @def DUO_CXX_BIT_VIEW_ITERATOR(WordsExpr, BitCountExpr)
 * @brief Injects immutable bit iterator suite, reverse iterators, and const subscript accessor
 * into read-only bit view containers (BitView).
 */
#define DUO_CXX_BIT_VIEW_ITERATOR(WordsExpr, BitCountExpr) \
    using Iterator               = ::duo::BitIterator<const uint64_t*>; \
    using const_reference        = bool; \
    using reference              = bool; \
    using iterator               = ::duo::BitIterator<const uint64_t*>; \
    using const_iterator         = ::duo::BitIterator<const uint64_t*>; \
    using reverse_iterator       = ::duo::ReverseIterator<const_iterator>; \
    using const_reverse_iterator = ::duo::ReverseIterator<const_iterator>; \
    inline const_iterator begin() const noexcept   { return const_iterator((WordsExpr), 0); } \
    inline const_iterator end() const noexcept     { return const_iterator((WordsExpr), (BitCountExpr)); } \
    inline const_iterator cbegin() const noexcept  { return begin(); } \
    inline const_iterator cend() const noexcept    { return end(); } \
    inline const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); } \
    inline const_reverse_iterator rend() const noexcept   { return const_reverse_iterator(begin()); } \
    inline const_reverse_iterator crbegin() const noexcept{ return const_reverse_iterator(end()); } \
    inline const_reverse_iterator crend() const noexcept  { return const_reverse_iterator(begin()); } \
    inline bool operator[](size_t idx) const noexcept { return duo_bit_get((WordsExpr), idx); }


// ----------------------------------------------------------------------------
// Generic Dual-ABI C Struct Reset & Adopt Specializations for BitVec
// ----------------------------------------------------------------------------

template <>
struct duo_c_reset_helper<duo_bitvec_t> {
    static inline void apply(duo_bitvec_t& c) noexcept {
        duo_bitvec_init(&c);
    }
};

// 10. BIT CONTAINERS: BitView, BitSpan & BitArray (Dual-ABI C++17)
// ============================================================================

/**
 * @class BitView
 * @brief Non-owning immutable view over a contiguous 64-bit word-aligned bit sequence.
 * Wraps duo_bitview_t (16 bytes on 64-bit architectures, standard-layout).
 */
class BitView {
public:
    using c_type = duo_bitview_t;
    using self_type = BitView;
    using value_type = bool;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    duo_bitview_t m_inner; /**< Standard-layout C mirror struct at Offset 0. */

    /** @brief Constructs an empty BitView referencing 0 bits. */
    inline BitView() noexcept : m_inner{0, nullptr} {}

    /**
     * @brief Constructs a BitView referencing @p bit_count bits starting at 64-bit word buffer @p words.
     * @param words Immutable pointer to 64-bit word array.
     * @param bit_count Total number of valid bits in the view.
     */
    inline BitView(const uint64_t* words, size_t bit_count) noexcept : m_inner(duo_bitview_make(words, bit_count)) {}

    /** @brief Constructs a BitView wrapping raw C struct @p v. */
    inline BitView(duo_bitview_t v) noexcept : m_inner(v) {}

    /** @brief Constructs a BitView implicitly from a mutable BitSpan @p s. */
    inline BitView(duo_bitspan_t s) noexcept : m_inner(duo_bitspan_to_view(s)) {}

    /** @brief Returns the total number of valid bits in the view. */
    inline size_t size() const noexcept { return m_inner.bit_count; }

    /** @brief Returns the number of 64-bit words backing the valid bits. */
    inline size_t words() const noexcept { return (m_inner.bit_count + 63) / 64; }

    /** @brief Returns true if the view contains 0 bits. */
    inline bool empty() const noexcept { return m_inner.bit_count == 0; }

    /** @brief Returns an immutable pointer to the underlying 64-bit word buffer. */
    inline const uint64_t* data() const noexcept { return m_inner.words; }

    /** @brief Returns the bit value at index @p idx. */
    inline bool get(size_t idx) const noexcept { return duo_bitview_get(m_inner, idx); }

    /** @brief Semantic alias for get(). */
    inline bool test(size_t idx) const noexcept { return duo_bitview_test(m_inner, idx); }

    /** @brief Counts set bits (ones) using hardware POPCNT. */
    inline size_t count_ones() const noexcept { return duo_bitview_count_ones(m_inner); }

    /** @brief Counts cleared bits (zeros). */
    inline size_t count_zeros() const noexcept { return duo_bitview_count_zeros(m_inner); }

    /** @brief Returns true if all valid bits are 1. */
    inline bool all() const noexcept { return duo_bitview_all(m_inner); }

    /** @brief Returns true if at least one valid bit is 1. */
    inline bool any() const noexcept { return duo_bitview_any(m_inner); }

    /** @brief Returns true if no valid bits are 1. */
    inline bool none() const noexcept { return duo_bitview_none(m_inner); }

    /** @brief Finds the index of the first set bit using hardware CTZ, or DUO_BIT_NPOS if none. */
    inline size_t find_first() const noexcept { return duo_bitview_find_first(m_inner); }

    /** @brief Finds the index of the next set bit strictly after @p prev_idx. */
    inline size_t find_next(size_t prev_idx) const noexcept { return duo_bitview_find_next(m_inner, prev_idx); }

    /** @brief Returns *this as an immutable BitView. */
    inline BitView as_view() const noexcept { return *this; }

    /** @brief Returns *this as an immutable BitView (span alias). */
    inline BitView as_span() const noexcept { return *this; }

    /** @brief Lexicographical comparison against another bit view. */
    inline int compare(BitView o) const noexcept { return duo_bitview_cmp(m_inner, o.m_inner); }

    /** @brief Computes 64-bit xxHash3 digest of the bit sequence with masked tail bits. */
    inline uint64_t hash(uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return duo_bitview_hash_seed(m_inner, seed0, seed1);
    }

    // Relational operators between BitView and BitView
    inline bool operator==(BitView o) const noexcept { return compare(o) == 0; }
    inline bool operator!=(BitView o) const noexcept { return compare(o) != 0; }
    inline bool operator<(BitView o) const noexcept  { return compare(o) < 0; }
    inline bool operator<=(BitView o) const noexcept { return compare(o) <= 0; }
    inline bool operator>(BitView o) const noexcept  { return compare(o) > 0; }
    inline bool operator>=(BitView o) const noexcept { return compare(o) >= 0; }

    // Dual-ABI FFI Operations & Bit Iterators
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_BIT_VIEW_ITERATOR(m_inner.words, m_inner.bit_count)
};

/**
 * @class BitSpan
 * @brief Non-owning mutable slice over a contiguous 64-bit word-aligned bit sequence.
 * Wraps duo_bitspan_t (16 bytes on 64-bit architectures, standard-layout).
 */
class BitSpan {
public:
    using c_type = duo_bitspan_t;
    using self_type = BitSpan;
    using value_type = bool;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    duo_bitspan_t m_inner; /**< Standard-layout C mirror struct at Offset 0. */

    /** @brief Constructs an empty BitSpan referencing 0 bits. */
    inline BitSpan() noexcept : m_inner{0, nullptr} {}

    /**
     * @brief Constructs a BitSpan referencing @p bit_count bits starting at 64-bit word buffer @p words.
     * @param words Mutable pointer to 64-bit word array.
     * @param bit_count Total number of valid bits in the span.
     */
    inline BitSpan(uint64_t* words, size_t bit_count) noexcept : m_inner(duo_bitspan_make(words, bit_count)) {}

    /** @brief Constructs a BitSpan wrapping raw C struct @p s. */
    inline BitSpan(duo_bitspan_t s) noexcept : m_inner(s) {}

    inline operator BitView() const noexcept { return BitView(duo_bitspan_to_view(m_inner)); }

    /** @brief Returns the total number of valid bits in the span. */
    inline size_t size() const noexcept { return m_inner.bit_count; }

    /** @brief Returns the number of 64-bit words backing the valid bits. */
    inline size_t words() const noexcept { return (m_inner.bit_count + 63) / 64; }

    /** @brief Returns true if the span contains 0 bits. */
    inline bool empty() const noexcept { return m_inner.bit_count == 0; }

    /** @brief Returns a mutable pointer to the underlying 64-bit word buffer. */
    inline uint64_t* data() noexcept { return m_inner.words; }

    /** @brief Returns an immutable pointer to the underlying 64-bit word buffer. */
    inline const uint64_t* data() const noexcept { return m_inner.words; }

    /** @brief Returns the bit value at index @p idx. */
    inline bool get(size_t idx) const noexcept { return duo_bitspan_get(m_inner, idx); }

    /** @brief Semantic alias for get(). */
    inline bool test(size_t idx) const noexcept { return duo_bitspan_test(m_inner, idx); }

    /** @brief Sets the bit at index @p idx to @p val. */
    inline void set(size_t idx, bool val = true) noexcept { duo_bitspan_set(m_inner, idx, val); }

    /** @brief Inverts the bit at index @p idx. */
    inline void flip(size_t idx) noexcept { duo_bitspan_flip(m_inner, idx); }

    /** @brief Sets all valid bits to 1, sanitizing unused tail bits. */
    inline void set_all() noexcept { duo_bitspan_set_all(m_inner); }

    /** @brief Clears all valid bits to 0. */
    inline void clear_all() noexcept { duo_bitspan_clear_all(m_inner); }

    /** @brief Inverts all valid bits, sanitizing unused tail bits. */
    inline void flip_all() noexcept { duo_bitspan_flip_all(m_inner); }

    /** @brief Counts set bits (ones) using hardware POPCNT. */
    inline size_t count_ones() const noexcept { return duo_bitspan_count_ones(m_inner); }

    /** @brief Counts cleared bits (zeros). */
    inline size_t count_zeros() const noexcept { return duo_bitspan_count_zeros(m_inner); }

    /** @brief Returns true if all valid bits are 1. */
    inline bool all() const noexcept { return duo_bitspan_all(m_inner); }

    /** @brief Returns true if at least one valid bit is 1. */
    inline bool any() const noexcept { return duo_bitspan_any(m_inner); }

    /** @brief Returns true if no valid bits are 1. */
    inline bool none() const noexcept { return duo_bitspan_none(m_inner); }

    /** @brief Finds the index of the first set bit using hardware CTZ, or DUO_BIT_NPOS if none. */
    inline size_t find_first() const noexcept { return duo_bitspan_find_first(m_inner); }

    /** @brief Finds the index of the next set bit strictly after @p prev_idx. */
    inline size_t find_next(size_t prev_idx) const noexcept { return duo_bitspan_find_next(m_inner, prev_idx); }

    /** @brief Returns *this as a mutable BitSpan. */
    inline BitSpan as_span() noexcept { return *this; }

    /** @brief Borrows an immutable BitView slice over this span. */
    inline BitView as_view() const noexcept { return BitView(duo_bitspan_to_view(m_inner)); }

    // In-place bitwise logic
    inline BitSpan& operator&=(BitView src) noexcept { duo_bitspan_and(m_inner, src.m_inner); return *this; }
    inline BitSpan& operator|=(BitView src) noexcept { duo_bitspan_or(m_inner, src.m_inner); return *this; }
    inline BitSpan& operator^=(BitView src) noexcept { duo_bitspan_xor(m_inner, src.m_inner); return *this; }

    /** @brief Lexicographical comparison against another bit view. */
    inline int compare(BitView o) const noexcept { return as_view().compare(o); }

    /** @brief Computes 64-bit xxHash3 digest of the bit sequence with masked tail bits. */
    inline uint64_t hash(uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return duo_bitspan_hash_seed(m_inner, seed0, seed1);
    }

    // Relational operators between BitSpan and BitSpan
    inline bool operator==(BitSpan o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(BitSpan o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(BitSpan o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(BitSpan o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(BitSpan o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(BitSpan o) const noexcept { return compare(o.as_view()) >= 0; }

    // Relational operators between BitSpan and BitView
    inline bool operator==(BitView o) const noexcept { return compare(o) == 0; }
    inline bool operator!=(BitView o) const noexcept { return compare(o) != 0; }
    inline bool operator<(BitView o) const noexcept  { return compare(o) < 0; }
    inline bool operator<=(BitView o) const noexcept { return compare(o) <= 0; }
    inline bool operator>(BitView o) const noexcept  { return compare(o) > 0; }
    inline bool operator>=(BitView o) const noexcept { return compare(o) >= 0; }

    // Dual-ABI FFI Operations & Bit Iterators
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_BIT_ITERATOR(m_inner.words, m_inner.bit_count)
};

// Comparison operators between BitView and BitSpan
inline bool operator==(BitView a, BitSpan b) noexcept { return b == a; }
inline bool operator!=(BitView a, BitSpan b) noexcept { return b != a; }
inline bool operator<(BitView a, BitSpan b) noexcept  { return b > a; }
inline bool operator<=(BitView a, BitSpan b) noexcept { return b >= a; }
inline bool operator>(BitView a, BitSpan b) noexcept  { return b < a; }
inline bool operator>=(BitView a, BitSpan b) noexcept { return b <= a; }

/**
 * @class BitArray
 * @brief Owning dynamic heap-allocated bit array of fixed size.
 * Wraps duo_bitarray_t (16 bytes on 64-bit architectures, standard-layout).
 */
class BitArray {
public:
    using c_type = duo_bitarray_t;
    using self_type = BitArray;
    using value_type = bool;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    duo_bitarray_t m_inner; /**< Standard-layout C mirror struct at Offset 0. */

    /** @brief Constructs an empty BitArray with 0 bits and null storage. */
    inline BitArray() noexcept { duo_bitarray_init(&m_inner, 0, false); }

    /**
     * @brief Constructs an owning BitArray of @p bits elements initialized to @p fill_value.
     * @param bits Total number of bits allocated on the heap.
     * @param fill_value Initial boolean value for all bits.
     */
    inline explicit BitArray(size_t bits, bool fill_value = false) {
        duo_bitarray_init(&m_inner, bits, fill_value);
    }

    /** @brief Destructor. Releases heap-allocated word buffer. */
    inline ~BitArray() { duo_bitarray_destroy(&m_inner); }

    BitArray(const BitArray&) = delete;
    BitArray& operator=(const BitArray&) = delete;

    /**
     * @brief Move constructor. Transfers word buffer ownership in O(1) time without copying.
     * @param o Source BitArray to move from.
     */
    inline BitArray(BitArray&& o) noexcept {
        m_inner = o.m_inner;
        o.m_inner.bit_count = 0;
        o.m_inner.words = nullptr;
    }

    inline BitArray& operator=(BitArray&& o) noexcept {
        if (this != &o) {
            duo_bitarray_destroy(&m_inner);
            m_inner = o.m_inner;
            o.m_inner.bit_count = 0;
            o.m_inner.words = nullptr;
        }
        return *this;
    }

    /**
     * @brief Creates an independent deep clone of this bit array.
     * @return New BitArray instance with duplicated heap buffer.
     */
    inline BitArray clone() const {
        BitArray out;
        duo_bitarray_clone(&out.m_inner, &m_inner);
        return out;
    }

    /**
     * @brief Swaps the internal storage of two bit arrays in O(1) time.
     * @param o Other BitArray instance.
     */
    inline void swap(BitArray& o) noexcept {
        duo_bitarray_swap(&m_inner, &o.m_inner);
    }

    /** @brief Returns the total number of bits in the array. */
    inline size_t size() const noexcept { return m_inner.bit_count; }

    /** @brief Returns the number of 64-bit words backing the bit array. */
    inline size_t words() const noexcept { return (m_inner.bit_count + 63) / 64; }

    /** @brief Returns true if the bit array contains 0 bits. */
    inline bool empty() const noexcept { return m_inner.bit_count == 0; }

    /** @brief Returns a mutable pointer to the 64-bit word buffer. */
    inline uint64_t* data() noexcept { return m_inner.words; }

    /** @brief Returns an immutable pointer to the 64-bit word buffer. */
    inline const uint64_t* data() const noexcept { return m_inner.words; }

    /** @brief Returns the bit value at index @p idx. */
    inline bool get(size_t idx) const noexcept { return duo_bitarray_get(&m_inner, idx); }

    /** @brief Semantic alias for get(). */
    inline bool test(size_t idx) const noexcept { return duo_bitarray_test(&m_inner, idx); }

    /** @brief Sets the bit at index @p idx to @p val. */
    inline void set(size_t idx, bool val = true) noexcept { duo_bitarray_set(&m_inner, idx, val); }

    /** @brief Inverts the bit at index @p idx. */
    inline void flip(size_t idx) noexcept { duo_bitarray_flip(&m_inner, idx); }

    /** @brief Sets all bits to 1, sanitizing unused tail bits. */
    inline void set_all() noexcept { duo_bitarray_set_all(&m_inner); }

    /** @brief Clears all bits to 0. */
    inline void clear_all() noexcept { duo_bitarray_clear_all(&m_inner); }

    /** @brief Inverts all bits, sanitizing unused tail bits. */
    inline void flip_all() noexcept { duo_bitarray_flip_all(&m_inner); }

    /** @brief Counts set bits (ones) using hardware POPCNT. */
    inline size_t count_ones() const noexcept { return duo_bitarray_count_ones(&m_inner); }

    /** @brief Counts cleared bits (zeros). */
    inline size_t count_zeros() const noexcept { return duo_bitarray_count_zeros(&m_inner); }

    /** @brief Returns true if all bits are 1. */
    inline bool all() const noexcept { return duo_bitarray_all(&m_inner); }

    /** @brief Returns true if at least one bit is 1. */
    inline bool any() const noexcept { return duo_bitarray_any(&m_inner); }

    /** @brief Returns true if no bits are 1. */
    inline bool none() const noexcept { return duo_bitarray_none(&m_inner); }

    /** @brief Finds the index of the first set bit using hardware CTZ, or DUO_BIT_NPOS if none. */
    inline size_t find_first() const noexcept { return duo_bitarray_find_first(&m_inner); }

    /** @brief Finds the index of the next set bit strictly after @p prev_idx. */
    inline size_t find_next(size_t prev_idx) const noexcept { return duo_bitarray_find_next(&m_inner, prev_idx); }

    /** @brief Borrows a zero-copy mutable BitSpan slice over this array. */
    inline BitSpan as_span() noexcept { return BitSpan(duo_bitarray_as_span(&m_inner)); }

    /** @brief Borrows a zero-copy immutable BitView slice over this array. */
    inline BitView as_view() const noexcept { return BitView(duo_bitarray_as_view(&m_inner)); }

    inline operator BitSpan() noexcept { return as_span(); }
    inline operator BitView() const noexcept { return as_view(); }

    // Bitwise operators
    inline BitArray& operator&=(const BitArray& o) noexcept { duo_bitarray_and(&m_inner, &o.m_inner); return *this; }
    inline BitArray& operator&=(BitView o) noexcept { duo_bitspan_and(duo_bitarray_as_span(&m_inner), o.m_inner); return *this; }
    inline BitArray& operator|=(const BitArray& o) noexcept { duo_bitarray_or(&m_inner, &o.m_inner); return *this; }
    inline BitArray& operator|=(BitView o) noexcept { duo_bitspan_or(duo_bitarray_as_span(&m_inner), o.m_inner); return *this; }
    inline BitArray& operator^=(const BitArray& o) noexcept { duo_bitarray_xor(&m_inner, &o.m_inner); return *this; }
    inline BitArray& operator^=(BitView o) noexcept { duo_bitspan_xor(duo_bitarray_as_span(&m_inner), o.m_inner); return *this; }

    inline BitArray operator~() const {
        BitArray res(size(), false);
        duo_bitarray_not(&res.m_inner, &m_inner);
        return res;
    }

    /** @brief Lexicographical comparison against a bit view. */
    inline int compare(BitView o) const noexcept { return duo_bit_cmp(data(), size(), o.data(), o.size()); }

    /** @brief Computes 64-bit xxHash3 digest of the bit sequence with masked tail bits. */
    inline uint64_t hash(uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return duo_bitarray_hash_seed(&m_inner, seed0, seed1);
    }

    // Relational operators between BitArray and BitArray
    inline bool operator==(const BitArray& o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(const BitArray& o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(const BitArray& o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(const BitArray& o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(const BitArray& o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(const BitArray& o) const noexcept { return compare(o.as_view()) >= 0; }

    // Relational operators between BitArray and BitView
    inline bool operator==(BitView o) const noexcept { return compare(o) == 0; }
    inline bool operator!=(BitView o) const noexcept { return compare(o) != 0; }
    inline bool operator<(BitView o) const noexcept  { return compare(o) < 0; }
    inline bool operator<=(BitView o) const noexcept { return compare(o) <= 0; }
    inline bool operator>(BitView o) const noexcept  { return compare(o) > 0; }
    inline bool operator>=(BitView o) const noexcept { return compare(o) >= 0; }

    // Relational operators between BitArray and BitSpan
    inline bool operator==(BitSpan o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(BitSpan o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(BitSpan o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(BitSpan o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(BitSpan o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(BitSpan o) const noexcept { return compare(o.as_view()) >= 0; }

    // Dual-ABI FFI Operations & Bit Iterators
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_BIT_ITERATOR(m_inner.words, m_inner.bit_count)
};

// Comparison operators between BitView and BitArray
inline bool operator==(BitView a, const BitArray& b) noexcept { return b == a; }
inline bool operator!=(BitView a, const BitArray& b) noexcept { return b != a; }
inline bool operator<(BitView a, const BitArray& b) noexcept  { return b > a; }
inline bool operator<=(BitView a, const BitArray& b) noexcept { return b >= a; }
inline bool operator>(BitView a, const BitArray& b) noexcept  { return b < a; }
inline bool operator>=(BitView a, const BitArray& b) noexcept { return b <= a; }

// Comparison operators between BitSpan and BitArray
inline bool operator==(BitSpan a, const BitArray& b) noexcept { return b == a; }
inline bool operator!=(BitSpan a, const BitArray& b) noexcept { return b != a; }
inline bool operator<(BitSpan a, const BitArray& b) noexcept  { return b > a; }
inline bool operator<=(BitSpan a, const BitArray& b) noexcept { return b >= a; }
inline bool operator>(BitSpan a, const BitArray& b) noexcept  { return b < a; }
inline bool operator>=(BitSpan a, const BitArray& b) noexcept { return b <= a; }

/**
 * @brief Non-member swap overload for duo::BitArray.
 * @param a First bit array.
 * @param b Second bit array.
 */
inline void swap(BitArray& a, BitArray& b) noexcept {
    a.swap(b);
}

// Deduction / factory helpers
/**
 * @brief Constructs a non-owning mutable BitSpan from a fixed-size C array of 64-bit words.
 * @tparam N Word count of the array.
 * @param arr Reference to 64-bit word array.
 * @param bit_count Total valid bits.
 * @return BitSpan instance.
 */
template <size_t N>
inline BitSpan make_bitspan(uint64_t (&arr)[N], size_t bit_count) noexcept {
    return BitSpan(arr, bit_count);
}

/**
 * @brief Constructs a non-owning immutable BitView from a fixed-size C array of 64-bit words.
 * @tparam N Word count of the array.
 * @param arr Reference to const 64-bit word array.
 * @param bit_count Total valid bits.
 * @return BitView instance.
 */
template <size_t N>
inline BitView make_bitview(const uint64_t (&arr)[N], size_t bit_count) noexcept {
    return BitView(arr, bit_count);
}

/**
 * @class BitVec
 * @brief Dynamic, growable bit vector with 24-byte Small Buffer Optimization (SBO).
 *
 * Holds up to 128 contiguous inline bits inside the 24-byte struct with zero heap allocations.
 * Seamlessly promotes to dynamic heap storage with geometric doubling when exceeding 128 bits,
 * and demotes back to inline SBO mode via shrink_to_fit().
 *
 * Guaranteed 100% standard-layout, Dual-ABI C11 compatible with duo_bitvec_t.
 */
class BitVec {
public:
    using c_type = duo_bitvec_t;
    using self_type = BitVec;
    using value_type = bool;
    using size_type = size_t;
    using difference_type = ptrdiff_t;

    duo_bitvec_t m_inner; /**< Standard-layout C mirror struct at Offset 0. */

    /**
     * @brief Constructs an empty BitVec in inline SBO mode (size = 0, capacity = 128).
     * Zero heap allocations.
     */
    inline BitVec() noexcept { duo_bitvec_init(&m_inner); }

    /**
     * @brief Constructs a BitVec containing @p bits elements initialized to @p fill_value.
     * Stored in inline SBO mode if bits <= 128, otherwise allocates a heap buffer.
     * @param bits Initial bit count.
     * @param fill_value Boolean value to initialize all bits to.
     */
    inline explicit BitVec(size_t bits, bool fill_value = false) {
        duo_bitvec_init(&m_inner);
        if (bits > 0) {
            duo_bitvec_resize(&m_inner, bits, fill_value);
        }
    }

    /**
     * @brief Factory method constructing an empty BitVec with pre-reserved bit capacity.
     * @param cap_bits Minimum initial bit capacity.
     * @return Constructed BitVec instance.
     */
    static inline BitVec with_capacity(size_t cap_bits) {
        BitVec v;
        duo_bitvec_reserve(&v.m_inner, cap_bits);
        return v;
    }

    /**
     * @brief Destructor. Releases any heap-allocated memory.
     */
    inline ~BitVec() { duo_bitvec_destroy(&m_inner); }

    BitVec(const BitVec&) = delete;
    BitVec& operator=(const BitVec&) = delete;

    /**
     * @brief Move constructor. Transfers buffer ownership in O(1) time without copying.
     * @param o Source BitVec to move from.
     */
    inline BitVec(BitVec&& o) noexcept {
        m_inner = o.m_inner;
        duo_bitvec_init(&o.m_inner);
    }

    inline BitVec& operator=(BitVec&& o) noexcept {
        if (this != &o) {
            duo_bitvec_destroy(&m_inner);
            m_inner = o.m_inner;
            duo_bitvec_init(&o.m_inner);
        }
        return *this;
    }

    /**
     * @brief Creates an independent deep clone of this bit vector.
     * @return New BitVec instance containing identical bits.
     */
    inline BitVec clone() const {
        BitVec out;
        duo_bitvec_clone(&out.m_inner, &m_inner);
        return out;
    }

    /**
     * @brief Swaps the internal storage of two bit vectors in O(1) time.
     * @param o Other BitVec instance.
     */
    inline void swap(BitVec& o) noexcept {
        duo_bitvec_swap(&m_inner, &o.m_inner);
    }

    /** @brief Returns true if currently stored in inline SBO mode (<= 128 bits). */
    inline bool is_sbo() const noexcept { return duo_bitvec_is_sbo(&m_inner); }

    /** @brief Returns the total number of valid bits in the vector. Single-instruction query. */
    inline size_t size() const noexcept { return duo_bitvec_size(&m_inner); }

    /** @brief Returns the total allocated bit capacity (128 in SBO, or heap capacity). */
    inline size_t capacity() const noexcept { return duo_bitvec_capacity(&m_inner); }

    /** @brief Returns the number of 64-bit words backing the valid bits. */
    inline size_t words() const noexcept { return duo_bitvec_words(&m_inner); }

    /** @brief Returns true if the vector contains 0 bits. */
    inline bool empty() const noexcept { return duo_bitvec_empty(&m_inner); }

    /** @brief Returns a mutable pointer to the active 64-bit word buffer. */
    inline uint64_t* data() noexcept { return duo_bitvec_data(&m_inner); }

    /** @brief Returns an immutable pointer to the active 64-bit word buffer. */
    inline const uint64_t* data() const noexcept { return duo_bitvec_data_const(&m_inner); }

    /**
     * @brief Reserves buffer capacity for at least @p min_cap bits.
     * Promotes from inline SBO to heap with adaptive 3-tier geometric growth when min_cap > 128.
     * @param min_cap Minimum required bit capacity.
     * @return true on success, false on allocation failure.
     */
    inline bool reserve(size_t min_cap) noexcept { return duo_bitvec_reserve(&m_inner, min_cap); }

    /**
     * @brief Internal geometric growth helper ensuring bit capacity for at least @p min_cap bits.
     * Uses 3-tier adaptive geometric growth via duo_geometric_grow_cap (<256: 2x, <4096: 1.5x, >=4096: 1.25x).
     * @param min_cap Minimum required bit capacity.
     * @return true on success, false on allocation failure.
     */
    inline bool grow_for(size_t min_cap) noexcept {
        if (min_cap <= capacity()) {
            return true;
        }
        size_t next_cap = (capacity() >= 256) ? duo_geometric_grow_cap(capacity()) : 256;
        size_t new_cap = (next_cap > min_cap) ? next_cap : min_cap;
        return reserve(new_cap);
    }

    /**
     * @brief Resizes the vector to @p new_size bits. Newly added bits are set to @p val.
     * @param new_size New bit count.
     * @param val Initial value for newly added bits.
     */
    inline void resize(size_t new_size, bool val = false) { duo_bitvec_resize(&m_inner, new_size, val); }

    /** @brief Clears all bits, resetting size to 0 without releasing allocated memory. */
    inline void clear() noexcept { duo_bitvec_clear(&m_inner); }

    /**
     * @brief Demotes back to inline SBO mode if size <= 128, or trims heap buffer to fit size.
     * @return true on success.
     */
    inline bool shrink_to_fit() noexcept { return duo_bitvec_shrink_to_fit(&m_inner); }

    /**
     * @brief Appends a single bit to the end of the vector, growing dynamically if needed.
     * @param val Boolean value of the bit to append.
     */
    inline void push_back(bool val) { duo_bitvec_push_back(&m_inner, val); }

    /**
     * @brief Removes the last bit from the vector, zeroing the tail.
     * @return true if a bit was removed, false if the vector was already empty.
     */
    inline bool pop_back() noexcept { return duo_bitvec_pop_back(&m_inner); }

    /**
     * @brief Returns the bit value at index @p idx.
     * @param idx Bit index (0 <= idx < size()).
     */
    inline bool get(size_t idx) const noexcept { return duo_bitvec_get(&m_inner, idx); }

    /** @brief Semantic alias for get(). */
    inline bool test(size_t idx) const noexcept { return duo_bitvec_test(&m_inner, idx); }

    /**
     * @brief Sets the bit at index @p idx to @p val.
     * @param idx Bit index (0 <= idx < size()).
     * @param val New boolean bit value.
     */
    inline void set(size_t idx, bool val = true) noexcept { duo_bitvec_set(&m_inner, idx, val); }

    /**
     * @brief Inverts the bit at index @p idx.
     * @param idx Bit index (0 <= idx < size()).
     */
    inline void flip(size_t idx) noexcept { duo_bitvec_flip(&m_inner, idx); }

    /** @brief Sets all valid bits to 1, sanitizing unused tail bits. */
    inline void set_all() noexcept { duo_bitvec_set_all(&m_inner); }

    /** @brief Clears all valid bits to 0. */
    inline void clear_all() noexcept { duo_bitvec_clear_all(&m_inner); }

    /** @brief Inverts all valid bits, sanitizing unused tail bits. */
    inline void flip_all() noexcept { duo_bitvec_flip_all(&m_inner); }

    /** @brief Counts set bits (ones) using hardware POPCNT. */
    inline size_t count_ones() const noexcept { return duo_bitvec_count_ones(&m_inner); }

    /** @brief Counts cleared bits (zeros). */
    inline size_t count_zeros() const noexcept { return duo_bitvec_count_zeros(&m_inner); }

    /** @brief Returns true if all valid bits are set to 1. */
    inline bool all() const noexcept { return duo_bitvec_all(&m_inner); }

    /** @brief Returns true if at least one valid bit is set to 1. */
    inline bool any() const noexcept { return duo_bitvec_any(&m_inner); }

    /** @brief Returns true if none of the valid bits are set to 1. */
    inline bool none() const noexcept { return duo_bitvec_none(&m_inner); }

    /** @brief Finds the index of the first set bit using hardware CTZ, or DUO_BIT_NPOS if none. */
    inline size_t find_first() const noexcept { return duo_bitvec_find_first(&m_inner); }

    /** @brief Finds the index of the next set bit strictly after @p prev_idx. */
    inline size_t find_next(size_t prev_idx) const noexcept { return duo_bitvec_find_next(&m_inner, prev_idx); }

    /** @brief Borrows a zero-copy mutable BitSpan slice over this vector. */
    inline BitSpan as_span() noexcept { return BitSpan(duo_bitvec_as_span(&m_inner)); }

    /** @brief Borrows a zero-copy immutable BitView slice over this vector. */
    inline BitView as_view() const noexcept { return BitView(duo_bitvec_as_view(&m_inner)); }

    inline operator BitSpan() noexcept { return as_span(); }
    inline operator BitView() const noexcept { return as_view(); }

    // In-place bitwise logic
    inline BitVec& operator&=(const BitVec& o) noexcept { duo_bitvec_and(&m_inner, duo_bitvec_as_view(&o.m_inner)); return *this; }
    inline BitVec& operator&=(BitView o) noexcept { duo_bitvec_and(&m_inner, o.m_inner); return *this; }
    inline BitVec& operator|=(const BitVec& o) noexcept { duo_bitvec_or(&m_inner, duo_bitvec_as_view(&o.m_inner)); return *this; }
    inline BitVec& operator|=(BitView o) noexcept { duo_bitvec_or(&m_inner, o.m_inner); return *this; }
    inline BitVec& operator^=(const BitVec& o) noexcept { duo_bitvec_xor(&m_inner, duo_bitvec_as_view(&o.m_inner)); return *this; }
    inline BitVec& operator^=(BitView o) noexcept { duo_bitvec_xor(&m_inner, o.m_inner); return *this; }

    inline BitVec operator~() const {
        BitVec res = clone();
        res.flip_all();
        return res;
    }

    /** @brief Lexicographical comparison against a bit view. */
    inline int compare(BitView o) const noexcept { return duo_bit_cmp(data(), size(), o.data(), o.size()); }

    /** @brief Computes 64-bit xxHash3 digest of the bit sequence with masked tail bits. */
    inline uint64_t hash(uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return duo_bitvec_hash_seed(&m_inner, seed0, seed1);
    }

    // Relational operators between BitVec and BitVec
    inline bool operator==(const BitVec& o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(const BitVec& o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(const BitVec& o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(const BitVec& o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(const BitVec& o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(const BitVec& o) const noexcept { return compare(o.as_view()) >= 0; }

    // Relational operators between BitVec and BitView
    inline bool operator==(BitView o) const noexcept { return compare(o) == 0; }
    inline bool operator!=(BitView o) const noexcept { return compare(o) != 0; }
    inline bool operator<(BitView o) const noexcept  { return compare(o) < 0; }
    inline bool operator<=(BitView o) const noexcept { return compare(o) <= 0; }
    inline bool operator>(BitView o) const noexcept  { return compare(o) > 0; }
    inline bool operator>=(BitView o) const noexcept { return compare(o) >= 0; }

    // Relational operators between BitVec and BitSpan
    inline bool operator==(BitSpan o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(BitSpan o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(BitSpan o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(BitSpan o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(BitSpan o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(BitSpan o) const noexcept { return compare(o.as_view()) >= 0; }

    // Relational operators between BitVec and BitArray
    inline bool operator==(const BitArray& o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(const BitArray& o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(const BitArray& o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(const BitArray& o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(const BitArray& o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(const BitArray& o) const noexcept { return compare(o.as_view()) >= 0; }

    // Dual-ABI FFI Operations & Bit Iterators
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_BIT_ITERATOR(data(), size())
};

// Comparison operators between BitView and BitVec
inline bool operator==(BitView a, const BitVec& b) noexcept { return b == a; }
inline bool operator!=(BitView a, const BitVec& b) noexcept { return b != a; }
inline bool operator<(BitView a, const BitVec& b) noexcept  { return b > a; }
inline bool operator<=(BitView a, const BitVec& b) noexcept { return b >= a; }
inline bool operator>(BitView a, const BitVec& b) noexcept  { return b < a; }
inline bool operator>=(BitView a, const BitVec& b) noexcept { return b <= a; }

// Comparison operators between BitSpan and BitVec
inline bool operator==(BitSpan a, const BitVec& b) noexcept { return b == a; }
inline bool operator!=(BitSpan a, const BitVec& b) noexcept { return b != a; }
inline bool operator<(BitSpan a, const BitVec& b) noexcept  { return b > a; }
inline bool operator<=(BitSpan a, const BitVec& b) noexcept { return b >= a; }
inline bool operator>(BitSpan a, const BitVec& b) noexcept  { return b < a; }
inline bool operator>=(BitSpan a, const BitVec& b) noexcept { return b <= a; }

// Comparison operators between BitArray and BitVec
inline bool operator==(const BitArray& a, const BitVec& b) noexcept { return b == a; }
inline bool operator!=(const BitArray& a, const BitVec& b) noexcept { return b != a; }
inline bool operator<(const BitArray& a, const BitVec& b) noexcept  { return b > a; }
inline bool operator<=(const BitArray& a, const BitVec& b) noexcept { return b >= a; }
inline bool operator>(const BitArray& a, const BitVec& b) noexcept  { return b < a; }
inline bool operator>=(const BitArray& a, const BitVec& b) noexcept { return b <= a; }

/**
 * @brief Non-member swap overload for duo::BitVec.
 * @param a First bit vector.
 * @param b Second bit vector.
 */
inline void swap(BitVec& a, BitVec& b) noexcept {
    a.swap(b);
}

// Static ABI assertions for BitView, BitSpan, BitArray, BitVec
static_assert(sizeof(BitView) == 16, "duo::BitView must be exactly 16 bytes!");
static_assert(sizeof(BitSpan) == 16, "duo::BitSpan must be exactly 16 bytes!");
static_assert(sizeof(BitArray) == 16, "duo::BitArray must be exactly 16 bytes!");
static_assert(sizeof(BitVec) == 24, "duo::BitVec must be exactly 24 bytes!");
static_assert(sizeof(BitView) == sizeof(duo_bitview_t), "BitView size must equal duo_bitview_t!");
static_assert(sizeof(BitSpan) == sizeof(duo_bitspan_t), "BitSpan size must equal duo_bitspan_t!");
static_assert(sizeof(BitArray) == sizeof(duo_bitarray_t), "BitArray size must equal duo_bitarray_t!");
static_assert(sizeof(BitVec) == sizeof(duo_bitvec_t), "BitVec size must equal duo_bitvec_t!");
static_assert(is_standard_layout<BitView>::value, "duo::BitView must be standard layout!");
static_assert(is_standard_layout<BitSpan>::value, "duo::BitSpan must be standard layout!");
static_assert(is_standard_layout<BitArray>::value, "duo::BitArray must be standard layout!");
static_assert(is_standard_layout<BitVec>::value, "duo::BitVec must be standard layout!");

// Semantic alias: FixedBitArray is BitSpan over stack/static storage (matches FixedArray = Span<T>)
using FixedBitArray = BitSpan;
static_assert(sizeof(FixedBitArray) == 16, "duo::FixedBitArray must be exactly 16 bytes!");
static_assert(is_standard_layout<FixedBitArray>::value, "duo::FixedBitArray must be standard layout!");

// ============================================================================
// 12. FIXED-CAPACITY GROWABLE BITVECTOR: duo::FixedBitVec
// ============================================================================

/**
 * @class FixedBitVec
 * @brief Fixed-capacity non-allocating growable bit vector for C++17.
 *
 * Wraps a standard-layout C mirror struct `m_inner` of type duo_fixed_bitvec_t
 * ({ size_t bit_count; size_t bit_capacity; uint64_t* words; }, exactly 24 bytes on 64-bit platforms).
 *
 * Features:
 *   - Freestanding (-nostdlib++), zero-exception, zero-RTTI.
 *   - Zero heap allocations: operates strictly within a pre-allocated stack or external word buffer.
 *   - Capacity overflow protection: push_back, try_push_back, and resize safely reject
 *     insertions and return false when bit capacity is reached.
 *   - Non-owning borrowing as BitSpan or BitView.
 *   - Full STL-style bit iterator suite via DUO_CXX_BIT_ITERATOR.
 *   - Dual-ABI FFI via DUO_CXX_FFI_OPS.
 */
class FixedBitVec {
public:
    using value_type      = bool;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using pointer         = void*;
    using const_pointer   = const void*;
    using c_type          = duo_fixed_bitvec_t;
    using self_type       = FixedBitVec;

    duo_fixed_bitvec_t m_inner; /**< Standard-layout C mirror struct at Offset 0. */

    /** @brief Constructs an empty fixed bit vector (0 bits, 0 capacity, null storage). */
    inline constexpr FixedBitVec() noexcept : m_inner{ 0, 0, nullptr } {}

    /**
     * @brief Constructs a fixed bit vector binding an external 64-bit word buffer with given bit capacity.
     * Initial bit count is 0. Zeroes the words buffer up to bit_capacity.
     * @param words Pointer to contiguous 64-bit words buffer.
     * @param bit_capacity Maximum capacity in bits.
     */
    inline FixedBitVec(uint64_t* words, size_t bit_capacity) noexcept {
        duo_fixed_bitvec_init(&m_inner, words, bit_capacity);
    }

    /**
     * @brief Constructs a fixed bit vector binding an external buffer with pre-existing bits.
     * @param words Pointer to contiguous 64-bit words buffer.
     * @param bit_count Initial number of active bits.
     * @param bit_capacity Maximum capacity in bits.
     */
    inline FixedBitVec(uint64_t* words, size_t bit_count, size_t bit_capacity) noexcept {
        duo_fixed_bitvec_init_from(&m_inner, words, bit_count, bit_capacity);
    }

    /**
     * @brief Constructs a fixed bit vector directly from its C mirror struct.
     * @param inner The standard-layout duo_fixed_bitvec_t struct.
     */
    inline explicit FixedBitVec(c_type inner) noexcept : m_inner(inner) {}

    /** @brief Destructor does NOT free underlying buffer storage (safe for stack buffers). */
    inline ~FixedBitVec() noexcept = default;

    /** @brief Move constructor transfers buffer pointer and counts, clearing source. */
    inline FixedBitVec(FixedBitVec&& other) noexcept : m_inner(other.m_inner) {
        other.m_inner = duo_fixed_bitvec_t{ 0, 0, nullptr };
    }

    /** @brief Move assignment transfers buffer pointer and counts, clearing source. */
    inline FixedBitVec& operator=(FixedBitVec&& other) noexcept {
        if (this != &other) {
            m_inner = other.m_inner;
            other.m_inner = duo_fixed_bitvec_t{ 0, 0, nullptr };
        }
        return *this;
    }

    FixedBitVec(const FixedBitVec&) = delete;
    FixedBitVec& operator=(const FixedBitVec&) = delete;

    // Buffer State & Capacity Queries
    /** @brief Returns the number of active valid bits in the vector. */
    inline size_t size() const noexcept { return m_inner.bit_count; }

    /** @brief Returns the maximum number of bits that can be held without overflowing. */
    inline size_t capacity() const noexcept { return m_inner.bit_capacity; }

    /** @brief Returns the maximum possible number of bits (equal to capacity). */
    inline size_t max_size() const noexcept { return m_inner.bit_capacity; }

    /** @brief Returns true if the vector contains 0 active bits. */
    inline bool empty() const noexcept { return m_inner.bit_count == 0; }

    /** @brief Returns true if the active bit count has reached capacity. */
    inline bool full() const noexcept { return m_inner.bit_count >= m_inner.bit_capacity; }

    /** @brief Returns the number of 64-bit words required to store active bits. */
    inline size_t words() const noexcept { return duo_fixed_bitvec_words(&m_inner); }

    /** @brief Returns the total number of 64-bit words in the backing buffer. */
    inline size_t capacity_words() const noexcept { return duo_fixed_bitvec_capacity_words(&m_inner); }

    /** @brief Returns pointer to mutable backing 64-bit word storage. */
    inline uint64_t* data() noexcept { return m_inner.words; }

    /** @brief Returns pointer to const backing 64-bit word storage. */
    inline const uint64_t* data() const noexcept { return m_inner.words; }

    /** @brief Returns pointer to const backing 64-bit word storage. */
    inline const uint64_t* data_const() const noexcept { return m_inner.words; }

    // Element Access
    /** @brief Returns proxy reference to the first bit. */
    inline BitReference front() noexcept { return BitReference(m_inner.words, 0); }

    /** @brief Returns boolean value of the first bit. */
    inline bool front() const noexcept { return duo_fixed_bitvec_get(&m_inner, 0); }

    /** @brief Returns proxy reference to the last active bit. */
    inline BitReference back() noexcept { return BitReference(m_inner.words, m_inner.bit_count - 1); }

    /** @brief Returns boolean value of the last active bit. */
    inline bool back() const noexcept { return duo_fixed_bitvec_get(&m_inner, m_inner.bit_count - 1); }

    /** @brief Tests whether bit at @p idx is set to 1. */
    inline bool test(size_t idx) const noexcept { return duo_fixed_bitvec_get(&m_inner, idx); }

    // Mutation
    /**
     * @brief Appends a bit to the end of the vector.
     * @param val Boolean value to append.
     * @return true on success, false if capacity is reached.
     */
    inline bool push_back(bool val) noexcept { return duo_fixed_bitvec_push_back(&m_inner, val); }

    /** @brief Semantic alias for push_back. */
    inline bool try_push_back(bool val) noexcept { return push_back(val); }

    /**
     * @brief Removes the last bit from the vector.
     * @return true on success, false if the vector was empty.
     */
    inline bool pop_back() noexcept { return duo_fixed_bitvec_pop_back(&m_inner); }

    /** @brief Semantic alias for pop_back. */
    inline bool try_pop_back() noexcept { return pop_back(); }

    /** @brief Sets bit at @p idx to @p val. */
    inline void set(size_t idx, bool val = true) noexcept { duo_fixed_bitvec_set(&m_inner, idx, val); }

    /** @brief Resets bit at @p idx to false (0). */
    inline void reset(size_t idx) noexcept { duo_fixed_bitvec_set(&m_inner, idx, false); }

    /** @brief Inverts bit at @p idx. */
    inline void flip(size_t idx) noexcept { duo_fixed_bitvec_flip(&m_inner, idx); }

    /** @brief Sets all active bits to true (1). */
    inline void set_all() noexcept { duo_fixed_bitvec_set_all(&m_inner); }

    /** @brief Clears all active bits to false (0). */
    inline void clear_all() noexcept { duo_fixed_bitvec_clear_all(&m_inner); }

    /** @brief Flips all active bits. */
    inline void flip_all() noexcept { duo_fixed_bitvec_flip_all(&m_inner); }

    /** @brief Resets size to 0 without freeing backing storage. */
    inline void clear() noexcept { duo_fixed_bitvec_clear(&m_inner); }

    /**
     * @brief Resizes the vector to @p new_size bits.
     * @param new_size Target bit count.
     * @param fill_value Value for newly added bits if new_size > size().
     * @return true on success, false if new_size > capacity().
     */
    inline bool resize(size_t new_size, bool fill_value = false) noexcept {
        return duo_fixed_bitvec_resize(&m_inner, new_size, fill_value);
    }

    // Non-owning View & Span Borrowing
    /** @brief Borrows a mutable non-owning BitSpan over the active bits. */
    inline BitSpan as_span() noexcept { return BitSpan(duo_fixed_bitvec_as_span(&m_inner)); }

    /** @brief Borrows an immutable non-owning BitView over the active bits. */
    inline BitView as_view() const noexcept { return BitView(duo_fixed_bitvec_as_view(&m_inner)); }

    /** @brief Implicit conversion to mutable BitSpan. */
    inline operator BitSpan() noexcept { return as_span(); }

    /** @brief Implicit conversion to immutable BitView. */
    inline operator BitView() const noexcept { return as_view(); }

    // Queries & Analysis
    /** @brief Counts set bits (population count) using hardware POPCNT. */
    inline size_t count_ones() const noexcept { return duo_fixed_bitvec_count_ones(&m_inner); }

    /** @brief Counts cleared bits (zeros) in the active bit range. */
    inline size_t count_zeros() const noexcept { return duo_fixed_bitvec_count_zeros(&m_inner); }

    /** @brief Returns true if all active bits are 1. */
    inline bool all() const noexcept { return duo_fixed_bitvec_all(&m_inner); }

    /** @brief Returns true if any active bit is 1. */
    inline bool any() const noexcept { return duo_fixed_bitvec_any(&m_inner); }

    /** @brief Returns true if no active bits are 1. */
    inline bool none() const noexcept { return duo_fixed_bitvec_none(&m_inner); }

    /** @brief Finds index of the first set bit using hardware CTZ, or DUO_BIT_NPOS if none. */
    inline size_t find_first() const noexcept { return duo_fixed_bitvec_find_first(&m_inner); }

    /** @brief Finds index of the next set bit strictly after @p prev_idx, or DUO_BIT_NPOS. */
    inline size_t find_next(size_t prev_idx) const noexcept { return duo_fixed_bitvec_find_next(&m_inner, prev_idx); }

    // In-place Bitwise Logic
    inline FixedBitVec& operator&=(BitView src) noexcept { duo_fixed_bitvec_and(&m_inner, src.m_inner); return *this; }
    inline FixedBitVec& operator|=(BitView src) noexcept { duo_fixed_bitvec_or(&m_inner, src.m_inner); return *this; }
    inline FixedBitVec& operator^=(BitView src) noexcept { duo_fixed_bitvec_xor(&m_inner, src.m_inner); return *this; }

    /** @brief Lexicographical comparison against a bit view. */
    inline int compare(BitView o) const noexcept { return duo_bit_cmp(data(), size(), o.data(), o.size()); }

    /** @brief Computes 64-bit xxHash3 digest of the bit sequence with masked tail bits. */
    inline uint64_t hash(uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept {
        return duo_fixed_bitvec_hash_seed(&m_inner, seed0, seed1);
    }

    // Relational operators between FixedBitVec and FixedBitVec
    inline bool operator==(const FixedBitVec& o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(const FixedBitVec& o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(const FixedBitVec& o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(const FixedBitVec& o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(const FixedBitVec& o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(const FixedBitVec& o) const noexcept { return compare(o.as_view()) >= 0; }

    // Relational operators between FixedBitVec and BitView
    inline bool operator==(BitView o) const noexcept { return compare(o) == 0; }
    inline bool operator!=(BitView o) const noexcept { return compare(o) != 0; }
    inline bool operator<(BitView o) const noexcept  { return compare(o) < 0; }
    inline bool operator<=(BitView o) const noexcept { return compare(o) <= 0; }
    inline bool operator>(BitView o) const noexcept  { return compare(o) > 0; }
    inline bool operator>=(BitView o) const noexcept { return compare(o) >= 0; }

    // Relational operators between FixedBitVec and BitSpan
    inline bool operator==(BitSpan o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(BitSpan o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(BitSpan o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(BitSpan o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(BitSpan o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(BitSpan o) const noexcept { return compare(o.as_view()) >= 0; }

    // Relational operators between FixedBitVec and BitArray
    inline bool operator==(const BitArray& o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(const BitArray& o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(const BitArray& o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(const BitArray& o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(const BitArray& o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(const BitArray& o) const noexcept { return compare(o.as_view()) >= 0; }

    // Relational operators between FixedBitVec and BitVec
    inline bool operator==(const BitVec& o) const noexcept { return compare(o.as_view()) == 0; }
    inline bool operator!=(const BitVec& o) const noexcept { return compare(o.as_view()) != 0; }
    inline bool operator<(const BitVec& o) const noexcept  { return compare(o.as_view()) < 0; }
    inline bool operator<=(const BitVec& o) const noexcept { return compare(o.as_view()) <= 0; }
    inline bool operator>(const BitVec& o) const noexcept  { return compare(o.as_view()) > 0; }
    inline bool operator>=(const BitVec& o) const noexcept { return compare(o.as_view()) >= 0; }

    /** @brief Swaps internal standard-layout C mirror struct with another FixedBitVec. */
    inline void swap(FixedBitVec& other) noexcept {
        duo_fixed_bitvec_t tmp = m_inner;
        m_inner = other.m_inner;
        other.m_inner = tmp;
    }

    // Dual-ABI FFI Operations & Bit Iterators
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_BIT_ITERATOR(m_inner.words, m_inner.bit_count)
};

// Comparison operators between BitView and FixedBitVec
inline bool operator==(BitView a, const FixedBitVec& b) noexcept { return b == a; }
inline bool operator!=(BitView a, const FixedBitVec& b) noexcept { return b != a; }
inline bool operator<(BitView a, const FixedBitVec& b) noexcept  { return b > a; }
inline bool operator<=(BitView a, const FixedBitVec& b) noexcept { return b >= a; }
inline bool operator>(BitView a, const FixedBitVec& b) noexcept  { return b < a; }
inline bool operator>=(BitView a, const FixedBitVec& b) noexcept { return b <= a; }

// Comparison operators between BitSpan and FixedBitVec
inline bool operator==(BitSpan a, const FixedBitVec& b) noexcept { return b == a; }
inline bool operator!=(BitSpan a, const FixedBitVec& b) noexcept { return b != a; }
inline bool operator<(BitSpan a, const FixedBitVec& b) noexcept  { return b > a; }
inline bool operator<=(BitSpan a, const FixedBitVec& b) noexcept { return b >= a; }
inline bool operator>(BitSpan a, const FixedBitVec& b) noexcept  { return b < a; }
inline bool operator>=(BitSpan a, const FixedBitVec& b) noexcept { return b <= a; }

// Comparison operators between BitArray and FixedBitVec
inline bool operator==(const BitArray& a, const FixedBitVec& b) noexcept { return b == a; }
inline bool operator!=(const BitArray& a, const FixedBitVec& b) noexcept { return b != a; }
inline bool operator<(const BitArray& a, const FixedBitVec& b) noexcept  { return b > a; }
inline bool operator<=(const BitArray& a, const FixedBitVec& b) noexcept { return b >= a; }
inline bool operator>(const BitArray& a, const FixedBitVec& b) noexcept  { return b < a; }
inline bool operator>=(const BitArray& a, const FixedBitVec& b) noexcept { return b <= a; }

// Comparison operators between BitVec and FixedBitVec
inline bool operator==(const BitVec& a, const FixedBitVec& b) noexcept { return b == a; }
inline bool operator!=(const BitVec& a, const FixedBitVec& b) noexcept { return b != a; }
inline bool operator<(const BitVec& a, const FixedBitVec& b) noexcept  { return b > a; }
inline bool operator<=(const BitVec& a, const FixedBitVec& b) noexcept { return b >= a; }
inline bool operator>(const BitVec& a, const FixedBitVec& b) noexcept  { return b < a; }
inline bool operator>=(const BitVec& a, const FixedBitVec& b) noexcept { return b <= a; }

static_assert(sizeof(FixedBitVec) == 24, "duo::FixedBitVec must be exactly 24 bytes!");
static_assert(sizeof(FixedBitVec) == sizeof(duo_fixed_bitvec_t), "FixedBitVec size must equal duo_fixed_bitvec_t!");
static_assert(is_standard_layout<FixedBitVec>::value, "duo::FixedBitVec must be standard layout!");

/**
 * @brief Non-member swap overload for duo::FixedBitVec.
 * @param a First fixed bit vector.
 * @param b Second fixed bit vector.
 */
inline void swap(FixedBitVec& a, FixedBitVec& b) noexcept {
    a.swap(b);
}

// ============================================================================
// STACK BIT ARRAY & FIXED BIT VECTOR MACROS
// ============================================================================

/**
 * @def DUO_STACK_BITARRAY(Name, BitCount)
 * @brief Allocates an uninitialized fixed-capacity buffer directly on the stack frame
 * and binds a duo::FixedBitArray view to it. Zero heap allocations.
 * @param Name Variable name for the created FixedBitArray.
 * @param BitCount Total number of bits allocated on the stack frame.
 */
#define DUO_STACK_BITARRAY(Name, BitCount) \
    uint64_t Name##_raw_words_[((BitCount) + 63) / 64] = { 0 }; \
    ::duo::FixedBitArray Name(Name##_raw_words_, (BitCount))

/**
 * @def DUO_STACK_BITARRAY_INIT(Name, BitCount, InitVal)
 * @brief Allocates an initialized fixed-capacity buffer on the stack frame,
 * binding a duo::FixedBitArray with all bits initialized to InitVal.
 * @param Name Variable name for the created FixedBitArray.
 * @param BitCount Total number of bits allocated on the stack frame.
 * @param InitVal Initial boolean value (true for all 1s, false for all 0s).
 */
#define DUO_STACK_BITARRAY_INIT(Name, BitCount, InitVal) \
    uint64_t Name##_raw_words_[((BitCount) + 63) / 64]; \
    ::duo::FixedBitArray Name(Name##_raw_words_, (BitCount)); \
    if (InitVal) { Name.set_all(); } else { Name.clear_all(); }

/**
 * @def DUO_STACK_BITVEC(Name, BitCapacity)
 * @brief Allocates an uninitialized fixed-capacity buffer directly on the stack frame
 * and binds a duo::FixedBitVec to it with initial size 0 and capacity BitCapacity.
 * Zero heap allocations.
 * @param Name Variable name for the created FixedBitVec.
 * @param BitCapacity Total number of bits capacity on the stack frame.
 */
#define DUO_STACK_BITVEC(Name, BitCapacity) \
    uint64_t Name##_raw_words_[((BitCapacity) + 63) / 64] = { 0 }; \
    ::duo::FixedBitVec Name(Name##_raw_words_, (BitCapacity))

/**
 * @def DUO_STACK_BITVEC_INIT(Name, BitCapacity, InitVal)
 * @brief Allocates an initialized fixed-capacity buffer on the stack frame,
 * binding a duo::FixedBitVec with bit count equal to capacity, initialized to InitVal.
 * @param Name Variable name for the created FixedBitVec.
 * @param BitCapacity Total number of bits on the stack frame.
 * @param InitVal Initial boolean value (true for all 1s, false for all 0s).
 */
#define DUO_STACK_BITVEC_INIT(Name, BitCapacity, InitVal) \
    uint64_t Name##_raw_words_[((BitCapacity) + 63) / 64]; \
    ::duo::FixedBitVec Name(Name##_raw_words_, (BitCapacity), (BitCapacity)); \
    if (InitVal) { Name.set_all(); } else { Name.clear_all(); }


/**
 * @brief Zero-heap scoped stack dispatcher for FixedBitVec with transparent dynamic heap fallback.
 *
 * Uses tiered 64-bit word stack buffers (4, 8, 16, 32, 64, 128 words for 256 to 8192 bits)
 * via a switch statement if capacity_bits <= MaxBits. If requested bit capacity exceeds MaxBits,
 * transparently allocates dynamic heap storage with RAII cleanup on scope exit.
 *
 * The visitor callback @p fn always receives a monomorphic reference: duo::FixedBitVec&.
 *
 * @tparam MaxBits Maximum bit capacity eligible for stack allocation (default 8192, multiple of 64).
 * @tparam Fn Visitor callable type with signature (FixedBitVec&) -> R.
 * @param capacity_bits Requested bit capacity.
 * @param fn Visitor callable invoked with the initialized FixedBitVec&.
 * @return Value returned by @p fn.
 */
template <size_t MaxBits = 8192, typename Fn>
inline auto with_stack_bitvec(size_t capacity_bits, Fn&& fn) {
    static_assert(MaxBits % 64 == 0, "MaxBits must be a multiple of 64");
    size_t num_words = (capacity_bits + 63) / 64;
    if (capacity_bits <= MaxBits) {
        switch (duo_stack_tier_words(num_words)) {
#define DUO_CXX_BITVEC_STACK_TIER_CASE_(Words) \
            case Words: { \
                uint64_t stack_words[Words] = { 0 }; \
                FixedBitVec bv(stack_words, capacity_bits); \
                return fn(bv); \
            }
            DUO_CXX_BITVEC_STACK_TIER_CASE_(1)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(2)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(3)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(4)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(5)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(6)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(7)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(8)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(10)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(12)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(14)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(16)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(20)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(24)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(28)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(32)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(40)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(48)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(56)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(64)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(80)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(96)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(112)
            DUO_CXX_BITVEC_STACK_TIER_CASE_(128)
#undef DUO_CXX_BITVEC_STACK_TIER_CASE_
            default:
                break;
        }
    }
    BitArray heap_arr(capacity_bits > 0 ? capacity_bits : 64);
    FixedBitVec bv(heap_arr.data(), capacity_bits);
    return fn(bv);
}

} // namespace duo

#endif /* DUOSTL_BIT_HPP */
