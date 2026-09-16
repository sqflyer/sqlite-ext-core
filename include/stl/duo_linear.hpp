#ifndef DUOSTL_LINEAR_HPP
#define DUOSTL_LINEAR_HPP

/* ============================================================================
 * duo_linear.hpp - Dual-ABI Linear Container Layer for C++17
 *
 * Part of DuoSTL: Zero-dependency embedded container library for SQLite extensions
 * and high-performance native systems.
 *
 * Design Guarantees:
 *   - Zero C++ runtime dependencies (-nostdlib++)
 *   - Zero C++ exceptions (-fno-exceptions)
 *   - Zero C++ Run-Time Type Information (-fno-rtti)
 *   - 100% standard-layout, binary-compatible with pure C C11 ABI structs.
 *
 * Generic Dual-ABI FFI Architecture:
 *   Every C++ container in DuoSTL encapsulates a standard-layout C mirror struct
 *   named `m_inner`. This is the universal, library-wide pattern across all
 *   DuoSTL containers (Span, Vector, Array, String, HashMap, HashSet).
 *
 *   This architectural pattern establishes complete, zero-overhead Foreign Function
 *   Interface (FFI) with pure C APIs:
 *     1. Direct Member Pass:   c_api(container.m_inner);
 *     2. In-Place Reference:   c_api_mutate(&container.m_inner);
 *     3. Implicit Conversion:  c_api(container);
 *
 *   Because `m_inner` is standard-layout and has an identical byte representation
 *   to its corresponding C struct, C++ containers achieve zero-cost interoperability
 *   with C code without data copying, wrapping overhead, or translation layers.
 * ============================================================================ */

#include "duo_alloc.hpp"

extern "C" {
    #include <string.h>
    #include "duo_linear.h"
}

namespace duo {

// ============================================================================
// 1. REVERSE ITERATOR, ACCESSOR & BULK OPERATION MACROS
//
// Macro-Based Container Injection Architecture:
//   Rather than relying on C++ class inheritance (which introduces base-class
//   padding, risks breaking standard-layout ABI guarantees, and prevents
//   compiler-level bit-for-bit equivalence with C structs), DuoSTL uses
//   code-generation macros to inject STL-compliant iterator suites, element
//   accessors, and bulk data transfer algorithms directly into container classes.
//
// Architectural Advantages:
//   1. 100% Standard-Layout: Zero base classes, zero virtual tables (vptrs),
//      and zero padding overhead. Guaranteed binary-compatible with pure C.
//   2. Pluggable Storage: Adapts seamlessly to flat buffers (Span, Array),
//      dynamic heap allocations (Vector), and dual-mode Small Buffer
//      Optimization (SBO) discriminant tags (String, Bytes).
//   3. Universal Interoperability: Duck-typed forwarding references allow any
//      linear container to copy from, move to, or borrow from any other container.
//   4. Hardware SIMD Acceleration: Automatically branches at compile-time via
//      `if constexpr` and `is_trivially_copyable<T>` to hardware-vectorized
//      `memcpy`, `memmove`, and `memset` at memory bus saturation speeds (~30-50 GB/s).
// ============================================================================

/**
 * @def DUO_CXX_BULK_OPS(Type, DataExpr, SizeExpr)
 * @brief Injects high-performance bulk element transfer algorithms into mutable
 * linear collections (Span, Array, Vector).
 *
 * Injected Operations:
 *   - copy_from(SrcView&& src): Bulk copy-assigns up to min(capacity, src.size()) elements.
 *     Hardware accelerated via memcpy when is_trivially_copyable<Type>::value is true.
 *   - copy_from(const Type* src, size_t count): Raw buffer copy-assign overload.
 *   - copy_to(DstSpan&& dst): Bulk copies elements out to any destination span or buffer.
 *   - copy_to(Type* dst, size_t count): Raw buffer destination copy-out overload.
 *   - move_from(SrcSpan&& src): Bulk move-assigns up to min(capacity, src.size()) elements.
 *     Safe for overlapping buffer slices via memmove when is_trivially_copyable<Type>::value is true.
 *   - move_from(Type* src, size_t count): Raw buffer move-assign overload.
 *   - move_to(DstSpan&& dst): Bulk moves elements out to any destination span or buffer.
 *   - move_to(Type* dst, size_t count): Raw buffer destination move-out overload.
 *   - fill(const Type& val): Sets all elements to @p val. Uses memset fast-path for
 *     single-byte trivially copyable types (e.g. uint8_t, char).
 *
 * Safety & Performance Guarantees:
 *   - Zero Buffer Overflows: Automatically clamps element count to min(capacity, source.size()).
 *   - Zero-Cost Abstraction: Discards unused branches at compile-time via `if constexpr`.
 *   - Overlap Safety: Uses memmove for trivial types to support in-place sliding window shifts.
 *
 * @param Type Element type contained in the collection.
 * @param DataExpr Expression yielding a pointer to the contiguous element buffer.
 * @param SizeExpr Expression yielding the number of valid elements in the buffer.
 */
#define DUO_CXX_BULK_OPS(Type, DataExpr, SizeExpr) \
    template <typename SrcView> \
    inline size_t copy_from(SrcView&& src) noexcept { \
        Type* d = reinterpret_cast<Type*>(DataExpr); \
        size_t cap = (SizeExpr); \
        size_t n = cap < src.size() ? cap : src.size(); \
        if (n > 0 && d && src.data()) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMCPY(d, src.data(), n * sizeof(Type)); \
            } else { \
                for (size_t i = 0; i < n; ++i) { \
                    d[i] = src.data()[i]; \
                } \
            } \
        } \
        return n; \
    } \
    inline size_t copy_from(const Type* src, size_t count) noexcept { \
        Type* d = reinterpret_cast<Type*>(DataExpr); \
        size_t cap = (SizeExpr); \
        size_t n = cap < count ? cap : count; \
        if (n > 0 && d && src) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMCPY(d, src, n * sizeof(Type)); \
            } else { \
                for (size_t i = 0; i < n; ++i) { \
                    d[i] = src[i]; \
                } \
            } \
        } \
        return n; \
    } \
    template <typename DstSpan> \
    inline size_t copy_to(DstSpan&& dst) const noexcept { \
        const Type* s = reinterpret_cast<const Type*>(DataExpr); \
        size_t sz = (SizeExpr); \
        size_t n = sz < dst.size() ? sz : dst.size(); \
        if (n > 0 && s && dst.data()) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMCPY(dst.data(), s, n * sizeof(Type)); \
            } else { \
                for (size_t i = 0; i < n; ++i) { \
                    dst.data()[i] = s[i]; \
                } \
            } \
        } \
        return n; \
    } \
    inline size_t copy_to(Type* dst, size_t count) const noexcept { \
        const Type* s = reinterpret_cast<const Type*>(DataExpr); \
        size_t sz = (SizeExpr); \
        size_t n = sz < count ? sz : count; \
        if (n > 0 && s && dst) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMCPY(dst, s, n * sizeof(Type)); \
            } else { \
                for (size_t i = 0; i < n; ++i) { \
                    dst[i] = s[i]; \
                } \
            } \
        } \
        return n; \
    } \
    template <typename SrcSpan> \
    inline size_t move_from(SrcSpan&& src) noexcept { \
        Type* d = reinterpret_cast<Type*>(DataExpr); \
        size_t cap = (SizeExpr); \
        size_t n = cap < src.size() ? cap : src.size(); \
        if (n > 0 && d && src.data()) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMMOVE(d, src.data(), n * sizeof(Type)); \
            } else { \
                if (d < src.data()) { \
                    for (size_t i = 0; i < n; ++i) { \
                        d[i] = duo::move(src.data()[i]); \
                    } \
                } else if (d > src.data()) { \
                    for (size_t i = n; i > 0; --i) { \
                        d[i - 1] = duo::move(src.data()[i - 1]); \
                    } \
                } \
            } \
        } \
        return n; \
    } \
    inline size_t move_from(Type* src, size_t count) noexcept { \
        Type* d = reinterpret_cast<Type*>(DataExpr); \
        size_t cap = (SizeExpr); \
        size_t n = cap < count ? cap : count; \
        if (n > 0 && d && src) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMMOVE(d, src, n * sizeof(Type)); \
            } else { \
                if (d < src) { \
                    for (size_t i = 0; i < n; ++i) { \
                        d[i] = duo::move(src[i]); \
                    } \
                } else if (d > src) { \
                    for (size_t i = n; i > 0; --i) { \
                        d[i - 1] = duo::move(src[i - 1]); \
                    } \
                } \
            } \
        } \
        return n; \
    } \
    template <typename DstSpan> \
    inline size_t move_to(DstSpan&& dst) noexcept { \
        Type* s = reinterpret_cast<Type*>(DataExpr); \
        size_t sz = (SizeExpr); \
        size_t n = sz < dst.size() ? sz : dst.size(); \
        if (n > 0 && s && dst.data()) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMMOVE(dst.data(), s, n * sizeof(Type)); \
            } else { \
                if (dst.data() < s) { \
                    for (size_t i = 0; i < n; ++i) { \
                        dst.data()[i] = duo::move(s[i]); \
                    } \
                } else if (dst.data() > s) { \
                    for (size_t i = n; i > 0; --i) { \
                        dst.data()[i - 1] = duo::move(s[i - 1]); \
                    } \
                } \
            } \
        } \
        return n; \
    } \
    inline size_t move_to(Type* dst, size_t count) noexcept { \
        Type* s = reinterpret_cast<Type*>(DataExpr); \
        size_t sz = (SizeExpr); \
        size_t n = sz < count ? sz : count; \
        if (n > 0 && s && dst) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMMOVE(dst, s, n * sizeof(Type)); \
            } else { \
                if (dst < s) { \
                    for (size_t i = 0; i < n; ++i) { \
                        dst[i] = duo::move(s[i]); \
                    } \
                } else if (dst > s) { \
                    for (size_t i = n; i > 0; --i) { \
                        dst[i - 1] = duo::move(s[i - 1]); \
                    } \
                } \
            } \
        } \
        return n; \
    } \
    inline auto fill(const Type& val) noexcept -> decltype(*this)& { \
        Type* d = reinterpret_cast<Type*>(DataExpr); \
        size_t sz = (SizeExpr); \
        if (d && sz > 0) { \
            if constexpr (sizeof(Type) == 1 && is_trivially_copyable<Type>::value) { \
                DUO_MEMSET(d, static_cast<int>(static_cast<uint8_t>(val)), sz); \
            } else { \
                for (size_t i = 0; i < sz; ++i) { \
                    d[i] = val; \
                } \
            } \
        } \
        return *this; \
    } \
    inline auto zero() noexcept -> decltype(*this)& { \
        Type* d = reinterpret_cast<Type*>(DataExpr); \
        size_t sz = (SizeExpr); \
        if (d && sz > 0) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMSET(d, 0, sz * sizeof(Type)); \
            } else { \
                for (size_t i = 0; i < sz; ++i) { \
                    d[i] = Type{}; \
                } \
            } \
        } \
        return *this; \
    }

/**
 * @def DUO_CXX_VIEW_BULK_OPS(Type, DataExpr, SizeExpr)
 * @brief Injects bulk copy-out operations into immutable collections (SpanView).
 *
 * Injected Operations:
 *   - copy_to(DstSpan&& dst): Bulk copies elements out to any destination span or container.
 *     Hardware SIMD accelerated via memcpy for trivially copyable types.
 *   - copy_to(Type* dst, size_t count): Raw destination buffer copy-out overload.
 *
 * @param Type Element type contained in the view.
 * @param DataExpr Expression yielding a const pointer to the contiguous buffer.
 * @param SizeExpr Expression yielding the number of valid elements in the view.
 */
#define DUO_CXX_VIEW_BULK_OPS(Type, DataExpr, SizeExpr) \
    template <typename DstSpan> \
    inline size_t copy_to(DstSpan&& dst) const noexcept { \
        const Type* s = reinterpret_cast<const Type*>(DataExpr); \
        size_t sz = (SizeExpr); \
        size_t n = sz < dst.size() ? sz : dst.size(); \
        if (n > 0 && s && dst.data()) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMCPY(dst.data(), s, n * sizeof(Type)); \
            } else { \
                for (size_t i = 0; i < n; ++i) { \
                    dst.data()[i] = s[i]; \
                } \
            } \
        } \
        return n; \
    } \
    inline size_t copy_to(Type* dst, size_t count) const noexcept { \
        const Type* s = reinterpret_cast<const Type*>(DataExpr); \
        size_t sz = (SizeExpr); \
        size_t n = sz < count ? sz : count; \
        if (n > 0 && s && dst) { \
            if constexpr (is_trivially_copyable<Type>::value) { \
                DUO_MEMCPY(dst, s, n * sizeof(Type)); \
            } else { \
                for (size_t i = 0; i < n; ++i) { \
                    dst[i] = s[i]; \
                } \
            } \
        } \
        return n; \
    }

/**
 * @def DUO_CXX_DERIVE_HASH(DataExpr, SizeExpr)
 * @brief Injects standard 64-bit member .hash() and .hash(seed0, seed1) using xxHash3
 * into linear containers (String, FixedString, StringView, Bytes, FixedBytes, etc.).
 *
 * @param DataExpr Expression yielding a const pointer to contiguous memory.
 * @param SizeExpr Expression yielding byte or character length.
 */
#define DUO_CXX_DERIVE_HASH(DataExpr, SizeExpr) \
    inline uint64_t hash(uint64_t seed0 = 0, uint64_t seed1 = 0) const noexcept { \
        return duo_hash_xxhash3((DataExpr), (SizeExpr), seed0, seed1); \
    }

/**
 * @def DUO_CXX_DERIVE_ORDERING_OPS(LhsType, RhsType, CmpFn)
 * @brief Generates full 6-way non-member relational operators (==, !=, <, <=, >, >=)
 * between LhsType and RhsType from a 3-way comparator function returning <0, 0, >0.
 *
 * @param LhsType Left-hand operand type.
 * @param RhsType Right-hand operand type.
 * @param CmpFn Comparator function or callable with signature int(const LhsType&, const RhsType&).
 */
#define DUO_CXX_DERIVE_ORDERING_OPS(LhsType, RhsType, CmpFn) \
    inline bool operator==(const LhsType& a, const RhsType& b) noexcept { return (CmpFn)(a, b) == 0; } \
    inline bool operator!=(const LhsType& a, const RhsType& b) noexcept { return (CmpFn)(a, b) != 0; } \
    inline bool operator<(const LhsType& a, const RhsType& b) noexcept  { return (CmpFn)(a, b) < 0; } \
    inline bool operator<=(const LhsType& a, const RhsType& b) noexcept { return (CmpFn)(a, b) <= 0; } \
    inline bool operator>(const LhsType& a, const RhsType& b) noexcept  { return (CmpFn)(a, b) > 0; } \
    inline bool operator>=(const LhsType& a, const RhsType& b) noexcept { return (CmpFn)(a, b) >= 0; }

template <typename C, typename = void>
struct has_capacity : false_type {};

template <typename C>
struct has_capacity<C, void_t<decltype(static_cast<size_t>(static_cast<C*>(nullptr)->capacity))>> : true_type {};

template <typename C, typename = void>
struct has_data : false_type {};

template <typename C>
struct has_data<C, void_t<decltype(static_cast<C*>(nullptr)->data)>> : true_type {};

template <>
struct duo_c_reset_helper<duo_string_t> {
    static inline void apply(duo_string_t& c) noexcept {
        duo_string_init(&c);
    }
};

template <typename Container, typename Ptr>
struct duo_container_adopt_raw_helper<Container, Ptr, void_t<typename Container::c_type>> {
    static inline void apply(Container& a, Ptr* data, size_t size, size_t capacity) noexcept {
        if constexpr (has_data<typename Container::c_type>::value) {
            a.m_inner.data = const_cast<decltype(a.m_inner.data)>(data);
            a.m_inner.size = size;
            if constexpr (has_capacity<typename Container::c_type>::value) {
                a.m_inner.capacity = (capacity >= size ? capacity : size);
            }
        } else if constexpr (is_same<typename Container::c_type, duo_bytes_t>::value) {
            if constexpr (is_same<typename Container::value_type, char>::value) {
                duo_string_init_len(&a.m_inner, reinterpret_cast<const char*>(data), size);
                (void)capacity;
            } else {
                duo_bytes_init_from(&a.m_inner, data, size);
                (void)capacity;
            }
        }
    }
};

/**
 * @def DUO_CXX_SPAN_CONVERSIONS(Type, DataExpr, SizeExpr)
 * @brief Injects implicit conversions to generic C spans (duo_span_t, duo_span_view_t)
 * and arbitrary external C-style structs having .data and .size members into mutable containers.
 */
#define DUO_CXX_SPAN_CONVERSIONS(Type, DataExpr, SizeExpr) \
    inline operator duo_span_t() const noexcept { \
        duo_span_t s; \
        s.data = const_cast<void*>(static_cast<const void*>(DataExpr)); \
        s.size = (SizeExpr) * sizeof(Type); \
        return s; \
    } \
    inline operator duo_span_view_t() const noexcept { \
        duo_span_view_t s; \
        s.data = static_cast<const void*>(DataExpr); \
        s.size = (SizeExpr) * sizeof(Type); \
        return s; \
    } \
    template <typename CSpan, typename = decltype(static_cast<CSpan*>(nullptr)->data = static_cast<Type*>(nullptr)), \
                              typename = decltype(static_cast<size_t>(static_cast<CSpan*>(nullptr)->size))> \
    inline operator CSpan() const noexcept { \
        CSpan s; \
        s.data = const_cast<Type*>(static_cast<const Type*>(DataExpr)); \
        s.size = (SizeExpr); \
        return s; \
    }

/**
 * @def DUO_CXX_VIEW_SPAN_CONVERSIONS(Type, DataExpr, SizeExpr)
 * @brief Injects implicit conversions to generic C span view (duo_span_view_t)
 * and arbitrary external C-style structs having const .data and .size members into immutable view containers.
 */
#define DUO_CXX_VIEW_SPAN_CONVERSIONS(Type, DataExpr, SizeExpr) \
    inline operator duo_span_view_t() const noexcept { \
        duo_span_view_t s; \
        s.data = static_cast<const void*>(DataExpr); \
        s.size = (SizeExpr) * sizeof(Type); \
        return s; \
    } \
    template <typename CSpan, typename = decltype(static_cast<CSpan*>(nullptr)->data = static_cast<const Type*>(nullptr)), \
                              typename = decltype(static_cast<size_t>(static_cast<CSpan*>(nullptr)->size))> \
    inline operator CSpan() const noexcept { \
        CSpan s; \
        s.data = static_cast<const Type*>(DataExpr); \
        s.size = (SizeExpr); \
        return s; \
    }

/**
 * @def DUO_CXX_C_STRUCT_SPAN_CONVERSIONS(Type, DataExpr, SizeExpr)
 * @brief Injects implicit conversions into C mirror structs (c_vector_t, c_array_t, etc.)
 * allowing seamless conversion to typed C spans (c_span_t<T>, c_span_view_t<T>)
 * and generic C spans (duo_span_t, duo_span_view_t).
 */
#define DUO_CXX_C_STRUCT_SPAN_CONVERSIONS(Type, DataExpr, SizeExpr) \
    inline operator c_span_t<Type>() const noexcept { \
        return c_span_t<Type>{ const_cast<Type*>(static_cast<const Type*>(DataExpr)), (SizeExpr) }; \
    } \
    inline operator c_span_view_t<Type>() const noexcept { \
        return c_span_view_t<Type>{ static_cast<const Type*>(DataExpr), (SizeExpr) }; \
    } \
    inline operator duo_span_t() const noexcept { \
        duo_span_t s; \
        s.data = const_cast<void*>(static_cast<const void*>(DataExpr)); \
        s.size = (SizeExpr) * sizeof(Type); \
        return s; \
    } \
    inline operator duo_span_view_t() const noexcept { \
        duo_span_view_t s; \
        s.data = static_cast<const void*>(DataExpr); \
        s.size = (SizeExpr) * sizeof(Type); \
        return s; \
    }

/**
 * @def DUO_CXX_STANDARD_ACCESSORS(Type, DataExpr, SizeExpr)
 * @brief Injects standard STL-compatible iterator suites, element accessors,
 * bulk operations, and C span conversions into mutable linear containers (Span, Vector, Array).
 *
 * Injected Facilities:
 *   - Type Aliases: iterator, const_iterator, reverse_iterator, const_reverse_iterator
 *   - Forward Iterators: begin(), end(), cbegin(), cend()
 *   - Reverse Iterators: rbegin(), rend(), crbegin(), crend()
 *   - Subscripts & Accessors: operator[](size_t), front(), back() (mutable and const)
 *   - Bulk Operations: Integrated DUO_CXX_BULK_OPS (copy_from, copy_to, move_from, move_to, fill, zero)
 *   - C Conversions: Integrated DUO_CXX_SPAN_CONVERSIONS (duo_span_t, duo_span_view_t, CSpan)
 *
 * @param Type Element type contained in the collection.
 * @param DataExpr Expression yielding a pointer to the contiguous buffer.
 * @param SizeExpr Expression yielding the number of elements in the buffer.
 */
#define DUO_CXX_STANDARD_ACCESSORS(Type, DataExpr, SizeExpr) \
    using iterator               = Type*; \
    using const_iterator         = const Type*; \
    using reverse_iterator       = duo::ReverseIterator<iterator>; \
    using const_reverse_iterator = duo::ReverseIterator<const_iterator>; \
    inline iterator begin() noexcept               { return reinterpret_cast<Type*>(DataExpr); } \
    inline iterator end() noexcept                 { return begin() + (SizeExpr); } \
    inline const_iterator begin() const noexcept   { return reinterpret_cast<const Type*>(DataExpr); } \
    inline const_iterator end() const noexcept     { return begin() + (SizeExpr); } \
    inline const_iterator cbegin() const noexcept  { return begin(); } \
    inline const_iterator cend() const noexcept    { return end(); } \
    inline reverse_iterator rbegin() noexcept             { return reverse_iterator(end()); } \
    inline reverse_iterator rend() noexcept               { return reverse_iterator(begin()); } \
    inline const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); } \
    inline const_reverse_iterator rend() const noexcept   { return const_reverse_iterator(begin()); } \
    inline const_reverse_iterator crbegin() const noexcept{ return const_reverse_iterator(end()); } \
    inline const_reverse_iterator crend() const noexcept  { return const_reverse_iterator(begin()); } \
    inline Type& operator[](size_t i) noexcept             { return begin()[i]; } \
    inline const Type& operator[](size_t i) const noexcept { return begin()[i]; } \
    inline Type& front() noexcept                          { return begin()[0]; } \
    inline const Type& front() const noexcept              { return begin()[0]; } \
    inline Type& back() noexcept                           { return begin()[(SizeExpr) - 1]; } \
    inline const Type& back() const noexcept               { return begin()[(SizeExpr) - 1]; } \
    inline size_t size() const noexcept                    { return (SizeExpr); } \
    inline size_t size_bytes() const noexcept              { return (SizeExpr) * sizeof(Type); } \
    inline bool empty() const noexcept                     { return (SizeExpr) == 0; } \
    inline Type* data() noexcept                           { return reinterpret_cast<Type*>(DataExpr); } \
    inline const Type* data() const noexcept               { return reinterpret_cast<const Type*>(DataExpr); } \
    DUO_CXX_BULK_OPS(Type, DataExpr, SizeExpr) \
    DUO_CXX_SPAN_CONVERSIONS(Type, DataExpr, SizeExpr)

/**
 * @def DUO_CXX_VIEW_ACCESSORS(Type, DataExpr, SizeExpr)
 * @brief Injects strictly immutable iterator suites, element accessors,
 * bulk operations, and C span conversions into read-only view containers (SpanView).
 *
 * Injected Facilities:
 *   - Type Aliases: iterator (const), const_iterator, reverse_iterator (const), const_reverse_iterator
 *   - Forward Iterators: begin(), end(), cbegin(), cend() (strictly const-qualified)
 *   - Reverse Iterators: rbegin(), rend(), crbegin(), crend() (strictly const-qualified)
 *   - Subscripts & Accessors: operator[](size_t), front(), back() (strictly const references)
 *   - Capacity & Buffer: size(), size_bytes(), empty(), data() (const)
 *   - Bulk Operations: Integrated DUO_CXX_VIEW_BULK_OPS (copy_to)
 *   - C Conversions: Integrated DUO_CXX_VIEW_SPAN_CONVERSIONS (duo_span_view_t, CSpan)
 *
 * @param Type Element type contained in the view.
 * @param DataExpr Expression yielding a const pointer to the contiguous buffer.
 * @param SizeExpr Expression yielding the number of elements in the view.
 */
#define DUO_CXX_VIEW_ACCESSORS(Type, DataExpr, SizeExpr) \
    using iterator               = const Type*; \
    using const_iterator         = const Type*; \
    using reverse_iterator       = duo::ReverseIterator<const_iterator>; \
    using const_reverse_iterator = duo::ReverseIterator<const_iterator>; \
    inline iterator begin() const noexcept                 { return reinterpret_cast<const Type*>(DataExpr); } \
    inline iterator end() const noexcept                   { return begin() + (SizeExpr); } \
    inline const_iterator cbegin() const noexcept          { return begin(); } \
    inline const_iterator cend() const noexcept            { return end(); } \
    inline reverse_iterator rbegin() const noexcept        { return reverse_iterator(end()); } \
    inline reverse_iterator rend() const noexcept          { return reverse_iterator(begin()); } \
    inline const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); } \
    inline const_reverse_iterator crend() const noexcept   { return const_reverse_iterator(begin()); } \
    inline const Type& operator[](size_t i) const noexcept { return begin()[i]; } \
    inline const Type& front() const noexcept              { return begin()[0]; } \
    inline const Type& back() const noexcept               { return begin()[(SizeExpr) - 1]; } \
    inline size_t size() const noexcept                    { return (SizeExpr); } \
    inline size_t size_bytes() const noexcept              { return (SizeExpr) * sizeof(Type); } \
    inline bool empty() const noexcept                     { return (SizeExpr) == 0; } \
    inline const Type* data() const noexcept               { return reinterpret_cast<const Type*>(DataExpr); } \
    DUO_CXX_VIEW_BULK_OPS(Type, DataExpr, SizeExpr) \
    DUO_CXX_VIEW_SPAN_CONVERSIONS(Type, DataExpr, SizeExpr)

// ============================================================================
// 2. C STRUCT MIRRORS & SPAN CLASSES (m_inner standard layout, exactly 16 bytes)
//
// Memory Layout & Dual-ABI Architecture:
//
//   C++17 duo::Span<T>                Pure C C11 duo_span_t / Typed Span
//   +-----------------------+         +-----------------------+
//   | m_inner.data  (8 B)   |  <===>  | data (void* / T*)     |
//   +-----------------------+         +-----------------------+
//   | m_inner.size  (8 B)   |  <===>  | size (size_t)         |
//   +-----------------------+         +-----------------------+
//   Total: 16 Bytes (64-bit)          Total: 16 Bytes (64-bit)
//   is_standard_layout == true        is_standard_layout == true
//
// Dual-ABI Interoperability:
//   Following DuoSTL's generic FFI style, Span<T> wraps a standard-layout C
//   mirror struct `c_span_t<T> m_inner`, while SpanView<T> wraps
//   `c_span_view_t<T> m_inner`.
//
//   This layout guarantees 100% binary compatibility with:
//     - Generic pure C spans: duo_span_t, duo_span_view_t
//     - Macro-generated typed C spans: DUO_C_SPAN_TYPE, DUO_C_SPAN_VIEW_TYPE
//     - External user-defined C structs having { T* data; size_t size; }
//
//   C++ spans can be passed directly to C functions via:
//     1. Direct field access:  c_function(s.m_inner);
//     2. Address of inner:     c_function(&s.m_inner);
//     3. Implicit conversion:  c_function(s);
// ============================================================================

/**
 * @struct c_span_view_t
 * @brief Standard-layout C mirror struct representing an immutable typed span view.
 * Exactly 16 bytes on 64-bit platforms, ABI-identical to pure C typed span views.
 * @tparam T Element type.
 */
template <typename T>
struct c_span_view_t {
    const T* data; /**< Pointer to contiguous elements. */
    size_t   size; /**< Number of elements in the view. */

    /**
     * @brief Implicit conversion to generic C duo_span_view_t.
     */
    inline operator duo_span_view_t() const noexcept {
        return duo_span_view_t{ static_cast<const void*>(data), size * sizeof(T) };
    }
};

/**
 * @struct c_span_t
 * @brief Standard-layout C mirror struct representing a mutable typed span.
 * Exactly 16 bytes on 64-bit platforms, ABI-identical to pure C typed spans.
 * @tparam T Element type.
 */
template <typename T>
struct c_span_t {
    T*     data; /**< Pointer to contiguous elements. */
    size_t size; /**< Number of elements in the span. */

    /**
     * @brief Implicit conversion to typed immutable view mirror.
     */
    inline operator c_span_view_t<T>() const noexcept {
        return c_span_view_t<T>{ data, size };
    }

    /**
     * @brief Implicit conversion to generic mutable C duo_span_t.
     */
    inline operator duo_span_t() const noexcept {
        return duo_span_t{ const_cast<void*>(static_cast<const void*>(data)), size * sizeof(T) };
    }

    /**
     * @brief Implicit conversion to generic immutable C duo_span_view_t.
     */
    inline operator duo_span_view_t() const noexcept {
        return duo_span_view_t{ static_cast<const void*>(data), size * sizeof(T) };
    }
};

template <typename T> class Span;
template <typename T> class SpanView;

/**
 * @class Span
 * @brief Freestanding non-owning mutable contiguous buffer slice for C++17.
 *
 * Wraps a standard-layout C mirror struct `m_inner` of type c_span_t<T>.
 * Guarantees zero runtime overhead, exactly 16 bytes, and standard-layout.
 *
 * @tparam T Element type.
 */
template <typename T>
class Span {
public:
    using self_type = Span;
    using c_type    = c_span_t<T>;
    c_type m_inner; /**< Standard-layout C mirror struct passed directly to C APIs. */

    /** @brief Constructs an empty span (data = nullptr, size = 0). */
    inline Span() noexcept : m_inner{nullptr, 0} {}

    /**
     * @brief Constructs a span over a pointer and element count.
     * @param d Pointer to the start of contiguous buffer.
     * @param l Number of elements in the buffer.
     */
    inline Span(T* d, size_t l) noexcept : m_inner{d, l} {}

    /**
     * @brief Constructs a span over a half-open pointer range [first, last).
     * @param first Pointer to the first element.
     * @param last Pointer one past the last element.
     */
    inline Span(T* first, T* last) noexcept : m_inner{first, last >= first ? static_cast<size_t>(last - first) : 0} {}

    /**
     * @brief Constructs a span directly from its C mirror struct.
     * @param inner The standard-layout c_span_t<T> struct.
     */
    inline Span(c_type inner) noexcept : m_inner(inner) {}

    /**
     * @brief Constructs a span from any external C-style typed span struct having .data and .size.
     * @tparam CSpan External struct type.
     * @param c Source C-style span struct.
     */
    template <typename CSpan, typename = decltype(static_cast<T*>(static_cast<CSpan*>(nullptr)->data)),
                              typename = decltype(static_cast<size_t>(static_cast<CSpan*>(nullptr)->size))>
    inline Span(const CSpan& c) noexcept : m_inner{c.data, c.size} {}

    /** @brief Implicit promotion from mutable Span<T> to immutable SpanView<T>. */
    inline operator SpanView<T>() const noexcept;

    /** @brief Self-borrow convenience method returning a copy of this span. */
    inline Span<T> as_span() const noexcept { return *this; }

    /** @brief Borrows an immutable SpanView<T> over this span. */
    inline SpanView<T> as_view() const noexcept;

    /**
     * @brief Slices a subspan with bounds clamping.
     * @param offset Element offset from start of span.
     * @param count Maximum number of elements to include.
     * @return Clamped subspan.
     */
    inline Span<T> subspan(size_t offset, size_t count = static_cast<size_t>(-1)) const noexcept {
        if (offset > m_inner.size) offset = m_inner.size;
        size_t available = m_inner.size - offset;
        if (count > available) count = available;
        return Span<T>(m_inner.data + offset, count);
    }

    DUO_CXX_DERIVE_HASH(m_inner.data, m_inner.size * sizeof(T))
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_STANDARD_ACCESSORS(T, m_inner.data, m_inner.size)
};

/**
 * @class SpanView
 * @brief Freestanding non-owning immutable contiguous buffer slice for C++17.
 *
 * Wraps a standard-layout C mirror struct `m_inner` of type c_span_view_t<T>.
 * Elements are strictly const-qualified and cannot be mutated through this view.
 *
 * @tparam T Element type.
 */
template <typename T>
class SpanView {
public:
    using self_type = SpanView;
    using c_type    = c_span_view_t<T>;
    c_type m_inner; /**< Standard-layout C mirror struct passed directly to C APIs. */

    /** @brief Constructs an empty view (data = nullptr, size = 0). */
    inline SpanView() noexcept : m_inner{nullptr, 0} {}

    /**
     * @brief Constructs a view over a const pointer and element count.
     * @param d Pointer to the start of contiguous buffer.
     * @param l Number of elements in the buffer.
     */
    inline SpanView(const T* d, size_t l) noexcept : m_inner{d, l} {}

    /**
     * @brief Constructs a view over a half-open pointer range [first, last).
     * @param first Pointer to the first element.
     * @param last Pointer one past the last element.
     */
    inline SpanView(const T* first, const T* last) noexcept : m_inner{first, last >= first ? static_cast<size_t>(last - first) : 0} {}

    /**
     * @brief Constructs a view directly from its C mirror struct.
     * @param inner The standard-layout c_span_view_t<T> struct.
     */
    inline SpanView(c_type inner) noexcept : m_inner(inner) {}

    /**
     * @brief Constructs an immutable view from a mutable Span<T>.
     * @param s Source mutable span.
     */
    inline SpanView(const Span<T>& s) noexcept : m_inner{s.m_inner.data, s.m_inner.size} {}

    /**
     * @brief Constructs a view from any external C-style typed span struct having .data and .size.
     * @tparam CSpan External struct type.
     * @param c Source C-style span struct.
     */
    template <typename CSpan, typename = decltype(static_cast<const T*>(static_cast<CSpan*>(nullptr)->data)),
                              typename = decltype(static_cast<size_t>(static_cast<CSpan*>(nullptr)->size))>
    inline SpanView(const CSpan& c) noexcept : m_inner{c.data, c.size} {}

    /** @brief Self-borrow convenience method returning a copy of this view. */
    inline SpanView<T> as_span() const noexcept { return *this; }

    /** @brief Self-borrow convenience method returning a copy of this view. */
    inline SpanView<T> as_view() const noexcept { return *this; }

    /**
     * @brief Slices a subview with bounds clamping.
     * @param offset Element offset from start of view.
     * @param count Maximum number of elements to include.
     * @return Clamped subview.
     */
    inline SpanView<T> subspan(size_t offset, size_t count = static_cast<size_t>(-1)) const noexcept {
        if (offset > m_inner.size) offset = m_inner.size;
        size_t available = m_inner.size - offset;
        if (count > available) count = available;
        return SpanView<T>(m_inner.data + offset, count);
    }

    DUO_CXX_DERIVE_HASH(m_inner.data, m_inner.size * sizeof(T))
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_VIEW_ACCESSORS(T, m_inner.data, m_inner.size)
};

template <typename T>
inline Span<T>::operator SpanView<T>() const noexcept {
    return SpanView<T>(m_inner.data, m_inner.size);
}

template <typename T>
inline SpanView<T> Span<T>::as_view() const noexcept {
    return SpanView<T>(m_inner.data, m_inner.size);
}

// ----------------------------------------------------------------------------
// Relational operators for SpanView<T>
// ----------------------------------------------------------------------------
template <typename T>
inline bool operator==(SpanView<T> a, SpanView<T> b) noexcept {
    if (a.size() != b.size()) return false;
    if (a.size() == 0) return true;
    if constexpr (is_trivially_copyable<T>::value) {
        return memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
    } else {
        for (size_t i = 0; i < a.size(); ++i) {
            if (!(a[i] == b[i])) return false;
        }
        return true;
    }
}
template <typename T>
inline bool operator!=(SpanView<T> a, SpanView<T> b) noexcept {
    return !(a == b);
}
template <typename T>
inline bool operator<(SpanView<T> a, SpanView<T> b) noexcept {
    size_t min_sz = a.size() < b.size() ? a.size() : b.size();
    if constexpr (is_trivially_copyable<T>::value) {
        if (min_sz > 0) {
            int r = memcmp(a.data(), b.data(), min_sz * sizeof(T));
            if (r != 0) return r < 0;
        }
    } else {
        for (size_t i = 0; i < min_sz; ++i) {
            if (a[i] < b[i]) return true;
            if (b[i] < a[i]) return false;
        }
    }
    return a.size() < b.size();
}
template <typename T>
inline bool operator<=(SpanView<T> a, SpanView<T> b) noexcept { return !(b < a); }
template <typename T>
inline bool operator>(SpanView<T> a, SpanView<T> b) noexcept { return b < a; }
template <typename T>
inline bool operator>=(SpanView<T> a, SpanView<T> b) noexcept { return !(a < b); }

// Static ABI assertions for Span
static_assert(sizeof(Span<int>) == 16, "Span<T> must be exactly 16 bytes!");
static_assert(sizeof(SpanView<int>) == 16, "SpanView<T> must be exactly 16 bytes!");
static_assert(is_standard_layout<Span<int>>::value, "Span<T> must be standard layout!");
static_assert(is_standard_layout<SpanView<int>>::value, "SpanView<T> must be standard layout!");
static_assert(is_trivially_copyable<Span<int>>::value, "Span<T> must be trivially copyable!");
static_assert(is_trivially_copyable<SpanView<int>>::value, "SpanView<T> must be trivially copyable!");

/** @brief Semantic alias for non-owning immutable view over contiguous bytes. */
using BytesView = SpanView<uint8_t>;

/**
 * @def DUO_CXX_SPAN_BORROW_OPS(Type, DataExpr, SizeExpr)
 * @brief Injects non-owning Span/SpanView borrowing, slicing, and conversion operators
 * into owning linear containers (Array, Vector).
 *
 * Injected Facilities:
 *   - as_span(): Borrows a mutable non-owning Span<Type> over the buffer
 *   - as_view(): Borrows an immutable non-owning SpanView<Type> over the buffer
 *   - subspan(offset, count): Slices a clamped mutable or immutable subspan
 *   - operator Span<Type>(): Implicit conversion to mutable Span<Type>
 *   - operator SpanView<Type>(): Implicit conversion to immutable SpanView<Type>
 *
 * @param Type Element type contained in the container.
 * @param DataExpr Expression yielding a pointer to the contiguous buffer.
 * @param SizeExpr Expression yielding the number of elements in the buffer.
 */
#define DUO_CXX_SPAN_BORROW_OPS(Type, DataExpr, SizeExpr) \
    inline Span<Type> as_span() noexcept { return Span<Type>(reinterpret_cast<Type*>(DataExpr), (SizeExpr)); } \
    inline SpanView<Type> as_view() const noexcept { return SpanView<Type>(reinterpret_cast<const Type*>(DataExpr), (SizeExpr)); } \
    inline Span<Type> subspan(size_t offset, size_t count = static_cast<size_t>(-1)) noexcept { \
        return as_span().subspan(offset, count); \
    } \
    inline SpanView<Type> subspan(size_t offset, size_t count = static_cast<size_t>(-1)) const noexcept { \
        return as_view().subspan(offset, count); \
    } \
    inline operator Span<Type>() noexcept { return as_span(); } \
    inline operator SpanView<Type>() const noexcept { return as_view(); }

// ============================================================================
// 4. C++17 DYNAMIC AUTO-GROWING VECTOR: duo::Vector<T> & duo::c_vector_t<T>
// ============================================================================

/**
 * @struct c_vector_t
 * @brief Standard-layout C mirror struct representing a dynamic auto-growing vector.
 * Exactly 24 bytes on 64-bit platforms, ABI-identical to pure C DUO_C_VEC_TYPE.
 * Member order is { size_t size; size_t capacity; T* data; }.
 *
 * @tparam T Element type.
 */
template <typename T>
struct c_vector_t {
    size_t size;     /**< Number of active elements. */
    size_t capacity; /**< Total allocated element capacity. */
    T*     data;     /**< Pointer to contiguous heap buffer. */

    DUO_CXX_C_STRUCT_SPAN_CONVERSIONS(T, data, size)
};

/**
 * @class Vector
 * @brief Freestanding RAII dynamic auto-growing vector container for C++17.
 *
 * Wraps a standard-layout C mirror struct `m_inner` of type c_vector_t<T>
 * ({ size_t size; size_t capacity; T* data; }, exactly 24 bytes on 64-bit platforms).
 *
 * Features:
 *   - Freestanding (-nostdlib++), zero-exception, zero-RTTI.
 *   - Geometric capacity doubling (starting at 4 elements).
 *   - RAII resource management via duo::allocate and duo::deallocate.
 *   - Placement-new object lifecycle management.
 *   - Compile-time triviality acceleration via duo::is_trivially_copyable<T>.
 *   - Move semantics with O(1) zero-allocation ownership transfer.
 *   - Bidirectional C interop: adopt C-allocated buffers, release/disown to C without copying.
 *   - Non-owning borrowing as Span<T> or SpanView<T>.
 *   - Standard accessors, iterators, and bulk algorithms.
 *
 * @tparam T Element type.
 */
template <typename T>
class Vector {
public:
    using value_type      = T;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using c_type          = c_vector_t<T>;
    using self_type       = Vector;



    c_type m_inner; /**< Standard-layout C mirror struct: { size_t size; size_t capacity; T* data; } */

    /** @brief Constructs an empty vector (size = 0, capacity = 0, data = nullptr). Zero allocations. */
    inline constexpr Vector() noexcept : m_inner{ 0, 0, nullptr } {}

    /**
     * @brief Constructs a vector with @p n default-initialized elements.
     * @param n Number of elements to allocate.
     */
    explicit inline Vector(size_t n) : m_inner{ 0, 0, nullptr } {
        if (n > 0 && reserve(n)) {
            m_inner.size = n;
            if constexpr (duo::is_trivially_copyable<T>::value) {
                DUO_MEMSET(m_inner.data, 0, n * sizeof(T));
            } else {
                for (size_t i = 0; i < n; ++i) {
                    duo::construct_at(&m_inner.data[i]);
                }
            }
        }
    }

    /**
     * @brief Constructs a vector of @p n elements, each initialized with a copy of @p val.
     * @param n Number of elements to allocate.
     * @param val Initial value to fill into each element.
     */
    inline Vector(size_t n, const T& val) : m_inner{ 0, 0, nullptr } {
        if (n > 0 && reserve(n)) {
            m_inner.size = n;
            if constexpr (duo::is_trivially_copyable<T>::value) {
                for (size_t i = 0; i < n; ++i) {
                    m_inner.data[i] = val;
                }
            } else {
                for (size_t i = 0; i < n; ++i) {
                    duo::construct_at(&m_inner.data[i], val);
                }
            }
        }
    }

    /**
     * @brief Constructs a vector by copying @p n elements from raw contiguous buffer @p src.
     * @param src Pointer to source buffer.
     * @param n Number of elements to copy.
     */
    inline Vector(const T* src, size_t n) : m_inner{ 0, 0, nullptr } {
        if (n > 0 && src && reserve(n)) {
            m_inner.size = n;
            duo::uninitialized_copy_n(src, n, m_inner.data);
        }
    }

    /** @brief Constructs a vector by copying elements from an immutable SpanView. */
    inline explicit Vector(SpanView<T> view) : Vector(view.data(), view.size()) {}

    /** @brief Constructs a vector by copying elements from a mutable Span. */
    inline explicit Vector(Span<T> s) : Vector(s.data(), s.size()) {}

    /** @brief Constructs a vector from a compile-time fixed array reference. */
    template <size_t N>
    inline explicit Vector(const T (&arr)[N]) : Vector(arr, N) {}

    /**
     * @brief Constructs a vector from a C mirror struct by copying its elements.
     * For zero-copy ownership transfer from C, use Vector::adopt(c).
     */
    inline explicit Vector(const c_type& inner) : m_inner{ 0, 0, nullptr } {
        if (inner.size > 0 && inner.data && reserve(inner.capacity >= inner.size ? inner.capacity : inner.size)) {
            m_inner.size = inner.size;
            duo::uninitialized_copy_n(inner.data, inner.size, m_inner.data);
        }
    }

    /**
     * @brief Constructs a vector from any external C-style typed vector struct having .data, .size, and .capacity.
     */
    template <typename CVec,
              typename = decltype(static_cast<T*>(static_cast<CVec*>(nullptr)->data)),
              typename = decltype(static_cast<size_t>(static_cast<CVec*>(nullptr)->size)),
              typename = decltype(static_cast<size_t>(static_cast<CVec*>(nullptr)->capacity))>
    inline explicit Vector(const CVec& c) : m_inner{ 0, 0, nullptr } {
        if (c.size > 0 && c.data && reserve(c.capacity >= c.size ? c.capacity : c.size)) {
            m_inner.size = c.size;
            duo::uninitialized_copy_n(c.data, c.size, m_inner.data);
        }
    }

    /** @brief Destructor releasing heap allocation and destroying non-trivial elements. */
    inline ~Vector() noexcept {
        reset();
    }

    /** @brief Deep copy constructor. */
    inline Vector(const Vector& other) : Vector(other.data(), other.size()) {}

    /** @brief Move constructor transferring ownership without allocation or element moves. */
    inline Vector(Vector&& other) noexcept : m_inner{ other.m_inner.size, other.m_inner.capacity, other.m_inner.data } {
        other.m_inner.size = 0;
        other.m_inner.capacity = 0;
        other.m_inner.data = nullptr;
    }

    /** @brief Deep copy assignment operator. */
    inline Vector& operator=(const Vector& other) {
        if (this != &other) {
            clear();
            if (other.m_inner.size > 0 && other.m_inner.data) {
                if (reserve(other.m_inner.size)) {
                    m_inner.size = other.m_inner.size;
                    duo::uninitialized_copy_n(other.m_inner.data, other.m_inner.size, m_inner.data);
                }
            }
        }
        return *this;
    }

    /** @brief Move assignment operator transferring buffer ownership. */
    inline Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            reset();
            m_inner = other.m_inner;
            other.m_inner.size = 0;
            other.m_inner.capacity = 0;
            other.m_inner.data = nullptr;
        }
        return *this;
    }

    /** @brief Returns total number of elements that can be stored without reallocating. */
    inline size_t capacity() const noexcept { return m_inner.capacity; }

    /**
     * @brief Ensures capacity is at least @p new_cap.
     * @param new_cap Minimum requested capacity.
     * @return true on success, false on allocation failure.
     */
    inline bool reserve(size_t new_cap) {
        if (new_cap <= m_inner.capacity) {
            return true;
        }
        if (sizeof(T) > 0 && new_cap > static_cast<size_t>(-1) / sizeof(T)) {
            return false;
        }
        if constexpr (duo::is_trivially_copyable<T>::value) {
            void* new_buf = duo::reallocate(m_inner.data, new_cap * sizeof(T));
            if (!new_buf) {
                return false;
            }
            m_inner.data = static_cast<T*>(new_buf);
            m_inner.capacity = new_cap;
            return true;
        } else {
            void* mem = duo::allocate(new_cap * sizeof(T));
            if (!mem) {
                return false;
            }
            T* new_data = static_cast<T*>(mem);
            duo::relocate_n(new_data, m_inner.data, m_inner.size);
            if (m_inner.data) {
                duo::deallocate(m_inner.data);
            }
            m_inner.data = new_data;
            m_inner.capacity = new_cap;
            return true;
        }
    }

    /**
     * @brief Internal geometric growth helper ensuring capacity for at least @p min_cap elements.
     * Uses 3-tier adaptive geometric growth via duo_geometric_grow_cap (<256: 2x, <4096: 1.5x, >=4096: 1.25x).
     */
    inline bool grow_for(size_t min_cap) {
        if (min_cap <= m_inner.capacity) {
            return true;
        }
        size_t next_cap = duo_geometric_grow_cap(m_inner.capacity);
        size_t new_cap = (next_cap > min_cap) ? next_cap : min_cap;
        return reserve(new_cap);
    }

    /**
     * @brief Shrinks capacity to match current size. Deallocates buffer if size is 0.
     * @return true on success, false on allocation failure.
     */
    inline bool shrink_to_fit() {
        if (m_inner.capacity == m_inner.size) {
            return true;
        }
        if (m_inner.size == 0) {
            if (m_inner.data) {
                duo::deallocate(m_inner.data);
                m_inner.data = nullptr;
            }
            m_inner.capacity = 0;
            return true;
        }
        if constexpr (duo::is_trivially_copyable<T>::value) {
            void* new_buf = duo::reallocate(m_inner.data, m_inner.size * sizeof(T));
            if (!new_buf) {
                return false;
            }
            m_inner.data = static_cast<T*>(new_buf);
            m_inner.capacity = m_inner.size;
            return true;
        } else {
            void* mem = duo::allocate(m_inner.size * sizeof(T));
            if (!mem) {
                return false;
            }
            T* new_data = static_cast<T*>(mem);
            duo::relocate_n(new_data, m_inner.data, m_inner.size);
            duo::deallocate(m_inner.data);
            m_inner.data = new_data;
            m_inner.capacity = m_inner.size;
            return true;
        }
    }

    /** @brief Destroys all active elements without freeing allocated capacity. */
    inline void clear() noexcept {
        const size_t sz = m_inner.size;
        m_inner.size = 0;
        if (m_inner.data && sz > 0) {
            if constexpr (!duo::is_trivially_destructible<T>::value) {
                duo::destroy_n(m_inner.data, sz);
            }
        }
    }

    /** @brief Destroys all elements, frees allocated heap memory, and resets to empty state. */
    inline void reset() noexcept {
        clear();
        if (m_inner.data) {
            duo::deallocate(m_inner.data);
            m_inner.data = nullptr;
        }
        m_inner.capacity = 0;
    }

    /** @brief Appends an element by copy. */
    inline bool push_back(const T& val) {
        if (!grow_for(m_inner.size + 1)) {
            return false;
        }
        duo::construct_at(&m_inner.data[m_inner.size], val);
        ++m_inner.size;
        return true;
    }

    /** @brief Appends an element by move. */
    inline bool push_back(T&& val) {
        if (!grow_for(m_inner.size + 1)) {
            return false;
        }
        duo::construct_at(&m_inner.data[m_inner.size], duo::move(val));
        ++m_inner.size;
        return true;
    }

    /** @brief Constructs an element in-place at the end. */
    template <typename... Args>
    inline T& emplace_back(Args&&... args) {
        grow_for(m_inner.size + 1);
        T* p = &m_inner.data[m_inner.size];
        duo::construct_at(p, duo::forward<Args>(args)...);
        ++m_inner.size;
        return *p;
    }

    /** @brief Removes the last element. Safe on empty vector. */
    inline void pop_back() noexcept {
        if (m_inner.size > 0) {
            --m_inner.size;
            if constexpr (!duo::is_trivially_destructible<T>::value) {
                duo::destroy_at(&m_inner.data[m_inner.size]);
            }
        }
    }

    /** @brief Inserts an element at @p index by copy. Shifts subsequent elements right. */
    inline bool insert(size_t index, const T& val) {
        if (index > m_inner.size) {
            return false;
        }
        if (!grow_for(m_inner.size + 1)) {
            return false;
        }
        if (index < m_inner.size) {
            duo::relocate_n(&m_inner.data[index + 1], &m_inner.data[index], m_inner.size - index);
        }
        duo::construct_at(&m_inner.data[index], val);
        ++m_inner.size;
        return true;
    }

    /** @brief Inserts an element at @p index by move. Shifts subsequent elements right. */
    inline bool insert(size_t index, T&& val) {
        if (index > m_inner.size) {
            return false;
        }
        if (!grow_for(m_inner.size + 1)) {
            return false;
        }
        if (index < m_inner.size) {
            duo::relocate_n(&m_inner.data[index + 1], &m_inner.data[index], m_inner.size - index);
        }
        duo::construct_at(&m_inner.data[index], duo::move(val));
        ++m_inner.size;
        return true;
    }

    /** @brief Constructs an element in-place at @p index. Shifts subsequent elements right. */
    template <typename... Args>
    inline T* emplace(size_t index, Args&&... args) {
        if (index > m_inner.size) {
            return nullptr;
        }
        if (!grow_for(m_inner.size + 1)) {
            return nullptr;
        }
        if (index < m_inner.size) {
            duo::relocate_n(&m_inner.data[index + 1], &m_inner.data[index], m_inner.size - index);
        }
        T* p = &m_inner.data[index];
        duo::construct_at(p, duo::forward<Args>(args)...);
        ++m_inner.size;
        return p;
    }

    /** @brief Erases element at @p index. Shifts subsequent elements left. */
    inline bool erase(size_t index) noexcept {
        if (index >= m_inner.size) {
            return false;
        }
        if constexpr (!duo::is_trivially_destructible<T>::value) {
            duo::destroy_at(&m_inner.data[index]);
        }
        if (index + 1 < m_inner.size) {
            duo::relocate_n(&m_inner.data[index], &m_inner.data[index + 1], m_inner.size - 1 - index);
        }
        --m_inner.size;
        return true;
    }

    /** @brief Erases range of elements [first, last). Shifts subsequent elements left. */
    inline bool erase(size_t first, size_t last) noexcept {
        if (first > last || last > m_inner.size) {
            return false;
        }
        size_t count = last - first;
        if (count == 0) {
            return true;
        }
        if constexpr (!duo::is_trivially_destructible<T>::value) {
            duo::destroy_n(&m_inner.data[first], count);
        }
        if (last < m_inner.size) {
            duo::relocate_n(&m_inner.data[first], &m_inner.data[last], m_inner.size - last);
        }
        m_inner.size -= count;
        return true;
    }

    /** @brief Resizes vector to @p new_size, zero-initializing or default-constructing new elements. */
    inline bool resize(size_t new_size) {
        if (new_size < m_inner.size) {
            if constexpr (!duo::is_trivially_destructible<T>::value) {
                duo::destroy_n(&m_inner.data[new_size], m_inner.size - new_size);
            }
            m_inner.size = new_size;
            return true;
        }
        if (new_size > m_inner.size) {
            if (!grow_for(new_size)) {
                return false;
            }
            if constexpr (duo::is_trivially_copyable<T>::value) {
                DUO_MEMSET(&m_inner.data[m_inner.size], 0, (new_size - m_inner.size) * sizeof(T));
            } else {
                for (size_t i = m_inner.size; i < new_size; ++i) {
                    duo::construct_at(&m_inner.data[i]);
                }
            }
            m_inner.size = new_size;
        }
        return true;
    }

    /** @brief Resizes vector to @p new_size, initializing new elements with @p val. */
    inline bool resize(size_t new_size, const T& val) {
        if (new_size < m_inner.size) {
            if constexpr (!duo::is_trivially_destructible<T>::value) {
                duo::destroy_n(&m_inner.data[new_size], m_inner.size - new_size);
            }
            m_inner.size = new_size;
            return true;
        }
        if (new_size > m_inner.size) {
            if (!grow_for(new_size)) {
                return false;
            }
            if constexpr (duo::is_trivially_copyable<T>::value) {
                for (size_t i = m_inner.size; i < new_size; ++i) {
                    m_inner.data[i] = val;
                }
            } else {
                for (size_t i = m_inner.size; i < new_size; ++i) {
                    duo::construct_at(&m_inner.data[i], val);
                }
            }
            m_inner.size = new_size;
        }
        return true;
    }

    /** @brief Swaps contents with another vector in O(1) time without allocations. */
    inline void swap(Vector& other) noexcept {
        duo::swap(m_inner.size, other.m_inner.size);
        duo::swap(m_inner.capacity, other.m_inner.capacity);
        duo::swap(m_inner.data, other.m_inner.data);
    }

    DUO_CXX_SPAN_BORROW_OPS(T, m_inner.data, m_inner.size)
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_STANDARD_ACCESSORS(T, m_inner.data, m_inner.size)
};

/**
 * @brief Non-member swap overload for duo::Vector<T>.
 */
template <typename T>
inline void swap(Vector<T>& a, Vector<T>& b) noexcept {
    a.swap(b);
}

// Static ABI assertions for Vector
static_assert(sizeof(c_vector_t<int>) == 24, "c_vector_t<T> must be exactly 24 bytes!");
static_assert(sizeof(Vector<int>) == 24, "Vector<T> must be exactly 24 bytes!");
static_assert(is_standard_layout<c_vector_t<int>>::value, "c_vector_t<T> must be standard layout!");
static_assert(is_standard_layout<Vector<int>>::value, "Vector<T> must be standard layout!");
static_assert(is_trivially_copyable<c_vector_t<int>>::value, "c_vector_t<T> must be trivially copyable!");

// ============================================================================
// 4.5 C++17 STACK / FIXED-CAPACITY VECTOR: duo::FixedVector<T> & c_fixed_vec_t<T>
// ============================================================================

/**
 * @brief Standard-layout C mirror struct alias for fixed stack vector.
 * Binary-identical to c_vector_t<T> ({ size_t size; size_t capacity; T* data; }, 24 bytes).
 */
template <typename T>
using c_fixed_vec_t = c_vector_t<T>;

/**
 * @class FixedVector
 * @brief Freestanding fixed-capacity non-reallocating vector for C++17.
 *
 * Wraps a standard-layout C mirror struct `m_inner` of type c_fixed_vec_t<T>
 * ({ size_t size; size_t capacity; T* data; }, exactly 24 bytes on 64-bit platforms).
 *
 * Features:
 *   - Freestanding (-nostdlib++), zero-exception, zero-RTTI.
 *   - Zero heap allocations: operates strictly within a pre-allocated stack or external buffer.
 *   - Automatic RAII element destruction on pop, erase, clear, and container destruction,
 *     WITHOUT freeing the underlying buffer (safe for stack allocations).
 *   - Capacity overflow protection: push_back, emplace_back, insert, and resize
 *     safely reject insertions and return false when capacity is reached.
 *   - Non-owning borrowing as Span<T> or SpanView<T>.
 *   - Injected accessors, iterators, and bulk data operations.
 *
 * @tparam T Element type.
 */
template <typename T>
class FixedVector {
public:
    using value_type      = T;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using c_type          = c_fixed_vec_t<T>;
    using self_type       = FixedVector;



    c_type m_inner; /**< Standard-layout C mirror struct: { size_t size; size_t capacity; T* data; } */

    /** @brief Constructs an empty fixed vector (size = 0, capacity = 0, data = nullptr). */
    inline constexpr FixedVector() noexcept : m_inner{ 0, 0, nullptr } {}

    /**
     * @brief Constructs a fixed vector binding an external contiguous buffer with given capacity.
     * Initial size is 0.
     * @param buf Pointer to contiguous buffer.
     * @param cap Maximum capacity of the buffer.
     */
    inline constexpr FixedVector(T* buf, size_t cap) noexcept : m_inner{ 0, cap, buf } {}

    /**
     * @brief Constructs a fixed vector binding an external buffer with pre-existing elements.
     * @param buf Pointer to contiguous buffer.
     * @param sz Initial number of active elements in buffer.
     * @param cap Maximum capacity of the buffer.
     */
    inline constexpr FixedVector(T* buf, size_t sz, size_t cap) noexcept : m_inner{ sz, cap, buf } {}

    /**
     * @brief Constructs a fixed vector directly from its C mirror struct.
     * @param inner The standard-layout c_vector_t<T> struct.
     */
    inline explicit FixedVector(c_type inner) noexcept : m_inner(inner) {}

    /** @brief Destructor destroying non-trivial elements WITHOUT freeing buffer storage. */
    inline ~FixedVector() noexcept {
        clear();
    }

    /** @brief Move constructor stealing buffer reference and resetting other. */
    inline FixedVector(FixedVector&& other) noexcept : m_inner(other.m_inner) {
        other.m_inner.size = 0;
        other.m_inner.capacity = 0;
        other.m_inner.data = nullptr;
    }

    /** @brief Move assignment operator destroying current elements and stealing buffer. */
    inline FixedVector& operator=(FixedVector&& other) noexcept {
        if (this != &other) {
            clear();
            m_inner = other.m_inner;
            other.m_inner.size = 0;
            other.m_inner.capacity = 0;
            other.m_inner.data = nullptr;
        }
        return *this;
    }

    FixedVector(const FixedVector&) = delete;
    FixedVector& operator=(const FixedVector&) = delete;

    /** @brief Returns total number of elements that can be stored without growing. */
    inline size_t capacity() const noexcept { return m_inner.capacity; }

    /** @brief Returns true if size has reached maximum capacity. */
    inline bool full() const noexcept { return m_inner.size >= m_inner.capacity; }

    /** @brief Destroys all active elements without touching buffer storage. */
    inline void clear() noexcept {
        const size_t sz = m_inner.size;
        m_inner.size = 0;
        if (m_inner.data && sz > 0) {
            if constexpr (!duo::is_trivially_destructible<T>::value) {
                duo::destroy_n(m_inner.data, sz);
            }
        }
    }

    /** @brief Appends an element by copy. Returns false if at capacity. */
    inline bool push_back(const T& val) {
        if (m_inner.size >= m_inner.capacity) {
            return false;
        }
        duo::construct_at(&m_inner.data[m_inner.size], val);
        ++m_inner.size;
        return true;
    }

    /** @brief Appends an element by move. Returns false if at capacity. */
    inline bool push_back(T&& val) {
        if (m_inner.size >= m_inner.capacity) {
            return false;
        }
        duo::construct_at(&m_inner.data[m_inner.size], duo::move(val));
        ++m_inner.size;
        return true;
    }

    /** @brief Constructs an element in-place at the end. Returns nullptr if at capacity. */
    template <typename... Args>
    inline T* emplace_back(Args&&... args) {
        if (m_inner.size >= m_inner.capacity) {
            return nullptr;
        }
        T* p = &m_inner.data[m_inner.size];
        duo::construct_at(p, duo::forward<Args>(args)...);
        ++m_inner.size;
        return p;
    }

    /** @brief Removes the last element, invoking its destructor. Returns false if empty. */
    inline bool pop_back() noexcept {
        if (m_inner.size == 0) {
            return false;
        }
        --m_inner.size;
        if constexpr (!duo::is_trivially_destructible<T>::value) {
            duo::destroy_at(&m_inner.data[m_inner.size]);
        }
        return true;
    }

    /** @brief Inserts an element by copy at @p index. Returns false if full or index > size. */
    inline bool insert(size_t index, const T& val) {
        if (index > m_inner.size || m_inner.size >= m_inner.capacity) {
            return false;
        }
        if (index < m_inner.size) {
            duo::relocate_n(&m_inner.data[index + 1], &m_inner.data[index], m_inner.size - index);
        }
        duo::construct_at(&m_inner.data[index], val);
        ++m_inner.size;
        return true;
    }

    /** @brief Inserts an element by move at @p index. Returns false if full or index > size. */
    inline bool insert(size_t index, T&& val) {
        if (index > m_inner.size || m_inner.size >= m_inner.capacity) {
            return false;
        }
        if (index < m_inner.size) {
            duo::relocate_n(&m_inner.data[index + 1], &m_inner.data[index], m_inner.size - index);
        }
        duo::construct_at(&m_inner.data[index], duo::move(val));
        ++m_inner.size;
        return true;
    }

    /** @brief Erases the element at @p index. Returns false if index >= size. */
    inline bool erase(size_t index) noexcept {
        if (index >= m_inner.size) {
            return false;
        }
        if constexpr (!duo::is_trivially_destructible<T>::value) {
            duo::destroy_at(&m_inner.data[index]);
        }
        if (index + 1 < m_inner.size) {
            duo::relocate_n(&m_inner.data[index], &m_inner.data[index + 1], m_inner.size - 1 - index);
        }
        --m_inner.size;
        return true;
    }

    /** @brief Erases a contiguous range [first, first + count). Returns false if first > size. */
    inline bool erase(size_t first, size_t count) noexcept {
        if (first > m_inner.size) {
            return false;
        }
        if (first + count > m_inner.size) {
            count = m_inner.size - first;
        }
        if (count == 0) {
            return true;
        }
        if constexpr (!duo::is_trivially_destructible<T>::value) {
            duo::destroy_n(&m_inner.data[first], count);
        }
        if (first + count < m_inner.size) {
            duo::relocate_n(&m_inner.data[first], &m_inner.data[first + count], m_inner.size - (first + count));
        }
        m_inner.size -= count;
        return true;
    }

    /** @brief Resizes to @p new_size (default-initialized). Returns false if new_size > capacity. */
    inline bool resize(size_t new_size) {
        if (new_size > m_inner.capacity) {
            return false;
        }
        if (new_size < m_inner.size) {
            if constexpr (!duo::is_trivially_destructible<T>::value) {
                duo::destroy_n(&m_inner.data[new_size], m_inner.size - new_size);
            }
            m_inner.size = new_size;
            return true;
        }
        if (new_size > m_inner.size) {
            if constexpr (duo::is_trivially_copyable<T>::value) {
                DUO_MEMSET(&m_inner.data[m_inner.size], 0, (new_size - m_inner.size) * sizeof(T));
            } else {
                for (size_t i = m_inner.size; i < new_size; ++i) {
                    duo::construct_at(&m_inner.data[i]);
                }
            }
            m_inner.size = new_size;
        }
        return true;
    }

    /** @brief Resizes to @p new_size filled with @p val. Returns false if new_size > capacity. */
    inline bool resize(size_t new_size, const T& val) {
        if (new_size > m_inner.capacity) {
            return false;
        }
        if (new_size < m_inner.size) {
            if constexpr (!duo::is_trivially_destructible<T>::value) {
                duo::destroy_n(&m_inner.data[new_size], m_inner.size - new_size);
            }
            m_inner.size = new_size;
            return true;
        }
        if (new_size > m_inner.size) {
            for (size_t i = m_inner.size; i < new_size; ++i) {
                duo::construct_at(&m_inner.data[i], val);
            }
            m_inner.size = new_size;
        }
        return true;
    }

    /** @brief Swaps contents and buffer pointers with another fixed vector. */
    inline void swap(FixedVector& other) noexcept {
        duo::swap(m_inner.size, other.m_inner.size);
        duo::swap(m_inner.capacity, other.m_inner.capacity);
        duo::swap(m_inner.data, other.m_inner.data);
    }

    DUO_CXX_SPAN_BORROW_OPS(T, m_inner.data, m_inner.size)
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_STANDARD_ACCESSORS(T, m_inner.data, m_inner.size)
};

/**
 * @brief Non-member swap overload for duo::FixedVector<T>.
 */
template <typename T>
inline void swap(FixedVector<T>& a, FixedVector<T>& b) noexcept {
    a.swap(b);
}

// Static ABI assertions for FixedVector
static_assert(sizeof(c_fixed_vec_t<int>) == 24, "c_fixed_vec_t<T> must be exactly 24 bytes!");
static_assert(sizeof(FixedVector<int>) == 24, "FixedVector<T> must be exactly 24 bytes!");
static_assert(is_standard_layout<c_fixed_vec_t<int>>::value, "c_fixed_vec_t<T> must be standard layout!");
static_assert(is_standard_layout<FixedVector<int>>::value, "FixedVector<T> must be standard layout!");
static_assert(is_trivially_copyable<c_fixed_vec_t<int>>::value, "c_fixed_vec_t<T> must be trivially copyable!");

// ============================================================================
// 5. C++17 HEAP-ALLOCATED FIXED ARRAY: duo::Array<T> & duo::c_array_t<T>
// ============================================================================

/**
 * @struct c_array_t
 * @brief Standard-layout C mirror struct representing a heap-allocated fixed array.
 * Exactly 16 bytes on 64-bit platforms, ABI-identical to pure C DUO_C_ARRAY_TYPE.
 * Member order is { size_t size; T* data; }.
 *
 * @tparam T Element type.
 */
template <typename T>
struct c_array_t {
    size_t size; /**< Number of elements in the array. */
    T*     data; /**< Pointer to contiguous heap buffer. */

    DUO_CXX_C_STRUCT_SPAN_CONVERSIONS(T, data, size)
};

/**
 * @class Array
 * @brief Freestanding RAII owning heap-allocated fixed array for C++17.
 *
 * Wraps a standard-layout C mirror struct `m_inner` of type c_array_t<T>
 * ({ size_t size; T* data; }, exactly 16 bytes on 64-bit platforms).
 *
 * Features:
 *   - Freestanding (-nostdlib++), zero-exception, zero-RTTI.
 *   - RAII resource management via duo::allocate and duo::deallocate.
 *   - Move semantics with O(1) zero-allocation ownership transfer.
 *   - Compile-time triviality optimization via duo::is_trivially_copyable<T>.
 *   - Non-owning borrowing as Span<T> or SpanView<T>.
 *   - Injected accessors, iterators, and bulk data operations.
 *
 * @tparam T Element type.
 */
template <typename T>
class Array {
public:
    using value_type      = T;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using c_type          = c_array_t<T>;
    using self_type       = Array;



    c_type m_inner; /**< Standard-layout C mirror struct: { size_t size; T* data; } */

    /** @brief Constructs an empty array (size = 0, data = nullptr). */
    inline constexpr Array() noexcept : m_inner{ 0, nullptr } {}

    /**
     * @brief Constructs an array of @p n default/value-initialized elements.
     * @param n Number of elements to allocate.
     */
    explicit inline Array(size_t n) : m_inner{ 0, nullptr } {
        if (n > 0) {
            void* mem = duo::allocate(sizeof(T) * n);
            if (mem) {
                m_inner.size = n;
                m_inner.data = static_cast<T*>(mem);
                if constexpr (duo::is_trivially_copyable<T>::value) {
                    DUO_MEMSET(m_inner.data, 0, n * sizeof(T));
                } else {
                    for (size_t i = 0; i < n; ++i) {
                        duo::construct_at(&m_inner.data[i]);
                    }
                }
            }
        }
    }

    /**
     * @brief Constructs an array of @p n elements, each initialized with a copy of @p val.
     * @param n Number of elements to allocate.
     * @param val Initial value to fill into each element.
     */
    inline Array(size_t n, const T& val) : m_inner{ 0, nullptr } {
        if (n > 0) {
            void* mem = duo::allocate(sizeof(T) * n);
            if (mem) {
                m_inner.size = n;
                m_inner.data = static_cast<T*>(mem);
                if constexpr (duo::is_trivially_copyable<T>::value) {
                    for (size_t i = 0; i < n; ++i) {
                        m_inner.data[i] = val;
                    }
                } else {
                    for (size_t i = 0; i < n; ++i) {
                        duo::construct_at(&m_inner.data[i], val);
                    }
                }
            }
        }
    }

    /**
     * @brief Constructs an array by copying @p n elements from raw contiguous buffer @p src.
     * @param src Pointer to source buffer.
     * @param n Number of elements to copy.
     */
    inline Array(const T* src, size_t n) : m_inner{ 0, nullptr } {
        if (n > 0 && src) {
            void* mem = duo::allocate(sizeof(T) * n);
            if (mem) {
                m_inner.size = n;
                m_inner.data = static_cast<T*>(mem);
                duo::uninitialized_copy_n(src, n, m_inner.data);
            }
        }
    }

    /** @brief Constructs an array by copying elements from an immutable SpanView. */
    inline explicit Array(SpanView<T> view) : Array(view.data(), view.size()) {}

    /** @brief Constructs an array by copying elements from a mutable Span. */
    inline explicit Array(Span<T> s) : Array(s.data(), s.size()) {}

    /**
     * @brief Constructs an array by copying from any external C-style typed array struct having .data and .size.
     */
    template <typename CArr,
              typename = decltype(static_cast<T*>(static_cast<CArr*>(nullptr)->data)),
              typename = decltype(static_cast<size_t>(static_cast<CArr*>(nullptr)->size))>
    inline explicit Array(const CArr& c) : Array(c.data, c.size) {}

    /** @brief Destructor releasing heap allocation and destroying non-trivial elements. */
    inline ~Array() noexcept {
        reset();
    }

    /** @brief Deep copy constructor. */
    inline Array(const Array& other) : Array(other.data(), other.size()) {}

    /** @brief Move constructor transferring ownership without allocation or element moves. */
    inline Array(Array&& other) noexcept : m_inner{ other.m_inner.size, other.m_inner.data } {
        other.m_inner.size = 0;
        other.m_inner.data = nullptr;
    }

    /** @brief Deep copy assignment operator. */
    inline Array& operator=(const Array& other) {
        if (this != &other) {
            reset();
            if (other.m_inner.size > 0 && other.m_inner.data) {
                void* mem = duo::allocate(sizeof(T) * other.m_inner.size);
                if (mem) {
                    m_inner.size = other.m_inner.size;
                    m_inner.data = static_cast<T*>(mem);
                    duo::uninitialized_copy_n(other.m_inner.data, other.m_inner.size, m_inner.data);
                }
            }
        }
        return *this;
    }

    /** @brief Move assignment operator transferring buffer ownership. */
    inline Array& operator=(Array&& other) noexcept {
        if (this != &other) {
            reset();
            m_inner = other.m_inner;
            other.m_inner.size = 0;
            other.m_inner.data = nullptr;
        }
        return *this;
    }

    /** @brief Destroys all elements, frees allocated heap memory, and resets to empty state. */
    inline void reset() noexcept {
        if (m_inner.data) {
            if constexpr (!duo::is_trivially_destructible<T>::value) {
                duo::destroy_n(m_inner.data, m_inner.size);
            }
            duo::deallocate(m_inner.data);
            m_inner.data = nullptr;
            m_inner.size = 0;
        }
    }

    /** @brief Swaps contents with another array in O(1) time without heap allocations. */
    inline void swap(Array& other) noexcept {
        duo::swap(m_inner.size, other.m_inner.size);
        duo::swap(m_inner.data, other.m_inner.data);
    }

    DUO_CXX_SPAN_BORROW_OPS(T, m_inner.data, m_inner.size)
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_STANDARD_ACCESSORS(T, m_inner.data, m_inner.size)
};

/**
 * @brief Non-member swap overload for duo::Array<T>.
 */
template <typename T>
inline void swap(Array<T>& a, Array<T>& b) noexcept {
    a.swap(b);
}

// ============================================================================
// 5. SEMANTIC ALIASES: FixedArray
// ============================================================================

/**
 * @brief Semantic alias for duo::Span<T> representing a fixed-capacity contiguous buffer.
 * Typically used over stack buffers or fixed pre-allocated blocks.
 */
template <typename T>
using FixedArray = Span<T>;

// Static ABI assertions for Array
static_assert(sizeof(c_array_t<int>) == 16, "c_array_t<T> must be exactly 16 bytes!");
static_assert(sizeof(Array<int>) == 16, "Array<T> must be exactly 16 bytes!");
static_assert(is_standard_layout<c_array_t<int>>::value, "c_array_t<T> must be standard layout!");
static_assert(is_standard_layout<Array<int>>::value, "Array<T> must be standard layout!");
static_assert(is_trivially_copyable<c_array_t<int>>::value, "c_array_t<T> must be trivially copyable!");

// ============================================================================
// 6. C++17 SBO BYTES CONTAINER: duo::Bytes
// ============================================================================

/**
 * @class Bytes
 * @brief Freestanding Small Buffer Optimized (SBO) binary byte buffer for C++17.
 *
 * Encapsulates a standard-layout C mirror struct `m_inner` of type duo_bytes_t,
 * occupying exactly 24 bytes on 64-bit platforms.
 *
 * Features:
 *   - Freestanding (-nostdlib++), zero-exception, zero-RTTI.
 *   - SBO storage: stores up to 23 bytes inline on the struct with zero dynamic memory allocation.
 *   - Seamless promotion: automatically promotes to dynamic heap allocation with geometric
 *     doubling upon exceeding 23 bytes.
 *   - Dynamic demotion: shrink_to_fit() demotes heap storage back to inline SBO when size <= 23.
 *   - Zero-copy C FFI: c_ptr(), c_val(), adopt(), disown(), release_c(), and implicit conversion operators.
 *   - Non-owning borrowing as Span<uint8_t> and SpanView<uint8_t>.
 *   - STL iterator suites, element accessors, and bulk data operations.
 */
class Bytes {
public:
    using value_type      = uint8_t;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using reference       = uint8_t&;
    using const_reference = const uint8_t&;
    using pointer         = uint8_t*;
    using const_pointer   = const uint8_t*;
    using c_type          = duo_bytes_t;
    using self_type       = Bytes;

    c_type m_inner; /**< Standard-layout C mirror struct: exactly 24 bytes on 64-bit platforms. */

    /** @brief Constructs an empty byte buffer in inline SBO mode (size = 0, capacity = 23). */
    inline Bytes() noexcept {
        duo_bytes_init(&m_inner);
    }

    /**
     * @brief Constructs a byte buffer by copying @p len bytes from @p data.
     */
    inline Bytes(const void* data, size_t len) noexcept {
        duo_bytes_init(&m_inner);
        if (data && len > 0) {
            if (len <= DUO_SBO_BYTES_INLINE_CAP) {
                DUO_MEMCPY(m_inner.m_sbo.m_sbo, data, len);
                m_inner.m_sbo.tag.length = static_cast<uint8_t>(len);
            } else {
                duo_bytes_append(&m_inner, data, len);
            }
        }
    }

    /**
     * @brief Constructs a byte buffer from a SpanView of bytes.
     */
    inline Bytes(SpanView<uint8_t> view) noexcept : Bytes(view.data(), view.size()) {}

    /**
     * @brief Constructs a byte buffer from a null-terminated string.
     */
    inline explicit Bytes(const char* str) noexcept {
        duo_bytes_init(&m_inner);
        if (str) {
            size_t len = 0;
            while (str[len] != '\0') {
                ++len;
            }
            if (len > 0) {
                if (len <= DUO_SBO_BYTES_INLINE_CAP) {
                    DUO_MEMCPY(m_inner.m_sbo.m_sbo, str, len);
                    m_inner.m_sbo.tag.length = static_cast<uint8_t>(len);
                } else {
                    duo_bytes_append(&m_inner, str, len);
                }
            }
        }
    }

    /**
     * @brief Constructs a byte buffer filled with @p count copies of @p val.
     */
    inline Bytes(size_t count, uint8_t val) noexcept {
        duo_bytes_init(&m_inner);
        if (count > 0 && duo_bytes_reserve(&m_inner, count)) {
            uint8_t* ptr = duo_bytes_data(&m_inner);
            DUO_MEMSET(ptr, val, count);
            if (duo_bytes_is_sbo(&m_inner)) {
                m_inner.m_sbo.tag.length = static_cast<uint8_t>(count);
            } else {
                m_inner.m_heap.m_size = count;
            }
        }
    }

    /** @brief Destroys the container, releasing dynamic heap memory if allocated. */
    inline ~Bytes() noexcept {
        duo_bytes_destroy(&m_inner);
    }

    /** @brief Copy constructor: deep-copies bytes from @p other. */
    inline Bytes(const Bytes& other) noexcept {
        duo_bytes_init(&m_inner);
        size_t sz = other.size();
        if (sz > 0) {
            duo_bytes_append(&m_inner, other.data(), sz);
        }
    }

    /** @brief Copy assignment operator: deep-copies bytes from @p other. */
    inline Bytes& operator=(const Bytes& other) noexcept {
        if (this != &other) {
            duo_bytes_destroy(&m_inner);
            size_t sz = other.size();
            if (sz > 0) {
                duo_bytes_append(&m_inner, other.data(), sz);
            }
        }
        return *this;
    }

    /** @brief Move constructor: steals ownership from @p other, resetting @p other to empty SBO. */
    inline Bytes(Bytes&& other) noexcept {
        DUO_MEMCPY(&m_inner, &other.m_inner, sizeof(m_inner));
        duo_bytes_init(&other.m_inner);
    }

    /** @brief Move assignment operator: steals ownership from @p other. */
    inline Bytes& operator=(Bytes&& other) noexcept {
        if (this != &other) {
            duo_bytes_destroy(&m_inner);
            DUO_MEMCPY(&m_inner, &other.m_inner, sizeof(m_inner));
            duo_bytes_init(&other.m_inner);
        }
        return *this;
    }

    /** @brief Returns maximum byte capacity before next reallocation. */
    inline size_t capacity() const noexcept {
        return duo_bytes_capacity(&m_inner);
    }

    /** @brief Semantic alias for size(). */
    inline size_t length() const noexcept {
        return size();
    }

    /** @brief Returns true if bytes are currently stored inline via SBO. */
    inline bool is_sbo() const noexcept {
        return duo_bytes_is_sbo(&m_inner);
    }

    /**
     * @brief Ensures capacity for at least @p min_cap bytes.
     * @return true on success, false on allocation failure.
     */
    inline bool reserve(size_t min_cap) noexcept {
        return duo_bytes_reserve(&m_inner, min_cap);
    }

    /**
     * @brief Internal geometric growth helper ensuring capacity for at least @p min_cap bytes.
     * Uses 3-tier adaptive geometric growth via duo_geometric_grow_cap (<256: 2x, <4096: 1.5x, >=4096: 1.25x).
     */
    inline bool grow_for(size_t min_cap) noexcept {
        if (min_cap <= capacity()) {
            return true;
        }
        size_t next_cap = duo_geometric_grow_cap(capacity());
        size_t new_cap = (next_cap > min_cap) ? next_cap : min_cap;
        return reserve(new_cap);
    }

    /**
     * @brief Shrinks capacity to match size.
     * If size <= 23, demotes back to inline SBO storage and frees heap memory.
     * @return true on success, false on allocation failure.
     */
    inline bool shrink_to_fit() noexcept {
        return duo_bytes_shrink_to_fit(&m_inner);
    }

    /** @brief Resets byte count to 0 without releasing buffer capacity. */
    inline void clear() noexcept {
        duo_bytes_clear(&m_inner);
    }

    /** @brief Resizes container to @p new_size bytes. Newly added bytes are zeroed. Truncation is O(1). */
    inline bool resize(size_t new_size) noexcept {
        return duo_bytes_resize(&m_inner, new_size);
    }

    /** @brief Frees any allocated heap buffer and resets container to empty inline SBO state. */
    inline void reset() noexcept {
        duo_bytes_destroy(&m_inner);
    }

    /** @brief Appends a single byte. Returns true on success, false on OOM. */
    inline bool push_back(uint8_t byte) noexcept {
        return duo_bytes_push_back(&m_inner, byte);
    }

    /** @brief Removes the last byte. Returns true on success, false if empty. */
    inline bool pop_back() noexcept {
        return duo_bytes_pop_back(&m_inner);
    }

    /** @brief Appends @p len bytes from @p src. Returns true on success, false on OOM. */
    inline bool append(const void* src, size_t len) noexcept {
        return duo_bytes_append(&m_inner, src, len);
    }

    /** @brief Appends bytes from a SpanView. */
    inline bool append(SpanView<uint8_t> view) noexcept {
        return append(view.data(), view.size());
    }

    /** @brief Inserts @p len bytes from @p src at index @p idx. Returns true on success, false on failure. */
    inline bool insert(size_t idx, const void* src, size_t len) noexcept {
        return duo_bytes_insert(&m_inner, idx, src, len);
    }

    /** @brief Inserts a single byte at index @p idx. Returns true on success, false on failure. */
    inline bool insert(size_t idx, uint8_t byte) noexcept {
        return duo_bytes_insert(&m_inner, idx, &byte, 1);
    }

    /** @brief Erases @p len bytes starting at index @p idx. Returns true on success, false on invalid range. */
    inline bool erase(size_t idx, size_t len) noexcept {
        return duo_bytes_erase(&m_inner, idx, len);
    }

    /** @brief Erases a single byte at index @p idx. Returns true on success, false on invalid range. */
    inline bool erase(size_t idx) noexcept {
        return duo_bytes_erase(&m_inner, idx, 1);
    }

    /** @brief Swaps contents with another Bytes container in O(1) time without allocations. */
    inline void swap(Bytes& other) noexcept {
        c_type tmp;
        DUO_MEMCPY(&tmp, &m_inner, sizeof(m_inner));
        DUO_MEMCPY(&m_inner, &other.m_inner, sizeof(m_inner));
        DUO_MEMCPY(&other.m_inner, &tmp, sizeof(m_inner));
    }

    /** @brief Lexicographical comparison against another byte span view. */
    inline int compare(SpanView<uint8_t> other) const noexcept {
        size_t sz = size();
        size_t osz = other.size();
        size_t min_sz = sz < osz ? sz : osz;
        if (min_sz > 0) {
            int r = memcmp(data(), other.data(), min_sz);
            if (r != 0) return r;
        }
        return sz < osz ? -1 : (sz > osz ? 1 : 0);
    }

    DUO_CXX_DERIVE_HASH(data(), size())

    // Relational operators between Bytes and Bytes
    inline bool operator==(const Bytes& other) const noexcept { return compare(other.as_view()) == 0; }
    inline bool operator!=(const Bytes& other) const noexcept { return compare(other.as_view()) != 0; }
    inline bool operator<(const Bytes& other) const noexcept  { return compare(other.as_view()) < 0; }
    inline bool operator<=(const Bytes& other) const noexcept { return compare(other.as_view()) <= 0; }
    inline bool operator>(const Bytes& other) const noexcept  { return compare(other.as_view()) > 0; }
    inline bool operator>=(const Bytes& other) const noexcept { return compare(other.as_view()) >= 0; }

    // Relational operators between Bytes and SpanView<uint8_t>
    inline bool operator==(SpanView<uint8_t> other) const noexcept { return compare(other) == 0; }
    inline bool operator!=(SpanView<uint8_t> other) const noexcept { return compare(other) != 0; }
    inline bool operator<(SpanView<uint8_t> other) const noexcept  { return compare(other) < 0; }
    inline bool operator<=(SpanView<uint8_t> other) const noexcept { return compare(other) <= 0; }
    inline bool operator>(SpanView<uint8_t> other) const noexcept  { return compare(other) > 0; }
    inline bool operator>=(SpanView<uint8_t> other) const noexcept { return compare(other) >= 0; }

    DUO_CXX_SPAN_BORROW_OPS(uint8_t, duo_bytes_data(const_cast<duo_bytes_t*>(&m_inner)), duo_bytes_size(&m_inner))
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_STANDARD_ACCESSORS(uint8_t, duo_bytes_data(const_cast<duo_bytes_t*>(&m_inner)), duo_bytes_size(&m_inner))
};

/**
 * @brief Non-member swap overload for duo::Bytes.
 */
inline void swap(Bytes& a, Bytes& b) noexcept {
    a.swap(b);
}

// Comparison operators between SpanView<uint8_t> and Bytes
inline bool operator==(SpanView<uint8_t> a, const Bytes& b) noexcept { return b == a; }
inline bool operator!=(SpanView<uint8_t> a, const Bytes& b) noexcept { return b != a; }
inline bool operator<(SpanView<uint8_t> a, const Bytes& b) noexcept  { return b > a; }
inline bool operator<=(SpanView<uint8_t> a, const Bytes& b) noexcept { return b >= a; }
inline bool operator>(SpanView<uint8_t> a, const Bytes& b) noexcept  { return b < a; }
inline bool operator>=(SpanView<uint8_t> a, const Bytes& b) noexcept { return b <= a; }

// Static ABI assertions for Bytes
static_assert(sizeof(duo_bytes_t) == 24, "duo_bytes_t must be exactly 24 bytes!");
static_assert(sizeof(Bytes) == 24, "duo::Bytes must be exactly 24 bytes!");
static_assert(is_standard_layout<duo_bytes_t>::value, "duo_bytes_t must be standard layout!");
static_assert(is_standard_layout<Bytes>::value, "duo::Bytes must be standard layout!");

// ============================================================================
// 5.5 C++17 STACK / FIXED-CAPACITY BYTES: duo::FixedBytes & c_fixed_bytes_t
// ============================================================================

/**
 * @brief Standard-layout C mirror struct alias for fixed stack bytes.
 * Binary-identical to duo_fixed_bytes_t ({ uint8_t* data; size_t size; size_t capacity; }, 24 bytes).
 */
using c_fixed_bytes_t = duo_fixed_bytes_t;

/**
 * @class FixedBytes
 * @brief Freestanding fixed-capacity non-allocating binary byte buffer for C++17.
 *
 * Encapsulates a standard-layout C mirror struct `m_inner` of type c_fixed_bytes_t
 * ({ uint8_t* data; size_t size; size_t capacity; }, exactly 24 bytes on 64-bit platforms).
 *
 * Features:
 *   - Freestanding (-nostdlib++), zero-exception, zero-RTTI.
 *   - Zero heap allocations: operates strictly within a pre-allocated stack or external buffer.
 *   - Capacity overflow protection: push_back, append, insert, and resize safely reject
 *     operations that would exceed capacity, returning false with buffer unmodified.
 *   - Non-owning borrowing as Span<uint8_t> and SpanView<uint8_t>.
 *   - STL iterator suites, element accessors, and bulk data operations.
 *   - Standard C FFI interop via c_ptr(), c_val(), adopt(), release_c(), disown().
 */
class FixedBytes {
public:
    using value_type      = uint8_t;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using reference       = uint8_t&;
    using const_reference = const uint8_t&;
    using pointer         = uint8_t*;
    using const_pointer   = const uint8_t*;
    using c_type          = c_fixed_bytes_t;
    using self_type       = FixedBytes;

    c_type m_inner; /**< Standard-layout C mirror struct: { uint8_t* data; size_t size; size_t capacity; } */

    /** @brief Constructs an empty fixed byte buffer (data = nullptr, size = 0, capacity = 0). */
    inline constexpr FixedBytes() noexcept : m_inner{ nullptr, 0, 0 } {}

    /**
     * @brief Constructs a fixed byte buffer binding an external contiguous buffer with given capacity.
     * Initial size is 0.
     * @param buf Pointer to contiguous byte buffer.
     * @param cap Maximum capacity in bytes.
     */
    inline constexpr FixedBytes(uint8_t* buf, size_t cap) noexcept : m_inner{ buf, 0, cap } {}

    /**
     * @brief Constructs a fixed byte buffer binding an external buffer with pre-existing elements.
     * @param buf Pointer to contiguous byte buffer.
     * @param sz Initial number of active bytes in buffer.
     * @param cap Maximum capacity in bytes.
     */
    inline constexpr FixedBytes(uint8_t* buf, size_t sz, size_t cap) noexcept : m_inner{ buf, sz, cap } {}

    /**
     * @brief Constructs a fixed byte buffer directly from its C mirror struct.
     * @param inner The standard-layout duo_fixed_bytes_t struct.
     */
    inline explicit FixedBytes(c_type inner) noexcept : m_inner(inner) {}

    /** @brief Destructor. Does NOT free the underlying buffer (safe for stack allocations). */
    ~FixedBytes() noexcept = default;

    /** @brief Move constructor stealing buffer reference and resetting other. */
    inline FixedBytes(FixedBytes&& other) noexcept : m_inner(other.m_inner) {
        other.m_inner.data = nullptr;
        other.m_inner.size = 0;
        other.m_inner.capacity = 0;
    }

    /** @brief Move assignment operator stealing buffer reference and resetting other. */
    inline FixedBytes& operator=(FixedBytes&& other) noexcept {
        if (this != &other) {
            m_inner = other.m_inner;
            other.m_inner.data = nullptr;
            other.m_inner.size = 0;
            other.m_inner.capacity = 0;
        }
        return *this;
    }

    FixedBytes(const FixedBytes&) = delete;
    FixedBytes& operator=(const FixedBytes&) = delete;

    /** @brief Returns the maximum byte capacity. */
    inline size_t capacity() const noexcept { return m_inner.capacity; }

    /** @brief Returns the maximum byte capacity (STL compatibility). */
    inline size_t max_size() const noexcept { return m_inner.capacity; }

    /** @brief Returns remaining capacity in bytes before buffer is full. */
    inline size_t remaining() const noexcept { return duo_fixed_bytes_remaining(&m_inner); }

    /** @brief Returns true if size has reached maximum capacity. */
    inline bool full() const noexcept { return duo_fixed_bytes_full(&m_inner); }

    /** @brief Resets size to 0 without modifying the underlying buffer or capacity. */
    inline void clear() noexcept { duo_fixed_bytes_clear(&m_inner); }

    /** @brief Returns a mutable pointer to byte at @p idx, or nullptr if out of bounds. */
    inline uint8_t* at(size_t idx) noexcept { return duo_fixed_bytes_at(&m_inner, idx); }

    /** @brief Returns an immutable pointer to byte at @p idx, or nullptr if out of bounds. */
    inline const uint8_t* at(size_t idx) const noexcept { return duo_fixed_bytes_at_const(&m_inner, idx); }

    /** @brief Appends a single byte. Returns false if buffer is full. */
    inline bool push_back(uint8_t byte) noexcept {
        return duo_fixed_bytes_push_back(&m_inner, byte);
    }

    /** @brief Semantic alias for push_back. */
    inline bool append_byte(uint8_t byte) noexcept {
        return push_back(byte);
    }

    /** @brief Removes the last byte. Returns false if empty. */
    inline bool pop_back() noexcept {
        return duo_fixed_bytes_pop_back(&m_inner);
    }

    /** @brief Appends @p len bytes from @p src. Returns false if buffer would exceed capacity. */
    inline bool append(const void* src, size_t len) noexcept {
        return duo_fixed_bytes_append(&m_inner, src, len);
    }

    /** @brief Appends bytes from a SpanView. Returns false if buffer would exceed capacity. */
    inline bool append(SpanView<uint8_t> view) noexcept {
        return append(view.data(), view.size());
    }

    /** @brief Appends a null-terminated string. Returns false if buffer would exceed capacity. */
    inline bool append(const char* str) noexcept {
        if (!str) return true;
        size_t len = 0;
        while (str[len] != '\0') ++len;
        return append(str, len);
    }

    /** @brief Inserts @p len bytes at index @p idx. Returns false on invalid index or capacity overflow. */
    inline bool insert(size_t idx, const void* src, size_t len) noexcept {
        return duo_fixed_bytes_insert(&m_inner, idx, src, len);
    }

    /** @brief Inserts a single byte at index @p idx. Returns false on invalid index or capacity overflow. */
    inline bool insert(size_t idx, uint8_t byte) noexcept {
        return duo_fixed_bytes_insert(&m_inner, idx, &byte, 1);
    }

    /** @brief Inserts bytes from a SpanView at index @p idx. Returns false on invalid index or overflow. */
    inline bool insert(size_t idx, SpanView<uint8_t> view) noexcept {
        return insert(idx, view.data(), view.size());
    }

    /** @brief Erases @p len bytes starting at index @p idx. Returns false if range is invalid. */
    inline bool erase(size_t idx, size_t len) noexcept {
        return duo_fixed_bytes_erase(&m_inner, idx, len);
    }

    /** @brief Erases a single byte at index @p idx. Returns false if index is out of bounds. */
    inline bool erase(size_t idx) noexcept {
        return duo_fixed_bytes_erase(&m_inner, idx, 1);
    }

    /** @brief Resizes buffer up to capacity. If growing, new elements are initialized to @p fill_byte. */
    inline bool resize(size_t new_size, uint8_t fill_byte = 0) noexcept {
        return duo_fixed_bytes_resize(&m_inner, new_size, fill_byte);
    }

    /** @brief Swaps contents with another FixedBytes container in O(1) time without allocations. */
    inline void swap(FixedBytes& other) noexcept {
        c_type tmp = m_inner;
        m_inner = other.m_inner;
        other.m_inner = tmp;
    }

    /** @brief Lexicographical comparison against another byte span view. */
    inline int compare(SpanView<uint8_t> other) const noexcept {
        size_t sz = size();
        size_t osz = other.size();
        size_t min_sz = sz < osz ? sz : osz;
        if (min_sz > 0) {
            int r = memcmp(data(), other.data(), min_sz);
            if (r != 0) return r;
        }
        return sz < osz ? -1 : (sz > osz ? 1 : 0);
    }

    DUO_CXX_DERIVE_HASH(m_inner.data, m_inner.size)

    // Relational operators between FixedBytes and FixedBytes
    inline bool operator==(const FixedBytes& other) const noexcept { return compare(other.as_view()) == 0; }
    inline bool operator!=(const FixedBytes& other) const noexcept { return compare(other.as_view()) != 0; }
    inline bool operator<(const FixedBytes& other) const noexcept  { return compare(other.as_view()) < 0; }
    inline bool operator<=(const FixedBytes& other) const noexcept { return compare(other.as_view()) <= 0; }
    inline bool operator>(const FixedBytes& other) const noexcept  { return compare(other.as_view()) > 0; }
    inline bool operator>=(const FixedBytes& other) const noexcept { return compare(other.as_view()) >= 0; }

    // Relational operators between FixedBytes and SpanView<uint8_t>
    inline bool operator==(SpanView<uint8_t> other) const noexcept { return compare(other) == 0; }
    inline bool operator!=(SpanView<uint8_t> other) const noexcept { return compare(other) != 0; }
    inline bool operator<(SpanView<uint8_t> other) const noexcept  { return compare(other) < 0; }
    inline bool operator<=(SpanView<uint8_t> other) const noexcept { return compare(other) <= 0; }
    inline bool operator>(SpanView<uint8_t> other) const noexcept  { return compare(other) > 0; }
    inline bool operator>=(SpanView<uint8_t> other) const noexcept { return compare(other) >= 0; }

    DUO_CXX_SPAN_BORROW_OPS(uint8_t, m_inner.data, m_inner.size)
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_STANDARD_ACCESSORS(uint8_t, m_inner.data, m_inner.size)
};

/**
 * @brief Non-member swap overload for duo::FixedBytes.
 */
inline void swap(FixedBytes& a, FixedBytes& b) noexcept {
    a.swap(b);
}

// Comparison operators between SpanView<uint8_t> and FixedBytes
inline bool operator==(SpanView<uint8_t> a, const FixedBytes& b) noexcept { return b == a; }
inline bool operator!=(SpanView<uint8_t> a, const FixedBytes& b) noexcept { return b != a; }
inline bool operator<(SpanView<uint8_t> a, const FixedBytes& b) noexcept  { return b > a; }
inline bool operator<=(SpanView<uint8_t> a, const FixedBytes& b) noexcept { return b >= a; }
inline bool operator>(SpanView<uint8_t> a, const FixedBytes& b) noexcept  { return b < a; }
inline bool operator>=(SpanView<uint8_t> a, const FixedBytes& b) noexcept { return b <= a; }

// Cross-type relational comparisons for byte containers
inline int duo_cxx_bytes_span_cmp(SpanView<uint8_t> a, SpanView<uint8_t> b) noexcept {
    size_t sz = a.size();
    size_t osz = b.size();
    size_t min_sz = sz < osz ? sz : osz;
    if (min_sz > 0) {
        int r = memcmp(a.data(), b.data(), min_sz);
        if (r != 0) return r;
    }
    return sz < osz ? -1 : (sz > osz ? 1 : 0);
}

DUO_CXX_DERIVE_ORDERING_OPS(Bytes, FixedBytes, duo_cxx_bytes_span_cmp)
DUO_CXX_DERIVE_ORDERING_OPS(FixedBytes, Bytes, duo_cxx_bytes_span_cmp)
DUO_CXX_DERIVE_ORDERING_OPS(Bytes, FixedVector<uint8_t>, duo_cxx_bytes_span_cmp)
DUO_CXX_DERIVE_ORDERING_OPS(FixedVector<uint8_t>, Bytes, duo_cxx_bytes_span_cmp)
DUO_CXX_DERIVE_ORDERING_OPS(FixedBytes, FixedVector<uint8_t>, duo_cxx_bytes_span_cmp)
DUO_CXX_DERIVE_ORDERING_OPS(FixedVector<uint8_t>, FixedBytes, duo_cxx_bytes_span_cmp)

// Static ABI assertions for FixedBytes
static_assert(sizeof(c_fixed_bytes_t) == 24, "c_fixed_bytes_t must be exactly 24 bytes!");
static_assert(sizeof(FixedBytes) == 24, "duo::FixedBytes must be exactly 24 bytes!");
static_assert(is_standard_layout<c_fixed_bytes_t>::value, "c_fixed_bytes_t must be standard layout!");
static_assert(is_standard_layout<FixedBytes>::value, "duo::FixedBytes must be standard layout!");

// ============================================================================
// 7. C++17 STRING VIEW: duo::StringView & c_str_view_t
// ============================================================================

/**
 * @brief Standard-layout C mirror struct alias for string view.
 * Binary-identical to duo_str_view_t ({ const char* data; size_t size; }, 16 bytes).
 */
using c_str_view_t = duo_str_view_t;

/**
 * @class StringView
 * @brief Freestanding non-owning immutable string slice for C++17.
 *
 * Encapsulates a standard-layout C mirror struct `m_inner` of type duo_str_view_t,
 * occupying exactly 16 bytes on 64-bit platforms.
 *
 * Features:
 *   - Freestanding (-nostdlib++), zero-exception, zero-RTTI.
 *   - Standard-layout ABI compatible with pure C duo_str_view_t.
 *   - Non-owning: wraps string literals, stack arrays, or heap strings without allocations.
 *   - Implicit compile-time length deduction from string literals via template constructor.
 *   - Substring slicing (substr), prefix/suffix matching, and character/substring search.
 *   - Full iterator suites and lexicographical comparison operators.
 *   - Zero-copy C FFI interop via c_ptr(), c_val(), adopt(), release_c(), disown().
 */
class StringView {
public:
    using value_type      = char;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using reference       = const char&;
    using const_reference = const char&;
    using pointer         = const char*;
    using const_pointer   = const char*;
    using c_type          = duo_str_view_t;
    using self_type       = StringView;

    c_type m_inner; /**< Standard-layout C mirror struct passed directly to C APIs (16 bytes). */

    /** @brief Constructs an empty string view (data = nullptr, size = 0). */
    constexpr inline StringView() noexcept : m_inner{nullptr, 0} {}

    /**
     * @brief Constructs a string view from a pointer and explicit character count.
     * @param data Contiguous character buffer (does not require null termination).
     * @param size Number of characters in the view.
     */
    constexpr inline StringView(const char* data, size_t size) noexcept : m_inner{data, size} {}

    /**
     * @brief Constructs a string view from a null-terminated C string.
     * @param str Null-terminated C string, or nullptr for an empty view.
     */
    inline StringView(const char* str) noexcept : m_inner{duo_str_view_make_cstr(str)} {}

    /**
     * @brief Constructs a string view from a string literal or char array with compile-time deduced length.
     * Automatically excludes the trailing null terminator if present.
     * @tparam N Size of the character array including null terminator.
     */
    template <size_t N>
    constexpr inline StringView(const char (&arr)[N]) noexcept : m_inner{arr, 0} {
        size_t len = 0;
        while (len < N && arr[len] != '\0') {
            ++len;
        }
        m_inner.size = len;
    }

    /** @brief Constructs a string view directly from its pure C mirror struct duo_str_view_t. */
    constexpr inline StringView(duo_str_view_t inner) noexcept : m_inner(inner) {}

    /** @brief Semantic alias for size(). */
    inline size_t length() const noexcept { return m_inner.size; }

    /**
     * @brief Slices a subview starting at @p pos with length clamped to available characters.
     * @param pos Starting character offset.
     * @param count Maximum number of characters to include.
     * @return Clamped subview.
     */
    inline StringView substr(size_t pos = 0, size_t count = static_cast<size_t>(-1)) const noexcept {
        if (pos > m_inner.size) pos = m_inner.size;
        size_t available = m_inner.size - pos;
        if (count > available) count = available;
        return StringView(m_inner.data + pos, count);
    }

    /**
     * @brief Lexicographically compares this view with @p other.
     * @return Negative if this < other, 0 if equal, positive if this > other.
     */
    inline int compare(StringView other) const noexcept {
        return duo_str_view_cmp(m_inner, other.m_inner);
    }

    /** @brief Returns true if this view begins with @p prefix. */
    inline bool starts_with(StringView prefix) const noexcept {
        return duo_str_view_starts_with(m_inner, prefix.m_inner);
    }
    /** @brief Returns true if this view begins with character @p ch. */
    inline bool starts_with(char ch) const noexcept {
        return !empty() && front() == ch;
    }
    /** @brief Returns true if this view begins with null-terminated string @p s. */
    inline bool starts_with(const char* s) const noexcept {
        return starts_with(StringView(s));
    }

    /** @brief Returns true if this view ends with @p suffix. */
    inline bool ends_with(StringView suffix) const noexcept {
        return duo_str_view_ends_with(m_inner, suffix.m_inner);
    }
    /** @brief Returns true if this view ends with character @p ch. */
    inline bool ends_with(char ch) const noexcept {
        return !empty() && back() == ch;
    }
    /** @brief Returns true if this view ends with null-terminated string @p s. */
    inline bool ends_with(const char* s) const noexcept {
        return ends_with(StringView(s));
    }

    /** @brief Searches for first occurrence of character @p ch. Returns index or (size_t)-1. */
    inline size_t find(char ch) const noexcept {
        return duo_str_view_find(m_inner, ch);
    }

    /** @brief Searches for first occurrence of substring @p needle. Returns index or (size_t)-1. */
    inline size_t find(StringView needle) const noexcept {
        if (needle.size() == 0) return 0;
        if (needle.size() > size() || !data() || !needle.data()) return static_cast<size_t>(-1);
        size_t limit = size() - needle.size();
        for (size_t i = 0; i <= limit; ++i) {
            if (memcmp(data() + i, needle.data(), needle.size()) == 0) return i;
        }
        return static_cast<size_t>(-1);
    }

    /** @brief Self-borrow returning a copy of this view. */
    constexpr inline StringView as_view() const noexcept { return *this; }

    /** @brief Borrows an immutable SpanView<char> over this character slice. */
    inline SpanView<char> as_span() const noexcept {
        return SpanView<char>(m_inner.data, m_inner.size);
    }
    inline operator SpanView<char>() const noexcept {
        return as_span();
    }

    /** @brief Slices a subview (semantic alias for substr). */
    inline StringView subview(size_t pos = 0, size_t count = static_cast<size_t>(-1)) const noexcept {
        return substr(pos, count);
    }

    /** @brief Case-insensitive comparison against another view. */
    inline int case_compare(StringView other) const noexcept {
        return duo_str_view_casecmp(m_inner, other.m_inner);
    }

    /** @brief Case-insensitive equality check against another view. */
    inline bool case_eq(StringView other) const noexcept {
        return duo_str_view_case_eq(m_inner, other.m_inner);
    }

    DUO_CXX_DERIVE_HASH(m_inner.data, m_inner.size)
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_VIEW_ACCESSORS(char, m_inner.data, m_inner.size)
};

// Comparison operators between StringView and StringView
inline bool operator==(StringView a, StringView b) noexcept {
    return duo_str_view_eq(a.m_inner, b.m_inner);
}
inline bool operator!=(StringView a, StringView b) noexcept {
    return !duo_str_view_eq(a.m_inner, b.m_inner);
}
inline bool operator<(StringView a, StringView b) noexcept {
    return duo_str_view_cmp(a.m_inner, b.m_inner) < 0;
}
inline bool operator<=(StringView a, StringView b) noexcept {
    return duo_str_view_cmp(a.m_inner, b.m_inner) <= 0;
}
inline bool operator>(StringView a, StringView b) noexcept {
    return duo_str_view_cmp(a.m_inner, b.m_inner) > 0;
}
inline bool operator>=(StringView a, StringView b) noexcept {
    return duo_str_view_cmp(a.m_inner, b.m_inner) >= 0;
}

// Comparison operators between const char* and StringView
inline bool operator==(const char* a, StringView b) noexcept {
    return StringView(a) == b;
}
inline bool operator!=(const char* a, StringView b) noexcept {
    return StringView(a) != b;
}
inline bool operator<(const char* a, StringView b) noexcept {
    return StringView(a) < b;
}
inline bool operator<=(const char* a, StringView b) noexcept {
    return StringView(a) <= b;
}
inline bool operator>(const char* a, StringView b) noexcept {
    return StringView(a) > b;
}
inline bool operator>=(const char* a, StringView b) noexcept {
    return StringView(a) >= b;
}

// Comparison operators between StringView and const char*
inline bool operator==(StringView a, const char* b) noexcept {
    return a == StringView(b);
}
inline bool operator!=(StringView a, const char* b) noexcept {
    return a != StringView(b);
}
inline bool operator<(StringView a, const char* b) noexcept {
    return a < StringView(b);
}
inline bool operator<=(StringView a, const char* b) noexcept {
    return a <= StringView(b);
}
inline bool operator>(StringView a, const char* b) noexcept {
    return a > StringView(b);
}
inline bool operator>=(StringView a, const char* b) noexcept {
    return a >= StringView(b);
}

// Static ABI assertions for StringView
static_assert(sizeof(duo_str_view_t) == 16, "duo_str_view_t must be exactly 16 bytes!");
static_assert(sizeof(StringView) == 16, "duo::StringView must be exactly 16 bytes!");
static_assert(sizeof(StringView) == sizeof(duo_str_view_t), "StringView size must equal duo_str_view_t!");
static_assert(is_standard_layout<duo_str_view_t>::value, "duo_str_view_t must be standard layout!");
static_assert(is_standard_layout<StringView>::value, "duo::StringView must be standard layout!");
static_assert(is_trivially_copyable<StringView>::value, "duo::StringView must be trivially copyable!");

// ============================================================================
// 8. C++17 SBO STRING CONTAINER: duo::String & c_string_t
// ============================================================================

/**
 * @brief Standard-layout C mirror struct alias for SBO string.
 * Binary-identical to duo_string_t (24 bytes on 64-bit platforms).
 */
using c_string_t = duo_string_t;

/**
 * @class String
 * @brief Freestanding Small Buffer Optimized (SBO) null-terminated string container for C++17.
 *
 * Encapsulates a standard-layout C mirror struct `m_inner` of type duo_string_t,
 * occupying exactly 24 bytes on 64-bit platforms.
 *
 * Features:
 *   - Freestanding (-nostdlib++), zero-exception, zero-RTTI.
 *   - Standard-layout ABI compatible with pure C duo_string_t.
 *   - SBO storage: stores up to 22 characters inline on the struct with zero dynamic memory allocation.
 *   - Guaranteed null-termination invariant: c_str() is always instantaneously accessible in O(1)
 *     with zero allocations and zero copying in both inline SBO and heap modes.
 *   - Seamless heap promotion: automatically promotes to dynamic heap allocation with geometric
 *     doubling upon exceeding 22 characters.
 *   - Dynamic demotion: shrink_to_fit() demotes heap storage back to inline SBO when size <= 22,
 *     freeing heap memory and restoring inline mode.
 *   - Zero-copy C FFI: c_ptr(), c_val(), adopt(), disown(), release_c(), and implicit conversion operators.
 *   - Non-owning borrowing as StringView, Span<char>, and SpanView<char>.
 *   - Full STL iterator suites, element accessors, slicing, and mutation primitives.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

class String {
public:
    using value_type      = char;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using reference       = char&;
    using const_reference = const char&;
    using pointer         = char*;
    using const_pointer   = const char*;
    using c_type          = duo_string_t;
    using self_type       = String;

    c_type m_inner; /**< Standard-layout C mirror struct: exactly 24 bytes on 64-bit platforms. */

    /** @brief Constructs an empty string in inline SBO mode (size = 0, capacity = 22, null-terminated). */
    inline String() noexcept {
        duo_string_init(&m_inner);
    }

    /**
     * @brief Constructs a string from a null-terminated C string.
     * @param str Null-terminated C string.
     */
    inline String(const char* str) noexcept {
        duo_string_init_cstr(&m_inner, str);
    }

    /**
     * @brief Constructs a string copying @p len characters from @p str.
     * @param str Character buffer.
     * @param len Number of characters to copy.
     */
    inline String(const char* str, size_t len) noexcept {
        duo_string_init_len(&m_inner, str, len);
    }

    /**
     * @brief Constructs a string from a StringView.
     */
    inline explicit String(StringView sv) noexcept {
        duo_string_init_len(&m_inner, sv.data(), sv.size());
    }

    /**
     * @brief Constructs a string from a pure C duo_str_view_t.
     */
    inline explicit String(duo_str_view_t sv) noexcept {
        duo_string_init_len(&m_inner, sv.data, sv.size);
    }

    /**
     * @brief Constructs a string from a string literal with compile-time deduced length.
     */
    template <size_t N>
    inline String(const char (&arr)[N]) noexcept
        : String(arr, (N > 0 && arr[N - 1] == '\0') ? N - 1 : N) {}

    /**
     * @brief Constructs a string filled with @p count copies of character @p ch.
     */
    inline String(size_t count, char ch) noexcept {
        duo_string_init(&m_inner);
        if (count > 0 && duo_string_reserve(&m_inner, count)) {
            char* ptr = duo_string_data(&m_inner);
            DUO_MEMSET(ptr, ch, count);
            ptr[count] = '\0';
            if (duo_string_is_sbo(&m_inner)) {
                m_inner.m_sbo.tag.length = static_cast<uint8_t>(count);
            } else {
                m_inner.m_heap.m_size = count;
            }
        }
    }

    /** @brief Destroys string, releasing heap memory if allocated. */
    inline ~String() noexcept {
        duo_string_destroy(&m_inner);
    }

    /** @brief Copy constructor: deep-copies characters from @p other. */
    inline String(const String& other) noexcept {
        duo_string_init(&m_inner);
        size_t sz = other.size();
        if (sz > 0) {
            duo_string_append_len(&m_inner, other.data(), sz);
        }
    }

    /** @brief Copy assignment operator: deep-copies characters from @p other. */
    inline String& operator=(const String& other) noexcept {
        if (this != &other) {
            duo_string_destroy(&m_inner);
            size_t sz = other.size();
            if (sz > 0) {
                duo_string_append_len(&m_inner, other.data(), sz);
            }
        }
        return *this;
    }

    /** @brief Move constructor: steals ownership from @p other, resetting @p other to empty SBO. */
    inline String(String&& other) noexcept {
        DUO_MEMCPY(&m_inner, &other.m_inner, sizeof(m_inner));
        duo_string_init(&other.m_inner);
    }

    /** @brief Move assignment operator: steals ownership from @p other. */
    inline String& operator=(String&& other) noexcept {
        if (this != &other) {
            duo_string_destroy(&m_inner);
            DUO_MEMCPY(&m_inner, &other.m_inner, sizeof(m_inner));
            duo_string_init(&other.m_inner);
        }
        return *this;
    }

    /** @brief Assigns from a null-terminated C string. */
    inline String& operator=(const char* str) noexcept {
        duo_string_clear(&m_inner);
        if (str) {
            duo_string_append(&m_inner, str);
        }
        return *this;
    }

    /** @brief Assigns from a StringView. */
    inline String& operator=(StringView sv) noexcept {
        duo_string_clear(&m_inner);
        duo_string_append_len(&m_inner, sv.data(), sv.size());
        return *this;
    }

    /** @brief Returns guaranteed null-terminated C string in O(1) without allocations. */
    inline const char* c_str() const noexcept {
        return duo_string_c_str(&m_inner);
    }

    /** @brief Semantic alias for size(). */
    inline size_t length() const noexcept {
        return size();
    }

    /** @brief Returns maximum character capacity without reallocation. */
    inline size_t capacity() const noexcept {
        return duo_string_capacity(&m_inner);
    }

    /** @brief Returns true if string is currently stored in inline SBO mode. */
    inline bool is_sbo() const noexcept {
        return duo_string_is_sbo(&m_inner);
    }

    /**
     * @brief Pre-allocates buffer capacity for at least @p min_cap characters.
     * @return True on success; false on allocation failure.
     */
    inline bool reserve(size_t min_cap) noexcept {
        return duo_string_reserve(&m_inner, min_cap);
    }

    /**
     * @brief Internal geometric growth helper ensuring capacity for at least @p min_cap characters.
     * Uses 3-tier adaptive geometric growth via duo_geometric_grow_cap (<256: 2x, <4096: 1.5x, >=4096: 1.25x).
     */
    inline bool grow_for(size_t min_cap) noexcept {
        if (min_cap <= capacity()) {
            return true;
        }
        size_t next_cap = duo_geometric_grow_cap(capacity());
        size_t new_cap = (next_cap > min_cap) ? next_cap : min_cap;
        return reserve(new_cap);
    }

    /**
     * @brief Shrinks capacity to fit active characters; demotes to inline SBO if size <= 22.
     * @return True on success; false on reallocation failure.
     */
    inline bool shrink_to_fit() noexcept {
        return duo_string_shrink_to_fit(&m_inner);
    }

    /** @brief Resets string length to 0, maintaining allocated capacity and null terminator. */
    inline void clear() noexcept {
        duo_string_clear(&m_inner);
    }

    /** @brief Releases heap memory and resets to empty SBO state. */
    inline void reset() noexcept {
        duo_string_destroy(&m_inner);
    }

    /** @brief Appends single character. Returns true on success, false on OOM. */
    inline bool push_back(char ch) noexcept {
        return duo_string_push_back(&m_inner, ch);
    }

    /** @brief Removes last character. Returns true on success, false if empty. */
    inline bool pop_back() noexcept {
        return duo_string_pop_back(&m_inner);
    }

    /** @brief Appends @p len characters from @p str. */
    inline bool append(const char* str, size_t len) noexcept {
        return duo_string_append_len(&m_inner, str, len);
    }

    /** @brief Appends null-terminated C string @p str. */
    inline bool append(const char* str) noexcept {
        return duo_string_append(&m_inner, str);
    }

    /** @brief Appends characters from StringView @p sv. */
    inline bool append(StringView sv) noexcept {
        return duo_string_append_len(&m_inner, sv.data(), sv.size());
    }

    /** @brief Appends characters from another String @p s. */
    inline bool append(const String& s) noexcept {
        return duo_string_append_len(&m_inner, s.data(), s.size());
    }

    /** @brief Appends @p count copies of character @p ch. */
    inline bool append(size_t count, char ch) noexcept {
        for (size_t i = 0; i < count; ++i) {
            if (!push_back(ch)) return false;
        }
        return true;
    }

    inline String& operator+=(const char* str) noexcept {
        append(str);
        return *this;
    }
    inline String& operator+=(char ch) noexcept {
        push_back(ch);
        return *this;
    }
    inline String& operator+=(StringView sv) noexcept {
        append(sv);
        return *this;
    }
    inline String& operator+=(const String& s) noexcept {
        append(s);
        return *this;
    }

    /** @brief Inserts @p len characters from @p str at index @p idx. */
    inline bool insert(size_t idx, const char* str, size_t len) noexcept {
        return duo_string_insert(&m_inner, idx, str, len);
    }

    /** @brief Inserts null-terminated C string @p str at index @p idx. */
    inline bool insert(size_t idx, const char* str) noexcept {
        if (!str) return true;
        size_t len = 0;
        while (str[len] != '\0') ++len;
        return duo_string_insert(&m_inner, idx, str, len);
    }

    /** @brief Inserts StringView @p sv at index @p idx. */
    inline bool insert(size_t idx, StringView sv) noexcept {
        return duo_string_insert(&m_inner, idx, sv.data(), sv.size());
    }

    /** @brief Inserts single character @p ch at index @p idx. */
    inline bool insert(size_t idx, char ch) noexcept {
        return duo_string_insert(&m_inner, idx, &ch, 1);
    }

    /** @brief Erases @p len characters starting at index @p idx. */
    inline bool erase(size_t idx, size_t len) noexcept {
        return duo_string_erase(&m_inner, idx, len);
    }

    /** @brief Erases single character at index @p idx. */
    inline bool erase(size_t idx) noexcept {
        return duo_string_erase(&m_inner, idx, 1);
    }

    /** @brief Swaps contents with another String in O(1) time without allocations. */
    inline void swap(String& other) noexcept {
        c_type tmp;
        DUO_MEMCPY(&tmp, &m_inner, sizeof(m_inner));
        DUO_MEMCPY(&m_inner, &other.m_inner, sizeof(m_inner));
        DUO_MEMCPY(&other.m_inner, &tmp, sizeof(m_inner));
    }

    /** @brief Borrows a 16-byte non-owning StringView over this string in O(1). */
    inline StringView as_view() const noexcept {
        return StringView(data(), size());
    }

    /** @brief Implicit conversion to StringView. */
    inline operator StringView() const noexcept {
        return as_view();
    }

    /** @brief Implicit conversion to pure C duo_str_view_t. */
    inline operator duo_str_view_t() const noexcept {
        return duo_str_view_make(data(), size());
    }

    /** @brief Borrows a mutable Span<char> over this string's characters. */
    inline Span<char> as_span() noexcept {
        return Span<char>(data(), size());
    }

    /** @brief Borrows an immutable SpanView<char> over this string's characters. */
    inline SpanView<char> as_span_view() const noexcept {
        return SpanView<char>(data(), size());
    }

    inline operator Span<char>() noexcept {
        return as_span();
    }

    inline operator SpanView<char>() const noexcept {
        return as_span_view();
    }

    /** @brief Slices a substring view starting at @p pos. */
    inline StringView substr(size_t pos = 0, size_t count = static_cast<size_t>(-1)) const noexcept {
        return as_view().substr(pos, count);
    }

    /** @brief Lexicographical comparison against another view. */
    inline int compare(StringView other) const noexcept {
        return as_view().compare(other);
    }

    /** @brief Returns true if this string begins with @p prefix. */
    inline bool starts_with(StringView prefix) const noexcept {
        return as_view().starts_with(prefix);
    }
    /** @brief Returns true if this string begins with character @p ch. */
    inline bool starts_with(char ch) const noexcept {
        return as_view().starts_with(ch);
    }
    /** @brief Returns true if this string begins with null-terminated string @p s. */
    inline bool starts_with(const char* s) const noexcept {
        return as_view().starts_with(s);
    }

    /** @brief Returns true if this string ends with @p suffix. */
    inline bool ends_with(StringView suffix) const noexcept {
        return as_view().ends_with(suffix);
    }
    /** @brief Returns true if this string ends with character @p ch. */
    inline bool ends_with(char ch) const noexcept {
        return as_view().ends_with(ch);
    }
    /** @brief Returns true if this string ends with null-terminated string @p s. */
    inline bool ends_with(const char* s) const noexcept {
        return as_view().ends_with(s);
    }

    /** @brief Searches for first occurrence of character @p ch. Returns index or (size_t)-1. */
    inline size_t find(char ch) const noexcept {
        return as_view().find(ch);
    }
    /** @brief Searches for first occurrence of substring @p needle. Returns index or (size_t)-1. */
    inline size_t find(StringView needle) const noexcept {
        return as_view().find(needle);
    }
    /** @brief Searches for first occurrence of null-terminated string @p s. Returns index or (size_t)-1. */
    inline size_t find(const char* s) const noexcept {
        return as_view().find(StringView(s));
    }

    // Comparison operators between String and String
    inline bool operator==(const String& other) const noexcept { return as_view() == other.as_view(); }
    inline bool operator!=(const String& other) const noexcept { return as_view() != other.as_view(); }
    inline bool operator<(const String& other) const noexcept  { return as_view() < other.as_view(); }
    inline bool operator<=(const String& other) const noexcept { return as_view() <= other.as_view(); }
    inline bool operator>(const String& other) const noexcept  { return as_view() > other.as_view(); }
    inline bool operator>=(const String& other) const noexcept { return as_view() >= other.as_view(); }

    // Comparison operators between String and StringView
    inline bool operator==(StringView other) const noexcept    { return as_view() == other; }
    inline bool operator!=(StringView other) const noexcept    { return as_view() != other; }
    inline bool operator<(StringView other) const noexcept     { return as_view() < other; }
    inline bool operator<=(StringView other) const noexcept    { return as_view() <= other; }
    inline bool operator>(StringView other) const noexcept     { return as_view() > other; }
    inline bool operator>=(StringView other) const noexcept    { return as_view() >= other; }

    // Comparison operators between String and const char*
    inline bool operator==(const char* other) const noexcept   { return as_view() == StringView(other); }
    inline bool operator!=(const char* other) const noexcept   { return as_view() != StringView(other); }
    inline bool operator<(const char* other) const noexcept    { return as_view() < StringView(other); }
    inline bool operator<=(const char* other) const noexcept   { return as_view() <= StringView(other); }
    inline bool operator>(const char* other) const noexcept    { return as_view() > StringView(other); }
    inline bool operator>=(const char* other) const noexcept   { return as_view() >= StringView(other); }

    /** @brief Slices a subview (semantic alias for substr). */
    inline StringView subview(size_t pos = 0, size_t count = static_cast<size_t>(-1)) const noexcept {
        return substr(pos, count);
    }

    /** @brief Case-insensitive comparison against another view. */
    inline int case_compare(StringView other) const noexcept {
        return as_view().case_compare(other);
    }

    /** @brief Case-insensitive equality check against another view. */
    inline bool case_eq(StringView other) const noexcept {
        return as_view().case_eq(other);
    }

    /** @brief Case-insensitive equality check against another String. */
    inline bool case_eq(const String& other) const noexcept {
        return duo_string_case_eq(&m_inner, &other.m_inner);
    }

    DUO_CXX_DERIVE_HASH(data(), size())
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_STANDARD_ACCESSORS(char, duo_string_data(const_cast<duo_string_t*>(&m_inner)), duo_string_size(&m_inner))
};

/**
 * @brief Non-member swap overload for duo::String.
 */
inline void swap(String& a, String& b) noexcept {
    a.swap(b);
}

// Comparison operators between const char* and String
inline bool operator==(const char* lhs, const String& rhs) noexcept {
    return StringView(lhs) == rhs.as_view();
}
inline bool operator!=(const char* lhs, const String& rhs) noexcept {
    return StringView(lhs) != rhs.as_view();
}
inline bool operator<(const char* lhs, const String& rhs) noexcept {
    return StringView(lhs) < rhs.as_view();
}
inline bool operator<=(const char* lhs, const String& rhs) noexcept {
    return StringView(lhs) <= rhs.as_view();
}
inline bool operator>(const char* lhs, const String& rhs) noexcept {
    return StringView(lhs) > rhs.as_view();
}
inline bool operator>=(const char* lhs, const String& rhs) noexcept {
    return StringView(lhs) >= rhs.as_view();
}

inline String operator+(const String& lhs, const String& rhs) noexcept {
    String res = lhs;
    res.append(rhs);
    return res;
}
inline String operator+(const String& lhs, const char* rhs) noexcept {
    String res = lhs;
    res.append(rhs);
    return res;
}
inline String operator+(const char* lhs, const String& rhs) noexcept {
    String res(lhs);
    res.append(rhs);
    return res;
}
inline String operator+(const String& lhs, char rhs) noexcept {
    String res = lhs;
    res.push_back(rhs);
    return res;
}
inline String operator+(const String& lhs, StringView rhs) noexcept {
    String res = lhs;
    res.append(rhs);
    return res;
}
inline String operator+(StringView lhs, const String& rhs) noexcept {
    String res(lhs);
    res.append(rhs);
    return res;
}

// Static ABI assertions for String
static_assert(sizeof(duo_string_t) == 24, "duo_string_t must be exactly 24 bytes!");
static_assert(sizeof(String) == 24, "duo::String must be exactly 24 bytes!");
static_assert(sizeof(String) == sizeof(duo_string_t), "String size must equal duo_string_t!");
static_assert(is_standard_layout<duo_string_t>::value, "duo_string_t must be standard layout!");
static_assert(is_standard_layout<String>::value, "duo::String must be standard layout!");

// ============================================================================
// 9. C++17 STACK / FIXED-CAPACITY STRING: duo::FixedString & c_fixed_string_t
// ============================================================================

/**
 * @brief Standard-layout C mirror struct alias for fixed stack string.
 * Binary-identical to duo_fixed_string_t ({ char* data; size_t size; size_t capacity; }, 24 bytes).
 */
using c_fixed_string_t = duo_fixed_string_t;

/**
 * @class FixedString
 * @brief Freestanding fixed-capacity non-allocating character string container for C++17.
 *
 * Encapsulates a standard-layout C mirror struct `m_inner` of type c_fixed_string_t
 * ({ char* data; size_t size; size_t capacity; }, exactly 24 bytes on 64-bit platforms).
 *
 * Invariant:
 *   - The underlying buffer must have at least (capacity + 1) bytes allocated.
 *   - data[size] is ALWAYS guaranteed to be '\0' at all times in O(1).
 *
 * Features:
 *   - Freestanding (-nostdlib++), zero-exception, zero-RTTI.
 *   - Zero heap allocations: operates strictly within a pre-allocated stack or external buffer.
 *   - Guaranteed O(1) null-terminated C string access via c_str().
 *   - Capacity overflow protection: push_back, append, insert, and resize safely reject
 *     operations that would exceed capacity, returning false with buffer unmodified.
 *   - Non-owning borrowing as StringView, Span<char>, and SpanView<char>.
 *   - Full STL iterator suites, element accessors, and bulk data operations.
 *   - Standard C FFI interop via c_ptr(), c_val(), adopt(), release_c(), disown().
 */
class FixedString {
public:
    using value_type      = char;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;
    using reference       = char&;
    using const_reference = const char&;
    using pointer         = char*;
    using const_pointer   = const char*;
    using c_type          = c_fixed_string_t;
    using self_type       = FixedString;

    c_type m_inner; /**< Standard-layout C mirror struct: { char* data; size_t size; size_t capacity; } */

    /** @brief Constructs an empty fixed string (data = nullptr, size = 0, capacity = 0). */
    inline constexpr FixedString() noexcept : m_inner{ nullptr, 0, 0 } {}

    /**
     * @brief Constructs a fixed string binding an external contiguous buffer with given capacity.
     * Initial size is 0 and buf[0] is set to '\0'.
     * @param buf Pointer to contiguous character buffer (at least cap + 1 bytes allocated).
     * @param cap Maximum character capacity (excluding null terminator).
     */
    inline FixedString(char* buf, size_t cap) noexcept {
        duo_fixed_string_init(&m_inner, buf, cap);
    }

    /**
     * @brief Constructs a fixed string binding an external buffer with pre-existing elements.
     * @param buf Pointer to contiguous character buffer (at least cap + 1 bytes allocated).
     * @param sz Initial number of active characters in buffer.
     * @param cap Maximum character capacity (excluding null terminator).
     */
    inline constexpr FixedString(char* buf, size_t sz, size_t cap) noexcept : m_inner{ buf, sz, cap } {}

    /**
     * @brief Constructs a fixed string binding an external buffer and copying a C string.
     * @param buf Pointer to contiguous character buffer (at least cap + 1 bytes allocated).
     * @param cap Maximum character capacity (excluding null terminator).
     * @param str Null-terminated string to copy.
     */
    inline FixedString(char* buf, size_t cap, const char* str) noexcept {
        duo_fixed_string_init_from(&m_inner, buf, cap, str);
    }

    /**
     * @brief Constructs a fixed string binding an external buffer and copying an explicit character slice.
     * @param buf Pointer to contiguous character buffer (at least cap + 1 bytes allocated).
     * @param cap Maximum character capacity (excluding null terminator).
     * @param str Character slice.
     * @param len Number of characters to copy.
     */
    inline FixedString(char* buf, size_t cap, const char* str, size_t len) noexcept {
        duo_fixed_string_init_from_len(&m_inner, buf, cap, str, len);
    }

    /**
     * @brief Constructs a fixed string directly from its C mirror struct.
     * @param inner The standard-layout duo_fixed_string_t struct.
     */
    inline explicit FixedString(c_type inner) noexcept : m_inner(inner) {}

    /** @brief Destructor. Does NOT free the underlying buffer (safe for stack allocations). */
    ~FixedString() noexcept = default;

    /** @brief Move constructor stealing buffer reference and resetting other. */
    inline FixedString(FixedString&& other) noexcept : m_inner(other.m_inner) {
        other.m_inner.data = nullptr;
        other.m_inner.size = 0;
        other.m_inner.capacity = 0;
    }

    /** @brief Move assignment operator stealing buffer reference and resetting other. */
    inline FixedString& operator=(FixedString&& other) noexcept {
        if (this != &other) {
            m_inner = other.m_inner;
            other.m_inner.data = nullptr;
            other.m_inner.size = 0;
            other.m_inner.capacity = 0;
        }
        return *this;
    }

    FixedString(const FixedString&) = delete;
    FixedString& operator=(const FixedString&) = delete;

    /** @brief Returns the maximum character capacity (excluding null terminator). */
    inline size_t capacity() const noexcept { return m_inner.capacity; }

    /** @brief Returns the maximum character capacity (STL compatibility). */
    inline size_t max_size() const noexcept { return m_inner.capacity; }

    /** @brief Returns remaining capacity before buffer is full. */
    inline size_t remaining() const noexcept { return duo_fixed_string_remaining(&m_inner); }

    /** @brief Returns true if size has reached maximum capacity. */
    inline bool full() const noexcept { return duo_fixed_string_full(&m_inner); }

    /** @brief Semantic alias for size(). */
    inline size_t length() const noexcept { return m_inner.size; }

    /** @brief Returns a guaranteed null-terminated C string in O(1) without allocations. */
    inline const char* c_str() const noexcept { return duo_fixed_string_c_str(&m_inner); }

    /** @brief Resets size to 0 and writes '\0' at index 0. */
    inline void clear() noexcept { duo_fixed_string_clear(&m_inner); }

    /** @brief Returns a mutable pointer to character at @p idx, or nullptr if out of bounds. */
    inline char* at(size_t idx) noexcept { return duo_fixed_string_at(&m_inner, idx); }

    /** @brief Returns an immutable pointer to character at @p idx, or nullptr if out of bounds. */
    inline const char* at(size_t idx) const noexcept { return duo_fixed_string_at_const(&m_inner, idx); }

    /** @brief Appends a single character. Returns false if buffer is full. */
    inline bool push_back(char ch) noexcept {
        return duo_fixed_string_push_back(&m_inner, ch);
    }

    /** @brief Semantic alias for push_back. */
    inline bool append_char(char ch) noexcept {
        return push_back(ch);
    }

    /** @brief Removes the last character and restores '\0'. Returns false if empty. */
    inline bool pop_back() noexcept {
        return duo_fixed_string_pop_back(&m_inner);
    }

    /** @brief Appends null-terminated C string @p str. Returns false if buffer would exceed capacity. */
    inline bool append(const char* str) noexcept {
        return duo_fixed_string_append(&m_inner, str);
    }

    /** @brief Appends @p len characters from @p str. Returns false if buffer would exceed capacity. */
    inline bool append(const char* str, size_t len) noexcept {
        return duo_fixed_string_append_len(&m_inner, str, len);
    }

    /** @brief Appends StringView @p sv. Returns false if buffer would exceed capacity. */
    inline bool append(StringView sv) noexcept {
        return duo_fixed_string_append_len(&m_inner, sv.data(), sv.size());
    }

    /** @brief Appends another FixedString. Returns false if buffer would exceed capacity. */
    inline bool append(const FixedString& other) noexcept {
        return duo_fixed_string_append_len(&m_inner, other.data(), other.size());
    }

    inline FixedString& operator+=(char ch) noexcept {
        push_back(ch);
        return *this;
    }
    inline FixedString& operator+=(const char* str) noexcept {
        append(str);
        return *this;
    }
    inline FixedString& operator+=(StringView sv) noexcept {
        append(sv);
        return *this;
    }
    inline FixedString& operator+=(const FixedString& other) noexcept {
        append(other);
        return *this;
    }

    /** @brief Inserts @p len characters from @p str at index @p idx. Returns false on invalid index or overflow. */
    inline bool insert(size_t idx, const char* str, size_t len) noexcept {
        return duo_fixed_string_insert_len(&m_inner, idx, str, len);
    }

    /** @brief Inserts null-terminated C string @p str at index @p idx. */
    inline bool insert(size_t idx, const char* str) noexcept {
        return duo_fixed_string_insert(&m_inner, idx, str);
    }

    /** @brief Inserts StringView @p sv at index @p idx. */
    inline bool insert(size_t idx, StringView sv) noexcept {
        return duo_fixed_string_insert_len(&m_inner, idx, sv.data(), sv.size());
    }

    /** @brief Inserts single character @p ch at index @p idx. */
    inline bool insert(size_t idx, char ch) noexcept {
        return duo_fixed_string_insert_len(&m_inner, idx, &ch, 1);
    }

    /** @brief Erases @p len characters starting at index @p idx. Returns false if range is invalid. */
    inline bool erase(size_t idx, size_t len) noexcept {
        return duo_fixed_string_erase(&m_inner, idx, len);
    }

    /** @brief Erases a single character at index @p idx. Returns false if index is out of bounds. */
    inline bool erase(size_t idx) noexcept {
        return duo_fixed_string_erase(&m_inner, idx, 1);
    }

    /** @brief Resizes string up to capacity. If growing, new elements are initialized to @p fill_char. */
    inline bool resize(size_t new_size, char fill_char = '\0') noexcept {
        return duo_fixed_string_resize(&m_inner, new_size, fill_char);
    }

    /** @brief Swaps contents with another FixedString in O(1) time without allocations. */
    inline void swap(FixedString& other) noexcept {
        c_type tmp = m_inner;
        m_inner = other.m_inner;
        other.m_inner = tmp;
    }

    /** @brief Borrows a 16-byte non-owning StringView over this string in O(1). */
    inline StringView as_view() const noexcept {
        return StringView(data(), size());
    }

    /** @brief Implicit conversion to StringView. */
    inline operator StringView() const noexcept {
        return as_view();
    }

    /** @brief Implicit conversion to pure C duo_str_view_t. */
    inline operator duo_str_view_t() const noexcept {
        return duo_fixed_string_as_view(&m_inner);
    }

    /** @brief Borrows a mutable Span<char> over this string's characters. */
    inline Span<char> as_span() noexcept {
        return Span<char>(data(), size());
    }

    /** @brief Borrows an immutable SpanView<char> over this string's characters. */
    inline SpanView<char> as_span_view() const noexcept {
        return SpanView<char>(data(), size());
    }

    inline operator Span<char>() noexcept {
        return as_span();
    }

    inline operator SpanView<char>() const noexcept {
        return as_span_view();
    }

    /** @brief Slices a substring view starting at @p pos. */
    inline StringView substr(size_t pos = 0, size_t count = static_cast<size_t>(-1)) const noexcept {
        return as_view().substr(pos, count);
    }

    /** @brief Lexicographical comparison against another view. */
    inline int compare(StringView other) const noexcept {
        return as_view().compare(other);
    }

    /** @brief Returns true if this string begins with @p prefix. */
    inline bool starts_with(StringView prefix) const noexcept {
        return as_view().starts_with(prefix);
    }
    /** @brief Returns true if this string begins with character @p ch. */
    inline bool starts_with(char ch) const noexcept {
        return as_view().starts_with(ch);
    }
    /** @brief Returns true if this string begins with null-terminated string @p s. */
    inline bool starts_with(const char* s) const noexcept {
        return as_view().starts_with(s);
    }

    /** @brief Returns true if this string ends with @p suffix. */
    inline bool ends_with(StringView suffix) const noexcept {
        return as_view().ends_with(suffix);
    }
    /** @brief Returns true if this string ends with character @p ch. */
    inline bool ends_with(char ch) const noexcept {
        return as_view().ends_with(ch);
    }
    /** @brief Returns true if this string ends with null-terminated string @p s. */
    inline bool ends_with(const char* s) const noexcept {
        return as_view().ends_with(s);
    }

    /** @brief Searches for first occurrence of character @p ch. Returns index or (size_t)-1. */
    inline size_t find(char ch) const noexcept {
        return as_view().find(ch);
    }
    /** @brief Searches for first occurrence of substring @p needle. Returns index or (size_t)-1. */
    inline size_t find(StringView needle) const noexcept {
        return as_view().find(needle);
    }
    /** @brief Searches for first occurrence of null-terminated string @p s. Returns index or (size_t)-1. */
    inline size_t find(const char* s) const noexcept {
        return as_view().find(StringView(s));
    }

    // Comparison operators between FixedString and FixedString
    inline bool operator==(const FixedString& other) const noexcept { return as_view() == other.as_view(); }
    inline bool operator!=(const FixedString& other) const noexcept { return as_view() != other.as_view(); }
    inline bool operator<(const FixedString& other) const noexcept  { return as_view() < other.as_view(); }
    inline bool operator<=(const FixedString& other) const noexcept { return as_view() <= other.as_view(); }
    inline bool operator>(const FixedString& other) const noexcept  { return as_view() > other.as_view(); }
    inline bool operator>=(const FixedString& other) const noexcept { return as_view() >= other.as_view(); }

    // Comparison operators between FixedString and StringView
    inline bool operator==(StringView other) const noexcept         { return as_view() == other; }
    inline bool operator!=(StringView other) const noexcept         { return as_view() != other; }
    inline bool operator<(StringView other) const noexcept          { return as_view() < other; }
    inline bool operator<=(StringView other) const noexcept         { return as_view() <= other; }
    inline bool operator>(StringView other) const noexcept          { return as_view() > other; }
    inline bool operator>=(StringView other) const noexcept         { return as_view() >= other; }

    // Comparison operators between FixedString and const char*
    inline bool operator==(const char* other) const noexcept        { return as_view() == StringView(other); }
    inline bool operator!=(const char* other) const noexcept        { return as_view() != StringView(other); }
    inline bool operator<(const char* other) const noexcept         { return as_view() < StringView(other); }
    inline bool operator<=(const char* other) const noexcept        { return as_view() <= StringView(other); }
    inline bool operator>(const char* other) const noexcept         { return as_view() > StringView(other); }
    inline bool operator>=(const char* other) const noexcept        { return as_view() >= StringView(other); }

    /** @brief Slices a subview (semantic alias for substr). */
    inline StringView subview(size_t pos = 0, size_t count = static_cast<size_t>(-1)) const noexcept {
        return substr(pos, count);
    }

    /** @brief Case-insensitive comparison against another view. */
    inline int case_compare(StringView other) const noexcept {
        return as_view().case_compare(other);
    }

    /** @brief Case-insensitive equality check against another view. */
    inline bool case_eq(StringView other) const noexcept {
        return as_view().case_eq(other);
    }

    /** @brief Case-insensitive equality check against another FixedString. */
    inline bool case_eq(const FixedString& other) const noexcept {
        return duo_fixed_string_case_eq(&m_inner, &other.m_inner);
    }

    DUO_CXX_DERIVE_HASH(m_inner.data, m_inner.size)
    DUO_CXX_FFI_OPS(m_inner)
    DUO_CXX_STANDARD_ACCESSORS(char, m_inner.data, m_inner.size)
};

/**
 * @brief Non-member swap overload for duo::FixedString.
 */
inline void swap(FixedString& a, FixedString& b) noexcept {
    a.swap(b);
}

// Comparison operators between const char* and FixedString
inline bool operator==(const char* lhs, const FixedString& rhs) noexcept { return rhs == lhs; }
inline bool operator!=(const char* lhs, const FixedString& rhs) noexcept { return rhs != lhs; }
inline bool operator<(const char* lhs, const FixedString& rhs) noexcept  { return StringView(lhs) < rhs.as_view(); }
inline bool operator<=(const char* lhs, const FixedString& rhs) noexcept { return StringView(lhs) <= rhs.as_view(); }
inline bool operator>(const char* lhs, const FixedString& rhs) noexcept  { return StringView(lhs) > rhs.as_view(); }
inline bool operator>=(const char* lhs, const FixedString& rhs) noexcept { return StringView(lhs) >= rhs.as_view(); }

// Comparison operators between StringView and FixedString
inline bool operator==(StringView lhs, const FixedString& rhs) noexcept  { return rhs == lhs; }
inline bool operator!=(StringView lhs, const FixedString& rhs) noexcept  { return rhs != lhs; }
inline bool operator<(StringView lhs, const FixedString& rhs) noexcept   { return lhs < rhs.as_view(); }
inline bool operator<=(StringView lhs, const FixedString& rhs) noexcept  { return lhs <= rhs.as_view(); }
inline bool operator>(StringView lhs, const FixedString& rhs) noexcept   { return lhs > rhs.as_view(); }
inline bool operator>=(StringView lhs, const FixedString& rhs) noexcept  { return lhs >= rhs.as_view(); }

// Comparison operators between String and FixedString
inline int duo_cxx_str_fixed_cmp(const String& a, const FixedString& b) noexcept {
    return duo_str_view_cmp(a.as_view().m_inner, b.as_view().m_inner);
}
inline int duo_cxx_fixed_str_cmp(const FixedString& a, const String& b) noexcept {
    return duo_str_view_cmp(a.as_view().m_inner, b.as_view().m_inner);
}

DUO_CXX_DERIVE_ORDERING_OPS(String, FixedString, duo_cxx_str_fixed_cmp)
DUO_CXX_DERIVE_ORDERING_OPS(FixedString, String, duo_cxx_fixed_str_cmp)

inline String operator+(const FixedString& lhs, const FixedString& rhs) noexcept {
    String out(lhs.as_view());
    out.append(rhs.as_view());
    return out;
}
inline String operator+(const FixedString& lhs, StringView rhs) noexcept {
    String out(lhs.as_view());
    out.append(rhs);
    return out;
}
inline String operator+(StringView lhs, const FixedString& rhs) noexcept {
    String out(lhs);
    out.append(rhs.as_view());
    return out;
}
inline String operator+(const FixedString& lhs, const char* rhs) noexcept {
    String out(lhs.as_view());
    out.append(rhs);
    return out;
}
inline String operator+(const char* lhs, const FixedString& rhs) noexcept {
    String out(lhs);
    out.append(rhs.as_view());
    return out;
}
inline String operator+(const FixedString& lhs, char rhs) noexcept {
    String out(lhs.as_view());
    out.push_back(rhs);
    return out;
}

// Static ABI assertions for FixedString
static_assert(sizeof(c_fixed_string_t) == 24, "c_fixed_string_t must be exactly 24 bytes!");
static_assert(sizeof(FixedString) == 24, "duo::FixedString must be exactly 24 bytes!");
static_assert(sizeof(FixedString) == sizeof(c_fixed_string_t), "FixedString size must equal c_fixed_string_t!");
static_assert(is_standard_layout<c_fixed_string_t>::value, "c_fixed_string_t must be standard layout!");
static_assert(is_standard_layout<FixedString>::value, "duo::FixedString must be standard layout!");

// ============================================================================
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// ============================================================================
// ZERO-HEAP SCOPED STACK DISPATCHERS (Vector, Bytes, String)
// ============================================================================

/**
 * @brief Zero-heap scoped stack dispatcher for FixedVector<T> with transparent dynamic heap fallback.
 *
 * Uses tiered stack buffers (32B, 64B, 128B, 256B, 512B, 1024B) via a switch statement
 * if capacity <= MaxStackElems and total byte size (capacity * sizeof(T)) <= 1024 bytes.
 * If requested capacity exceeds stack limits, transparently allocates dynamic heap storage
 * via duo::Array<T> with RAII cleanup on scope exit.
 *
 * The visitor callback @p fn always receives a monomorphic reference: duo::FixedVector<T>&.
 *
 * @tparam T Element type.
 * @tparam MaxStackElems Maximum number of elements eligible for stack allocation (default 64).
 * @tparam Fn Visitor callable type with signature (FixedVector<T>&) -> R.
 * @param capacity Requested element capacity.
 * @param fn Visitor callable invoked with the initialized FixedVector<T>&.
 * @return Value returned by @p fn.
 */
template <typename T, size_t MaxStackElems = 64, typename Fn>
inline auto with_stack_vector(size_t capacity, Fn&& fn) {
    if (capacity <= MaxStackElems && capacity * sizeof(T) <= 1024) {
        switch (duo_stack_tier_bytes(capacity * sizeof(T))) {
#define DUO_CXX_VEC_STACK_TIER_CASE_(Cap) \
            case Cap: { \
                alignas(T) unsigned char stack_buf[Cap]; \
                FixedVector<T> vec(reinterpret_cast<T*>(stack_buf), capacity); \
                return fn(vec); \
            }
            DUO_CXX_VEC_STACK_TIER_CASE_(8)
            DUO_CXX_VEC_STACK_TIER_CASE_(16)
            DUO_CXX_VEC_STACK_TIER_CASE_(24)
            DUO_CXX_VEC_STACK_TIER_CASE_(32)
            DUO_CXX_VEC_STACK_TIER_CASE_(40)
            DUO_CXX_VEC_STACK_TIER_CASE_(48)
            DUO_CXX_VEC_STACK_TIER_CASE_(56)
            DUO_CXX_VEC_STACK_TIER_CASE_(64)
            DUO_CXX_VEC_STACK_TIER_CASE_(80)
            DUO_CXX_VEC_STACK_TIER_CASE_(96)
            DUO_CXX_VEC_STACK_TIER_CASE_(112)
            DUO_CXX_VEC_STACK_TIER_CASE_(128)
            DUO_CXX_VEC_STACK_TIER_CASE_(160)
            DUO_CXX_VEC_STACK_TIER_CASE_(192)
            DUO_CXX_VEC_STACK_TIER_CASE_(224)
            DUO_CXX_VEC_STACK_TIER_CASE_(256)
            DUO_CXX_VEC_STACK_TIER_CASE_(320)
            DUO_CXX_VEC_STACK_TIER_CASE_(384)
            DUO_CXX_VEC_STACK_TIER_CASE_(448)
            DUO_CXX_VEC_STACK_TIER_CASE_(512)
            DUO_CXX_VEC_STACK_TIER_CASE_(640)
            DUO_CXX_VEC_STACK_TIER_CASE_(768)
            DUO_CXX_VEC_STACK_TIER_CASE_(896)
            DUO_CXX_VEC_STACK_TIER_CASE_(1024)
#undef DUO_CXX_VEC_STACK_TIER_CASE_
            default:
                break;
        }
    }
    Array<T> heap_arr(capacity);
    FixedVector<T> vec(heap_arr.data(), capacity);
    return fn(vec);
}

/**
 * @brief Zero-heap scoped stack dispatcher for FixedBytes with transparent dynamic heap fallback.
 *
 * Uses fine-grained tiered stack buffers (~1.25x growth: 8B..1024B) via a switch statement
 * if capacity <= MaxBytes. If requested capacity exceeds MaxBytes, transparently allocates
 * dynamic heap storage via duo::Array<uint8_t> with RAII cleanup on scope exit.
 *
 * The visitor callback @p fn always receives a monomorphic reference: duo::FixedBytes&.
 *
 * @tparam MaxBytes Maximum byte capacity eligible for stack allocation (default 1024, multiple of 64).
 * @tparam Fn Visitor callable type with signature (FixedBytes&) -> R.
 * @param capacity Requested byte capacity.
 * @param fn Visitor callable invoked with the initialized FixedBytes&.
 * @return Value returned by @p fn.
 */
template <size_t MaxBytes = 1024, typename Fn>
inline auto with_stack_bytes(size_t capacity, Fn&& fn) {
    static_assert(MaxBytes % 64 == 0, "MaxBytes must be a multiple of 64");
    if (capacity <= MaxBytes) {
        switch (duo_stack_tier_bytes(capacity)) {
#define DUO_CXX_BYTES_STACK_TIER_CASE_(Cap) \
            case Cap: { \
                uint8_t stack_buf[Cap]; \
                FixedBytes bytes(stack_buf, capacity); \
                return fn(bytes); \
            }
            DUO_CXX_BYTES_STACK_TIER_CASE_(8)
            DUO_CXX_BYTES_STACK_TIER_CASE_(16)
            DUO_CXX_BYTES_STACK_TIER_CASE_(24)
            DUO_CXX_BYTES_STACK_TIER_CASE_(32)
            DUO_CXX_BYTES_STACK_TIER_CASE_(40)
            DUO_CXX_BYTES_STACK_TIER_CASE_(48)
            DUO_CXX_BYTES_STACK_TIER_CASE_(56)
            DUO_CXX_BYTES_STACK_TIER_CASE_(64)
            DUO_CXX_BYTES_STACK_TIER_CASE_(80)
            DUO_CXX_BYTES_STACK_TIER_CASE_(96)
            DUO_CXX_BYTES_STACK_TIER_CASE_(112)
            DUO_CXX_BYTES_STACK_TIER_CASE_(128)
            DUO_CXX_BYTES_STACK_TIER_CASE_(160)
            DUO_CXX_BYTES_STACK_TIER_CASE_(192)
            DUO_CXX_BYTES_STACK_TIER_CASE_(224)
            DUO_CXX_BYTES_STACK_TIER_CASE_(256)
            DUO_CXX_BYTES_STACK_TIER_CASE_(320)
            DUO_CXX_BYTES_STACK_TIER_CASE_(384)
            DUO_CXX_BYTES_STACK_TIER_CASE_(448)
            DUO_CXX_BYTES_STACK_TIER_CASE_(512)
            DUO_CXX_BYTES_STACK_TIER_CASE_(640)
            DUO_CXX_BYTES_STACK_TIER_CASE_(768)
            DUO_CXX_BYTES_STACK_TIER_CASE_(896)
            DUO_CXX_BYTES_STACK_TIER_CASE_(1024)
#undef DUO_CXX_BYTES_STACK_TIER_CASE_
            default:
                break;
        }
    }
    Array<uint8_t> heap_arr(capacity > 0 ? capacity : 1);
    FixedBytes bytes(heap_arr.data(), capacity);
    return fn(bytes);
}

/**
 * @brief Zero-heap scoped stack dispatcher for FixedString with transparent dynamic heap fallback.
 *
 * Uses fine-grained tiered stack buffers (~1.25x growth: 8B..1024B + 1 for null terminator)
 * via a switch statement if capacity <= MaxChars. If requested capacity exceeds MaxChars,
 * transparently allocates dynamic heap storage via duo::Array<char> with RAII cleanup on scope exit.
 *
 * The visitor callback @p fn always receives a monomorphic reference: duo::FixedString&.
 *
 * @tparam MaxChars Maximum character capacity eligible for stack allocation (default 1024, multiple of 64).
 * @tparam Fn Visitor callable type with signature (FixedString&) -> R.
 * @param capacity Requested character capacity (excluding null terminator).
 * @param fn Visitor callable invoked with the initialized FixedString&.
 * @return Value returned by @p fn.
 */
template <size_t MaxChars = 1024, typename Fn>
inline auto with_stack_string(size_t capacity, Fn&& fn) {
    static_assert(MaxChars % 64 == 0, "MaxChars must be a multiple of 64");
    if (capacity <= MaxChars) {
        switch (duo_stack_tier_bytes(capacity)) {
#define DUO_CXX_STR_STACK_TIER_CASE_(Cap) \
            case Cap: { \
                char stack_buf[(Cap) + 1]; \
                stack_buf[0] = '\0'; \
                FixedString str(stack_buf, capacity); \
                return fn(str); \
            }
            DUO_CXX_STR_STACK_TIER_CASE_(8)
            DUO_CXX_STR_STACK_TIER_CASE_(16)
            DUO_CXX_STR_STACK_TIER_CASE_(24)
            DUO_CXX_STR_STACK_TIER_CASE_(32)
            DUO_CXX_STR_STACK_TIER_CASE_(40)
            DUO_CXX_STR_STACK_TIER_CASE_(48)
            DUO_CXX_STR_STACK_TIER_CASE_(56)
            DUO_CXX_STR_STACK_TIER_CASE_(64)
            DUO_CXX_STR_STACK_TIER_CASE_(80)
            DUO_CXX_STR_STACK_TIER_CASE_(96)
            DUO_CXX_STR_STACK_TIER_CASE_(112)
            DUO_CXX_STR_STACK_TIER_CASE_(128)
            DUO_CXX_STR_STACK_TIER_CASE_(160)
            DUO_CXX_STR_STACK_TIER_CASE_(192)
            DUO_CXX_STR_STACK_TIER_CASE_(224)
            DUO_CXX_STR_STACK_TIER_CASE_(256)
            DUO_CXX_STR_STACK_TIER_CASE_(320)
            DUO_CXX_STR_STACK_TIER_CASE_(384)
            DUO_CXX_STR_STACK_TIER_CASE_(448)
            DUO_CXX_STR_STACK_TIER_CASE_(512)
            DUO_CXX_STR_STACK_TIER_CASE_(640)
            DUO_CXX_STR_STACK_TIER_CASE_(768)
            DUO_CXX_STR_STACK_TIER_CASE_(896)
            DUO_CXX_STR_STACK_TIER_CASE_(1024)
#undef DUO_CXX_STR_STACK_TIER_CASE_
            default:
                break;
        }
    }
    Array<char> heap_arr((capacity > 0 ? capacity : 1) + 1);
    if (heap_arr.data()) {
        heap_arr.data()[0] = '\0';
    }
    FixedString str(heap_arr.data(), capacity);
    return fn(str);
}

} // namespace duo


// ============================================================================
// 6. STACK ARRAY MACROS (Zero heap allocations, stack storage + FixedArray view)
// ============================================================================

/**
 * @def DUO_STACK_ARRAY(Type, Name, Capacity)
 * @brief Allocates an uninitialized fixed-capacity buffer directly on the stack frame
 * and binds a duo::FixedArray<Type> view to it. Zero heap allocations.
 * @param Type Element type (e.g. int, double, MyStruct).
 * @param Name Variable name for the created FixedArray.
 * @param Capacity Compile-time element count allocated on the stack frame.
 */
#define DUO_STACK_ARRAY(Type, Name, Capacity) \
    Type Name##_raw_buf_[(Capacity)]; \
    ::duo::FixedArray<Type> Name(Name##_raw_buf_, (Capacity))

/**
 * @def DUO_STACK_ARRAY_INIT(Type, Name, ...)
 * @brief Allocates and initializes a contiguous buffer on the stack frame from varargs,
 * binding a duo::FixedArray<Type> view with automatically inferred element count.
 * @param Type Element type.
 * @param Name Variable name for the created FixedArray.
 * @param ... Initializer list elements.
 */
#define DUO_STACK_ARRAY_INIT(Type, Name, ...) \
    Type Name##_raw_buf_[] = { __VA_ARGS__ }; \
    ::duo::FixedArray<Type> Name(Name##_raw_buf_, sizeof(Name##_raw_buf_) / sizeof(Name##_raw_buf_[0]))

/**
 * @def DUO_STACK_ARRAY_OF(Type, Name, Capacity, ...)
 * @brief Allocates an explicit fixed-capacity buffer on the stack frame and initializes
 * elements from varargs, binding a duo::FixedArray<Type> view.
 * @param Type Element type.
 * @param Name Variable name for the created FixedArray.
 * @param Capacity Total element capacity on the stack frame.
 * @param ... Initializer list elements.
 */
#define DUO_STACK_ARRAY_OF(Type, Name, Capacity, ...) \
    Type Name##_raw_buf_[(Capacity)] = { __VA_ARGS__ }; \
    ::duo::FixedArray<Type> Name(Name##_raw_buf_, (Capacity))

// ============================================================================
// 7. STACK VECTOR MACROS (Zero heap allocations, stack storage + FixedVector view)
// ============================================================================

/**
 * @def DUO_STACK_VECTOR(Type, Name, Capacity)
 * @brief Allocates an uninitialized fixed-capacity buffer directly on the stack frame
 * and binds a duo::FixedVector<Type> to it with initial size 0 and capacity Capacity.
 * @param Type Element type (e.g. int, double, MyStruct).
 * @param Name Variable name for the created FixedVector.
 * @param Capacity Compile-time element capacity on the stack frame.
 */
#define DUO_STACK_VECTOR(Type, Name, Capacity) \
    alignas(Type) unsigned char Name##_raw_buf_[(Capacity) * sizeof(Type)]; \
    ::duo::FixedVector<Type> Name(reinterpret_cast<Type*>(Name##_raw_buf_), (Capacity))

/**
 * @def DUO_STACK_VECTOR_INIT(Type, Name, ...)
 * @brief Allocates and initializes a contiguous buffer on the stack frame from varargs,
 * binding a duo::FixedVector<Type> with size and capacity equal to element count.
 * @param Type Element type.
 * @param Name Variable name for the created FixedVector.
 * @param ... Initializer list elements.
 */
#define DUO_STACK_VECTOR_INIT(Type, Name, ...) \
    Type Name##_raw_buf_[] = { __VA_ARGS__ }; \
    ::duo::FixedVector<Type> Name(Name##_raw_buf_, sizeof(Name##_raw_buf_) / sizeof(Name##_raw_buf_[0]), sizeof(Name##_raw_buf_) / sizeof(Name##_raw_buf_[0]))

/**
 * @def DUO_STACK_VECTOR_OF(Type, Name, Capacity, ...)
 * @brief Allocates an explicit fixed-capacity buffer on the stack frame and initializes
 * initial elements from varargs, binding a duo::FixedVector<Type> with capacity Capacity.
 * @param Type Element type.
 * @param Name Variable name for the created FixedVector.
 * @param Capacity Total element capacity on the stack frame.
 * @param ... Initializer list elements.
 */
#define DUO_STACK_VECTOR_OF(Type, Name, Capacity, ...) \
    Type Name##_raw_buf_[(Capacity)] = { __VA_ARGS__ }; \
    const Type Name##_init_[] = { __VA_ARGS__ }; \
    ::duo::FixedVector<Type> Name(Name##_raw_buf_, sizeof(Name##_init_) / sizeof(Name##_init_[0]), (Capacity))

// ============================================================================
// 8. STACK BYTES MACROS (Zero heap allocations, stack storage + FixedBytes view)
// ============================================================================

/**
 * @def DUO_STACK_BYTES(Name, Capacity)
 * @brief Allocates an uninitialized fixed-capacity buffer directly on the stack frame
 * and binds a duo::FixedBytes to it with initial size 0 and capacity Capacity.
 * @param Name Variable name for the created FixedBytes.
 * @param Capacity Compile-time byte capacity on the stack frame.
 */
#define DUO_STACK_BYTES(Name, Capacity) \
    uint8_t Name##_raw_buf_[(Capacity)]; \
    ::duo::FixedBytes Name(Name##_raw_buf_, (Capacity))

/**
 * @def DUO_STACK_BYTES_INIT(Name, ...)
 * @brief Allocates and initializes a contiguous byte buffer on the stack frame from varargs,
 * binding a duo::FixedBytes with size and capacity equal to element count.
 * @param Name Variable name for the created FixedBytes.
 * @param ... Initializer list elements.
 */
#define DUO_STACK_BYTES_INIT(Name, ...) \
    uint8_t Name##_raw_buf_[] = { __VA_ARGS__ }; \
    ::duo::FixedBytes Name(Name##_raw_buf_, sizeof(Name##_raw_buf_), sizeof(Name##_raw_buf_))

/**
 * @def DUO_STACK_BYTES_OF(Name, Capacity, ...)
 * @brief Allocates an explicit fixed-capacity buffer on the stack frame and initializes
 * initial elements from varargs, binding a duo::FixedBytes with capacity Capacity.
 * @param Name Variable name for the created FixedBytes.
 * @param Capacity Total byte capacity on the stack frame.
 * @param ... Initializer list elements.
 */
#define DUO_STACK_BYTES_OF(Name, Capacity, ...) \
    uint8_t Name##_raw_buf_[(Capacity)] = { __VA_ARGS__ }; \
    const uint8_t Name##_init_[] = { __VA_ARGS__ }; \
    ::duo::FixedBytes Name(Name##_raw_buf_, sizeof(Name##_init_), (Capacity))

// ============================================================================
// 9. STACK STRING MACROS (Zero heap allocations, stack storage + FixedString)
// ============================================================================

/**
 * @def DUO_STACK_STRING(Name, Capacity)
 * @brief Allocates an uninitialized fixed-capacity buffer directly on the stack frame
 * and binds a duo::FixedString to it with initial size 0 and capacity Capacity.
 * Allocates (Capacity + 1) bytes on the stack, ensuring space for the null terminator.
 * @param Name Variable name for the created FixedString.
 * @param Capacity Compile-time character capacity (excluding null terminator).
 */
#define DUO_STACK_STRING(Name, Capacity) \
    char Name##_raw_buf_[(Capacity) + 1] = { 0 }; \
    ::duo::FixedString Name(Name##_raw_buf_, (Capacity))

/**
 * @def DUO_STACK_STRING_INIT(Name, Str)
 * @brief Allocates and initializes a stack string from a string literal,
 * binding a duo::FixedString with size and capacity equal to the string length.
 * @param Name Variable name for the created FixedString.
 * @param Str String literal.
 */
#define DUO_STACK_STRING_INIT(Name, Str) \
    char Name##_raw_buf_[] = Str; \
    ::duo::FixedString Name(Name##_raw_buf_, sizeof(Name##_raw_buf_) - 1, sizeof(Name##_raw_buf_) - 1)

/**
 * @def DUO_STACK_STRING_OF(Name, Capacity, Str)
 * @brief Allocates an explicit fixed-capacity buffer on the stack frame and initializes
 * initial characters from a string literal, binding a duo::FixedString with capacity Capacity.
 * Allocates (Capacity + 1) bytes on the stack.
 * @param Name Variable name for the created FixedString.
 * @param Capacity Total character capacity on the stack frame (excluding null terminator).
 * @param Str String literal.
 */
#define DUO_STACK_STRING_OF(Name, Capacity, Str) \
    char Name##_raw_buf_[(Capacity) + 1] = Str; \
    ::duo::FixedString Name(Name##_raw_buf_, sizeof(Str) - 1, (Capacity))

#endif /* DUOSTL_LINEAR_HPP */

