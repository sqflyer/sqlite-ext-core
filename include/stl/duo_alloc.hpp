#ifndef DUOSTL_ALLOC_HPP
#define DUOSTL_ALLOC_HPP 1

/* ============================================================================
 * duo_alloc.hpp - Freestanding Memory Management, Object Lifecycle & Traits (C++17)
 *
 * Part of DuoSTL: Zero-dependency embedded container library for SQLite extensions
 * and high-performance native systems.
 *
 * Design Guarantees:
 *   - Freestanding C++17 (-nostdlib++) with zero standard library headers
 *   - No <new>, no <type_traits>, no <utility>, no <memory>
 *   - Zero C++ exceptions (-fno-exceptions)
 *   - Zero C++ Run-Time Type Information (-fno-rtti)
 *   - Clean tagged placement-new operator avoiding global namespace collisions
 *
 * Overview:
 *   This header implements the foundational C++ runtime primitives required
 *   by all DuoSTL containers (Vector, Array, String, HashMap, HashSet):
 *     1. Compile-time type traits and SFINAE primitives
 *     2. Rvalue move semantics, perfect forwarding, and swap
 *     3. Tagged placement-new and explicit object lifetime management
 *     4. Typed memory allocation/deallocation wrappers
 * ============================================================================ */

#include <stddef.h>
#include <stdint.h>

extern "C" {
    #include <string.h>
    #include "duo_alloc.h"
}

namespace duo {

// ============================================================================
// 1. FREESTANDING TYPE TRAITS & METAPROGRAMMING (NO <type_traits>)
// ============================================================================

/**
 * @struct remove_reference
 * @brief Strips top-level lvalue or rvalue references from type @p T.
 * @tparam T Type to transform.
 */
template <typename T> struct remove_reference      { using type = T; };
template <typename T> struct remove_reference<T&>  { using type = T; };
template <typename T> struct remove_reference<T&&> { using type = T; };

/**
 * @typedef remove_reference_t
 * @brief Helper alias for remove_reference<T>::type.
 */
template <typename T>
using remove_reference_t = typename remove_reference<T>::type;

/**
 * @struct remove_const
 * @brief Strips top-level const qualification from type @p T.
 * @tparam T Type to transform.
 */
template <typename T> struct remove_const          { using type = T; };
template <typename T> struct remove_const<const T> { using type = T; };

/**
 * @typedef remove_const_t
 * @brief Helper alias for remove_const<T>::type.
 */
template <typename T>
using remove_const_t = typename remove_const<T>::type;

/**
 * @struct enable_if
 * @brief SFINAE condition helper. Defines member @c type if @p B is true.
 * @tparam B Boolean condition.
 * @tparam T Resulting type when condition holds.
 */
template <bool B, typename T = void> struct enable_if {};
template <typename T> struct enable_if<true, T> { using type = T; };

/**
 * @typedef enable_if_t
 * @brief Helper alias for enable_if<B, T>::type.
 */
template <bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

/**
 * @typedef void_t
 * @brief Maps any sequence of types to void. Essential for SFINAE and trait detection.
 */
template <typename...>
using void_t = void;

/**
 * @brief Converts type T to a reference type, enabling unevaluated member access in decltype.
 */
template <typename T>
T&& declval() noexcept;

/** @brief Compile-time boolean integral constant wrapper for true. */
struct true_type { static constexpr bool value = true; };

/** @brief Compile-time boolean integral constant wrapper for false. */
struct false_type { static constexpr bool value = false; };

/**
 * @struct is_lvalue_reference
 * @brief Compile-time predicate testing if type @p T is an lvalue reference.
 */
template <typename T> struct is_lvalue_reference     : false_type {};
template <typename T> struct is_lvalue_reference<T&> : true_type {};

/**
 * @struct is_rvalue_reference
 * @brief Compile-time predicate testing if type @p T is an rvalue reference.
 */
template <typename T> struct is_rvalue_reference      : false_type {};
template <typename T> struct is_rvalue_reference<T&&> : true_type {};

/**
 * @struct is_same
 * @brief Compile-time predicate testing if types @p T and @p U are identical.
 */
template <typename T, typename U> struct is_same : false_type { using type = false_type; };
template <typename T> struct is_same<T, T>       : true_type  { using type = true_type; };

/**
 * @struct is_const
 * @brief Compile-time predicate testing if type @p T has top-level const qualification.
 */
template <typename T> struct is_const          : false_type { using type = false_type; };
template <typename T> struct is_const<const T> : true_type  { using type = true_type; };

/**
 * @struct conditional
 * @brief Selects type @p T if @p B is true, or type @p F if @p B is false.
 */
template <bool B, typename T, typename F> struct conditional { using type = T; };
template <typename T, typename F> struct conditional<false, T, F> { using type = F; };
template <bool B, typename T, typename F> using conditional_t = typename conditional<B, T, F>::type;

/** @brief Freestanding iterator tag markers. */
struct input_iterator_tag {};
struct forward_iterator_tag : input_iterator_tag {};
struct bidirectional_iterator_tag : forward_iterator_tag {};
struct random_access_iterator_tag : bidirectional_iterator_tag {};

/**
 * @struct is_trivially_copyable
 * @brief Checks if objects of type @p T can be safely copied via memcpy/memmove.
 * Uses compiler built-in __is_trivially_copyable without pulling in <type_traits>.
 */
template <typename T>
struct is_trivially_copyable {
    static constexpr bool value = __is_trivially_copyable(T);
};

/**
 * @struct is_standard_layout
 * @brief Checks if type @p T adheres to standard-layout rules for C-compatible ABI layouts.
 * Uses compiler built-in __is_standard_layout without pulling in <type_traits>.
 */
template <typename T>
struct is_standard_layout {
    static constexpr bool value = __is_standard_layout(T);
};

/**
 * @struct is_trivially_destructible
 * @brief Checks if type @p T has a trivial destructor.
 * Uses compiler built-in __is_trivially_destructible without pulling in <type_traits>.
 */
template <typename T>
struct is_trivially_destructible {
#if defined(__has_builtin)
  #if __has_builtin(__is_trivially_destructible)
    static constexpr bool value = __is_trivially_destructible(T);
  #else
    static constexpr bool value = __has_trivial_destructor(T);
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 5
    static constexpr bool value = __is_trivially_destructible(T);
#elif defined(_MSC_VER)
    static constexpr bool value = __is_trivially_destructible(T);
#else
    static constexpr bool value = __has_trivial_destructor(T);
#endif
};

// ============================================================================
// 2. MOVE SEMANTICS & PERFECT FORWARDING (NO <utility>)
// ============================================================================

/**
 * @brief Unconditionally casts an lvalue or rvalue reference to an rvalue reference.
 * Equivalent to std::move but implemented without <utility>.
 * @tparam T Type being moved.
 * @param t Object to cast.
 * @return Rvalue reference to @p t.
 */
template <typename T>
constexpr remove_reference_t<T>&& move(T&& t) noexcept {
    return static_cast<remove_reference_t<T>&&>(t);
}

/**
 * @brief Forwards an lvalue reference preserving value category.
 * Equivalent to std::forward but implemented without <utility>.
 * @tparam T Target forwarded type.
 * @param t Object reference.
 * @return Forwarded reference.
 */
template <typename T>
constexpr T&& forward(remove_reference_t<T>& t) noexcept {
    return static_cast<T&&>(t);
}

/**
 * @brief Forwards an rvalue reference preserving value category.
 * @tparam T Target forwarded type.
 * @param t Object reference.
 * @return Forwarded rvalue reference.
 */
template <typename T>
constexpr T&& forward(remove_reference_t<T>&& t) noexcept {
    return static_cast<T&&>(t);
}

/**
 * @brief Swaps the contents of two objects using move semantics.
 * @tparam T Object type supporting move construction and assignment.
 * @param a First object.
 * @param b Second object.
 */
template <typename T>
void swap(T& a, T& b) noexcept {
    T tmp = duo::move(a);
    a = duo::move(b);
    b = duo::move(tmp);
}

// ============================================================================
// 3. FREESTANDING PLACEMENT NEW & OBJECT LIFECYCLE (NO <new>)
// ============================================================================

/**
 * @struct duo_new_tag
 * @brief Tag type used to uniquely identify DuoSTL's freestanding placement-new operator,
 * eliminating collisions with standard <new> or global operator new overrides.
 */
struct duo_new_tag {};

} // namespace duo

/**
 * @brief Tagged placement-new operator for freestanding environments (-nostdlib++).
 * @param p Target memory address where the object will be constructed.
 * @return The unmodified memory pointer @p p.
 */
inline void* operator new(size_t, void* p, duo::duo_new_tag) noexcept {
    return p;
}

namespace duo {

/**
 * @brief Constructs an object of type @p T at memory location @p p with arguments @p args.
 * Equivalent to std::construct_at in C++20, but compatible with C++17 without <new>.
 * @tparam T Object type to construct.
 * @tparam Args Constructor parameter types.
 * @param p Target uninitialized memory pointer.
 * @param args Arguments forwarded to @p T's constructor.
 * @return Pointer to the newly constructed object.
 */
template <typename T, typename... Args>
inline T* construct_at(T* p, Args&&... args) {
    return new (p, duo_new_tag{}) T(duo::forward<Args>(args)...);
}

/**
 * @brief Explicitly destroys an object of type @p T by invoking its destructor.
 * Equivalent to std::destroy_at in C++17, but implemented without <memory>.
 * @tparam T Object type to destroy.
 * @param p Pointer to the object to destroy (safe if NULL).
 */
template <typename T>
inline void destroy_at(T* p) noexcept {
    if (p) {
        p->~T();
    }
}

/**
 * @brief Destroys an array of @p count objects in reverse order of construction.
 * Equivalent to std::destroy_n in C++17, but implemented without <memory>.
 * @tparam T Element type.
 * @param first Pointer to the first element in the contiguous range.
 * @param count Number of initialized elements to destroy.
 */
template <typename T>
inline void destroy_n(T* first, size_t count) noexcept {
    if (!first) return;
    for (size_t i = count; i > 0; --i) {
        destroy_at(&first[i - 1]);
    }
}

/**
 * @brief Relocates an object from @p src to uninitialized memory at @p dst.
 *
 * Employs hardware-vectorized memcpy for trivially copyable types;
 * otherwise move-constructs into @p dst and destroys the object at @p src.
 *
 * @tparam T Object type.
 * @param dst Pointer to uninitialized destination memory.
 * @param src Pointer to initialized source object.
 */
template <typename T>
inline void relocate_at(T* dst, T* src) noexcept {
    if constexpr (is_trivially_copyable<T>::value) {
        DUO_MEMCPY(dst, src, sizeof(T));
    } else {
        construct_at(dst, duo::move(*src));
        destroy_at(src);
    }
}

/**
 * @brief Relocates a contiguous sequence of @p count objects from @p src to uninitialized @p dst.
 *
 * Employs hardware-vectorized memmove for trivially copyable types;
 * otherwise performs safe ordered move-construction and destruction for non-trivial types
 * (handling potential buffer overlaps).
 *
 * @tparam T Element type.
 * @param dst Pointer to uninitialized destination buffer.
 * @param src Pointer to initialized source buffer.
 * @param count Number of elements to relocate.
 */
template <typename T>
inline void relocate_n(T* dst, T* src, size_t count) noexcept {
    if (!count) return;
    if constexpr (is_trivially_copyable<T>::value) {
        DUO_MEMMOVE(dst, src, count * sizeof(T));
    } else {
        if (dst < src) {
            for (size_t i = 0; i < count; ++i) {
                construct_at(&dst[i], duo::move(src[i]));
                destroy_at(&src[i]);
            }
        } else if (dst > src) {
            for (size_t i = count; i > 0; --i) {
                construct_at(&dst[i - 1], duo::move(src[i - 1]));
                destroy_at(&src[i - 1]);
            }
        }
    }
}

/**
 * @brief Constructs a copy of @p count elements from @p src into uninitialized memory @p dst.
 * Equivalent to std::uninitialized_copy_n in C++17.
 *
 * @tparam T Element type.
 * @param src Source buffer pointer.
 * @param count Number of elements to copy.
 * @param dst Destination uninitialized buffer pointer.
 */
template <typename T>
inline void uninitialized_copy_n(const T* src, size_t count, T* dst) {
    if (!count) return;
    if constexpr (is_trivially_copyable<T>::value) {
        DUO_MEMCPY(dst, src, count * sizeof(T));
    } else {
        for (size_t i = 0; i < count; ++i) {
            construct_at(&dst[i], src[i]);
        }
    }
}

/**
 * @brief Move-constructs @p count elements from @p src into uninitialized memory @p dst.
 * Equivalent to std::uninitialized_move_n in C++17.
 *
 * @tparam T Element type.
 * @param src Source buffer pointer.
 * @param count Number of elements to move.
 * @param dst Destination uninitialized buffer pointer.
 */
template <typename T>
inline void uninitialized_move_n(T* src, size_t count, T* dst) {
    if (!count) return;
    if constexpr (is_trivially_copyable<T>::value) {
        DUO_MEMCPY(dst, src, count * sizeof(T));
    } else {
        for (size_t i = 0; i < count; ++i) {
            construct_at(&dst[i], duo::move(src[i]));
        }
    }
}

// ============================================================================
// 4. FREESTANDING MEMORY ALLOCATOR WRAPPERS
// ============================================================================

/**
 * @brief Allocates dynamic heap memory via DuoSTL's configured memory engine.
 * @param bytes Number of bytes to allocate.
 * @return Pointer to allocated memory, or nullptr on failure.
 */
inline void* allocate(size_t bytes) noexcept {
    return DUO_MALLOC(bytes);
}

/**
 * @brief Resizes an existing heap allocation to a new capacity.
 * @param ptr Pointer to existing allocation (or nullptr to allocate fresh).
 * @param new_bytes New requested size in bytes.
 * @return Pointer to resized memory buffer, or nullptr on failure.
 */
inline void* reallocate(void* ptr, size_t new_bytes) noexcept {
    return DUO_REALLOC(ptr, new_bytes);
}

/**
 * @brief Frees dynamic memory allocated via duo::allocate or duo::reallocate.
 * @param ptr Pointer to memory block to release (safe if nullptr).
 */
inline void deallocate(void* ptr) noexcept {
    DUO_FREE(ptr);
}

// ============================================================================
// 5. UNIVERSAL DUAL-ABI C INTEROP FOUNDATION & REVERSE ITERATORS
// ============================================================================

/**
 * @class ReverseIterator
 * @brief Zero-dependency bidirectional reverse iterator wrapper.
 * @tparam Ptr Pointer or iterator type being adapted.
 */
template <typename Ptr>
class ReverseIterator {
    Ptr m_ptr;
public:
    inline explicit ReverseIterator(Ptr p) noexcept : m_ptr(p) {}
    inline auto operator*() const noexcept -> decltype(*m_ptr) { Ptr tmp = m_ptr; return *--tmp; }
    inline ReverseIterator& operator++() noexcept { --m_ptr; return *this; }
    inline ReverseIterator operator++(int) noexcept { ReverseIterator tmp = *this; --m_ptr; return tmp; }
    inline ReverseIterator& operator--() noexcept { ++m_ptr; return *this; }
    inline ReverseIterator operator--(int) noexcept { ReverseIterator tmp = *this; ++m_ptr; return tmp; }
    inline ReverseIterator operator+(ptrdiff_t n) const noexcept { return ReverseIterator(m_ptr - n); }
    inline ReverseIterator operator-(ptrdiff_t n) const noexcept { return ReverseIterator(m_ptr + n); }
    inline ReverseIterator& operator+=(ptrdiff_t n) noexcept { m_ptr -= n; return *this; }
    inline ReverseIterator& operator-=(ptrdiff_t n) noexcept { m_ptr += n; return *this; }
    inline bool operator==(const ReverseIterator& o) const noexcept { return m_ptr == o.m_ptr; }
    inline bool operator!=(const ReverseIterator& o) const noexcept { return !(*this == o); }
};

template <typename C, typename = void>
struct duo_c_reset_helper {
    static inline void apply(C& c) noexcept {
        c = C{};
    }
};

/**
 * @brief Generic zero-reset for standard-layout C mirror structs.
 * Specializable by individual containers with non-trivial default C state.
 * @tparam C Standard-layout C struct type.
 * @param c Reference to C struct to clear.
 */
template <typename C>
inline void duo_c_reset(C& c) noexcept {
    duo_c_reset_helper<C>::apply(c);
}

template <typename Container, typename Ptr, typename = void>
struct duo_container_adopt_raw_helper {
    static inline void apply(Container&, Ptr*, size_t, size_t) noexcept {}
};

/**
 * @brief Hook for raw pointer adoption into owning C++ containers.
 * Specialized by owning containers (Vector, Bytes, BitVec).
 */
template <typename Container, typename Ptr>
inline void duo_container_adopt_raw(Container& a, Ptr* data, size_t size, size_t capacity) noexcept {
    duo_container_adopt_raw_helper<Container, Ptr>::apply(a, data, size, capacity);
}

/**
 * @def DUO_CXX_FFI_OPS(InnerExpr)
 * @brief Injects mandatory Dual-ABI foreign function interface methods and operators
 * across all DuoSTL container classes (linear, bit, and associative).
 *
 * Mandated methods:
 *   - c_ptr() / c_ptr<CType>()
 *   - c_val() / c_val<CType>()
 *   - adopt()
 *   - release_c() / disown()
 *   - implicit conversions to C struct by value and pointer
 */
#define DUO_CXX_FFI_OPS(InnerExpr) \
    inline auto* c_ptr() noexcept { return &(InnerExpr); } \
    inline const auto* c_ptr() const noexcept { return &(InnerExpr); } \
    template <typename CType> \
    inline CType* c_ptr() noexcept { \
        return reinterpret_cast<CType*>(&(InnerExpr)); \
    } \
    template <typename CType> \
    inline const CType* c_ptr() const noexcept { \
        return reinterpret_cast<const CType*>(&(InnerExpr)); \
    } \
    inline auto& c_val() noexcept { return (InnerExpr); } \
    inline const auto& c_val() const noexcept { return (InnerExpr); } \
    template <typename CType> \
    inline CType c_val() const noexcept { \
        return *reinterpret_cast<const CType*>(&(InnerExpr)); \
    } \
    inline operator decltype(InnerExpr)() const noexcept { return (InnerExpr); } \
    inline operator decltype(&(InnerExpr))() noexcept { return &(InnerExpr); } \
    inline operator const decltype(InnerExpr)*() const noexcept { return &(InnerExpr); } \
    static inline self_type adopt(decltype(InnerExpr)& c) noexcept { \
        self_type a; \
        a.InnerExpr = c; \
        duo_c_reset(c); \
        return a; \
    } \
    template <typename Ptr> \
    static inline self_type adopt(Ptr* data, size_t size, size_t capacity = 0) noexcept { \
        self_type a; \
        duo_container_adopt_raw(a, data, size, capacity); \
        return a; \
    } \
    inline auto release_c() noexcept { \
        auto out = (InnerExpr); \
        duo_c_reset(InnerExpr); \
        return out; \
    } \
    inline auto disown() noexcept { \
        return release_c(); \
    }

} // namespace duo

#endif /* DUOSTL_ALLOC_HPP */
