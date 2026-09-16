#ifndef DUO_LINEAR_H
#define DUO_LINEAR_H

/* ============================================================================
 * duo_linear.h - Dual-ABI Linear Container Engine for Pure C (C11) and C++17
 *
 * Part of DuoSTL: Zero-dependency embedded container library for SQLite extensions
 * and high-performance native systems.
 *
 * This header provides the pure C foundation of DuoSTL's linear containers.
 * All structs are standard-layout, zero-overhead, and ABI-compatible with their
 * C++ counterparts defined in duo_linear.hpp.
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

#ifdef __cplusplus
extern "C" {
#endif


/* ============================================================================
 * 2. NON-OWNING GENERIC SPANS & VIEWS (C11 ABI)
 *
 * duo_span_t:      Non-owning mutable contiguous buffer slice { void* data; size_t size; }
 * duo_span_view_t: Non-owning const contiguous buffer slice   { const void* data; size_t size; }
 *
 * Both structs are exactly 16 bytes on 64-bit architectures, matching the memory
 * layout of duo::Span<T> and duo::SpanView<T> in C++.
 * ============================================================================ */

typedef struct {
    void*  data; /**< Pointer to the first element in the contiguous buffer. */
    size_t size; /**< Number of bytes in the buffer slice. */
} duo_span_t;

typedef struct {
    const void* data; /**< Pointer to the first element in the contiguous buffer. */
    size_t      size; /**< Number of bytes in the buffer slice. */
} duo_span_view_t;

DUO_STATIC_ASSERT(sizeof(duo_span_t) == 16, "duo_span_t must be exactly 16 bytes on 64-bit systems!");
DUO_STATIC_ASSERT(sizeof(duo_span_view_t) == 16, "duo_span_view_t must be exactly 16 bytes on 64-bit systems!");

/* ============================================================================
 * MACRO-SYNTHESIZED COMPARISON OPERATORS (C11)
 * ============================================================================ */
#ifndef DUO_C_DERIVE_CMP_OPS
/**
 * @def DUO_C_DERIVE_CMP_OPS(Prefix, Type, CmpFn)
 * @brief Generates full 6-way relational comparison suite (_eq, _ne, _lt, _le, _gt, _ge)
 * from a 3-way comparator function returning <0, 0, or >0.
 *
 * @param Prefix Function name prefix (e.g., duo_str_view -> duo_str_view_eq, etc.)
 * @param Type Parameter type (e.g., duo_str_view_t, const duo_string_t*)
 * @param CmpFn Comparator function with signature int (*)(Type, Type)
 */
#define DUO_C_DERIVE_CMP_OPS(Prefix, Type, CmpFn) \
    static inline bool Prefix##_eq(Type a, Type b) { return (CmpFn)(a, b) == 0; } \
    static inline bool Prefix##_ne(Type a, Type b) { return (CmpFn)(a, b) != 0; } \
    static inline bool Prefix##_lt(Type a, Type b) { return (CmpFn)(a, b) < 0; } \
    static inline bool Prefix##_le(Type a, Type b) { return (CmpFn)(a, b) <= 0; } \
    static inline bool Prefix##_gt(Type a, Type b) { return (CmpFn)(a, b) > 0; } \
    static inline bool Prefix##_ge(Type a, Type b) { return (CmpFn)(a, b) >= 0; }
#endif


/**
 * @brief Constructs a mutable duo_span_t slice.
 * @param data Pointer to the buffer memory.
 * @param size Total number of bytes in the slice.
 * @return Constructed duo_span_t instance.
 */
static inline duo_span_t duo_span_make(void* data, size_t size) {
    duo_span_t s;
    s.data = data;
    s.size = size;
    return s;
}

/**
 * @brief Constructs an immutable duo_span_view_t slice.
 * @param data Pointer to the buffer memory.
 * @param size Total number of bytes in the slice.
 * @return Constructed duo_span_view_t instance.
 */
static inline duo_span_view_t duo_span_view_make(const void* data, size_t size) {
    duo_span_view_t v;
    v.data = data;
    v.size = size;
    return v;
}

/**
 * @brief Demotes a mutable span into an immutable span view.
 * @param s Mutable span.
 * @return Equivalent duo_span_view_t view.
 */
static inline duo_span_view_t duo_span_to_view(duo_span_t s) {
    duo_span_view_t v;
    v.data = s.data;
    v.size = s.size;
    return v;
}

/**
 * @brief Obtains a subspan of a mutable span with bounds clamping.
 * @param s Parent span.
 * @param offset Byte offset from the beginning.
 * @param count Number of bytes requested (clamped to remaining capacity).
 * @return Sliced duo_span_t subspan.
 */
static inline duo_span_t duo_span_subspan(duo_span_t s, size_t offset, size_t count) {
    if (offset > s.size) {
        offset = s.size;
    }
    size_t avail = s.size - offset;
    if (count > avail) {
        count = avail;
    }
    duo_span_t res;
    res.data = (uint8_t*)s.data + offset;
    res.size = count;
    return res;
}

/**
 * @brief Obtains a subspan of an immutable view with bounds clamping.
 * @param s Parent view.
 * @param offset Byte offset from the beginning.
 * @param count Number of bytes requested (clamped to remaining capacity).
 * @return Sliced duo_span_view_t subview.
 */
static inline duo_span_view_t duo_span_view_subspan(duo_span_view_t s, size_t offset, size_t count) {
    if (offset > s.size) {
        offset = s.size;
    }
    size_t avail = s.size - offset;
    if (count > avail) {
        count = avail;
    }
    duo_span_view_t res;
    res.data = (const uint8_t*)s.data + offset;
    res.size = count;
    return res;
}

/**
 * @brief Copies bytes from source view @p src into destination span @p dst with bounds clamping.
 * Transfers min(dst.size, src.size) bytes.
 * @param dst Destination mutable span.
 * @param src Source immutable view.
 * @return Number of bytes copied.
 */
static inline size_t duo_span_copy(duo_span_t dst, duo_span_view_t src) {
    size_t n = (dst.size < src.size) ? dst.size : src.size;
    if (n > 0 && dst.data && src.data) {
        DUO_MEMCPY(dst.data, src.data, n);
    }
    return n;
}

/**
 * @brief Moves bytes from source span @p src into destination span @p dst.
 * Uses memmove to guarantee correctness even when buffer regions overlap.
 * Transfers min(dst.size, src.size) bytes.
 * @param dst Destination mutable span.
 * @param src Source mutable span.
 * @return Number of bytes moved.
 */
static inline size_t duo_span_move(duo_span_t dst, duo_span_t src) {
    size_t n = (dst.size < src.size) ? dst.size : src.size;
    if (n > 0 && dst.data && src.data) {
        DUO_MEMMOVE(dst.data, src.data, n);
    }
    return n;
}

/**
 * @brief Fills all bytes in span @p dst with byte value @p byte_val via memset.
 * @param dst Destination mutable span.
 * @param byte_val Byte value to set across the entire span.
 */
static inline void duo_span_fill_bytes(duo_span_t dst, uint8_t byte_val) {
    if (dst.data && dst.size > 0) {
        DUO_MEMSET(dst.data, (int)byte_val, dst.size);
    }
}

/**
 * @brief Zeroes out all bytes in span @p dst via memset.
 * @param dst Destination mutable span.
 */
static inline void duo_span_zero(duo_span_t dst) {
    duo_span_fill_bytes(dst, 0);
}

/**
 * @brief Ensures capacity of a dynamic vector buffer is at least @p min_cap elements.
 * Uses geometric doubling (starts at 4 if empty).
 * @param data_ptr Pointer to the vector's buffer pointer (void**).
 * @param cap_ptr Pointer to current capacity (size_t*).
 * @param min_cap Minimum required capacity.
 * @param elem_size Size of a single element in bytes.
 * @return 1 on success, 0 on allocation failure.
 */
static inline bool duo_vec_reserve_impl(void** data_ptr, size_t* cap_ptr, size_t min_cap, size_t elem_size) {
    if (min_cap <= *cap_ptr) {
        return true;
    }
    size_t next_cap = duo_geometric_grow_cap(*cap_ptr);
    size_t new_cap = (next_cap > min_cap) ? next_cap : min_cap;
    if (elem_size > 0 && new_cap > ((size_t)-1) / elem_size) {
        return false;
    }
    void* new_data = DUO_REALLOC(*data_ptr, new_cap * elem_size);
    if (!new_data) {
        return false;
    }
    *data_ptr = new_data;
    *cap_ptr = new_cap;
    return true;
}

/**
 * @brief Shrinks the capacity of a vector buffer to match its current element count.
 * If size is 0, the buffer is freed and capacity reset to 0.
 * @param data_ptr Pointer to the vector's buffer pointer (void**).
 * @param cap_ptr Pointer to current capacity (size_t*).
 * @param size Current element count.
 * @param elem_size Size of a single element in bytes.
 * @return true on success, false on failure.
 */
static inline bool duo_vec_shrink_to_fit_impl(void** data_ptr, size_t* cap_ptr, size_t size, size_t elem_size) {
    if (*cap_ptr == size) {
        return true;
    }
    if (size == 0) {
        if (*data_ptr) {
            DUO_FREE(*data_ptr);
            *data_ptr = NULL;
        }
        *cap_ptr = 0;
        return true;
    }
    void* new_data = DUO_REALLOC(*data_ptr, size * elem_size);
    if (!new_data) {
        return false;
    }
    *data_ptr = new_data;
    *cap_ptr = size;
    return true;
}

/**
 * @brief Bounds check helper for insertion operations (idx <= size).
 * @param idx Index to check.
 * @param size Container element count.
 * @return true if idx <= size, false otherwise.
 */
static inline bool duo_valid_insert_idx(size_t idx, size_t size) {
    return idx <= size;
}

/**
 * @brief Bounds check helper for erase operations (idx < size).
 * @param idx Index to check.
 * @param size Container element count.
 * @return true if idx < size, false otherwise.
 */
static inline bool duo_valid_erase_idx(size_t idx, size_t size) {
    return idx < size;
}

/**
 * @brief Replicates the first element across the remainder of the buffer slice.
 * @param elem_ptr Pointer to initial element to duplicate.
 * @param elem_size Size in bytes of a single element.
 * @param count Total number of replicas including the original.
 */
static inline void duo_replicate_elem(void* elem_ptr, size_t elem_size, size_t count) {
    for (size_t i = 1; i < count; ++i) {
        DUO_MEMCPY((char*)elem_ptr + i * elem_size, elem_ptr, elem_size);
    }
}

/* ============================================================================
 * 3. PURE C TYPED SPAN GENERATORS (Guarded from C++)
 * ============================================================================ */
#ifndef __cplusplus

/**
 * @brief Generates a typed mutable span type: { T* data; size_t size; }
 * Exactly matches sizeof and memory layout of C++ duo::Span<T>.
 */
#define DUO_C_SPAN_TYPE(T, Name) \
    typedef struct { T* data; size_t size; } Name; \
    typedef T Name##_basetype

/**
 * @brief Generates a typed immutable span view type: { const T* data; size_t size; }
 * Exactly matches sizeof and memory layout of C++ duo::SpanView<T>.
 */
#define DUO_C_SPAN_VIEW_TYPE(T, Name) \
    typedef struct { const T* data; size_t size; } Name; \
    typedef T Name##_basetype

/**
 * @brief Generates a typed heap array type: { size_t size; T* data; }
 * Exactly matches sizeof (16 bytes) and memory layout of C++ duo::Array<T>.
 */
#define DUO_C_ARRAY_TYPE(T, Name) \
    typedef struct { size_t size; T* data; } Name; \
    typedef T Name##_basetype

/**
 * @brief Generates a typed fixed array type: { T* data; size_t size; }
 * Semantic alias of DUO_C_SPAN_TYPE, exactly matching C++ duo::FixedArray<T>.
 */
#define DUO_C_FIXED_ARRAY_TYPE(T, Name) \
    DUO_C_SPAN_TYPE(T, Name)

/**
 * @brief Generates a typed dynamic vector type: { size_t size; size_t capacity; T* data; }
 * Exactly matches sizeof (24 bytes on 64-bit) and memory layout of C++ duo::Vector<T>.
 */
#define DUO_C_VEC_TYPE(T, Name) \
    typedef struct { size_t size; size_t capacity; T* data; } Name; \
    typedef T Name##_basetype

/**
 * @brief Generates a typed fixed stack vector type: { size_t size; size_t capacity; T* data; }
 * Semantic alias of DUO_C_VEC_TYPE, exactly matching C++ duo::FixedVector<T>.
 */
#define DUO_C_FIXED_VEC_TYPE(T, Name) \
    DUO_C_VEC_TYPE(T, Name)

#endif /* !__cplusplus */

/* ============================================================================
 * 4. TYPED SPAN BORROW & CONVERSION MACROS (Available in both C and C++)
 * ============================================================================ */

/**
 * @brief Constructs a typed mutable span from any container pointer having .data and .size.
 * @param SpanType The target typed span type (e.g., int_span_t)
 * @param c Pointer to the vector or array container
 */
#define duo_as_span(SpanType, c) \
    ((SpanType){ (c)->data, (c)->size })

/**
 * @brief Constructs a typed immutable span view from any container pointer having .data and .size.
 * @param SpanViewType The target typed view type (e.g., int_span_view_t)
 * @param c Pointer to the vector or array container
 */
#define duo_as_view(SpanViewType, c) \
    ((SpanViewType){ (c)->data, (c)->size })

/**
 * @brief Constructs a typed mutable span from a raw buffer pointer and element count.
 */
#define duo_span_from(SpanType, ptr, count) \
    ((SpanType){ (ptr), (count) })

/**
 * @brief Constructs a typed immutable span view from a raw buffer pointer and element count.
 */
#define duo_view_from(SpanViewType, ptr, count) \
    ((SpanViewType){ (ptr), (count) })

/**
 * @brief Converts a typed mutable span into a typed immutable span view.
 */
#define duo_span_to_typed_view(SpanViewType, span) \
    ((SpanViewType){ (span).data, (span).size })

/**
 * @brief Bulk copies elements from typed container @p src into typed container @p dst with bounds clamping.
 * Automatically computes element size from dst pointer.
 * Returns the number of elements copied.
 */
#define duo_copy(dst, src) \
    (duo_span_copy( \
        (duo_span_t){ (dst)->data, (dst)->size * sizeof(*(dst)->data) }, \
        (duo_span_view_t){ (src)->data, (src)->size * sizeof(*(src)->data) } \
    ) / sizeof(*(dst)->data))

/**
 * @brief Bulk moves elements from typed container @p src into typed container @p dst.
 * Safe for overlapping buffers via memmove. Returns the number of elements moved.
 */
#define duo_move(dst, src) \
    (duo_span_move( \
        (duo_span_t){ (dst)->data, (dst)->size * sizeof(*(dst)->data) }, \
        (duo_span_t){ (src)->data, (src)->size * sizeof(*(src)->data) } \
    ) / sizeof(*(dst)->data))

/**
 * @brief Fills all elements in typed container @p dst with value @p val.
 */
#define duo_fill(dst, val) \
    do { \
        for (size_t _i = 0; _i < (dst)->size; ++_i) { \
            (dst)->data[_i] = (val); \
        } \
    } while(0)

/**
 * @brief Zeroes out all memory in typed container @p dst.
 */
#define duo_zero(dst) \
    duo_span_zero((duo_span_t){ (dst)->data, (dst)->size * sizeof(*(dst)->data) })

/**
 * @brief Lexicographically compares two immutable byte span views.
 * @param a First span view.
 * @param b Second span view.
 * @return Negative if a < b, positive if a > b, 0 if identical.
 */
static inline int duo_span_view_cmp(duo_span_view_t a, duo_span_view_t b) {
    size_t min_len = a.size < b.size ? a.size : b.size;
    if (min_len > 0 && a.data && b.data) {
        int r = memcmp(a.data, b.data, min_len);
        if (r != 0) return r;
    }
    return (a.size < b.size) ? -1 : ((a.size > b.size) ? 1 : 0);
}

/**
 * @brief Lexicographically compares two mutable byte spans.
 */
static inline int duo_span_cmp(duo_span_t a, duo_span_t b) {
    return duo_span_view_cmp(duo_span_to_view(a), duo_span_to_view(b));
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_span_view_t:
 * duo_span_view_eq, duo_span_view_ne, duo_span_view_lt, duo_span_view_le,
 * duo_span_view_gt, duo_span_view_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_span_view, duo_span_view_t, duo_span_view_cmp)

/**
 * @brief Synthesized 6-way relational comparison operations for duo_span_t:
 * duo_span_eq, duo_span_ne, duo_span_lt, duo_span_le,
 * duo_span_gt, duo_span_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_span, duo_span_t, duo_span_cmp)

/**
 * @brief Computes 64-bit xxHash3 digest of byte span view @p v.
 */
static inline uint64_t duo_span_view_hash(duo_span_view_t v) {
    return duo_hash_xxhash3(v.data, v.size, 0, 0);
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of byte span view @p v.
 */
static inline uint64_t duo_span_view_hash_seed(duo_span_view_t v, uint64_t seed0, uint64_t seed1) {
    return duo_hash_xxhash3(v.data, v.size, seed0, seed1);
}

/**
 * @brief Computes 64-bit xxHash3 digest of byte span @p s.
 */
static inline uint64_t duo_span_hash(duo_span_t s) {
    return duo_span_view_hash(duo_span_to_view(s));
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of byte span @p s.
 */
static inline uint64_t duo_span_hash_seed(duo_span_t s, uint64_t seed0, uint64_t seed1) {
    return duo_span_view_hash_seed(duo_span_to_view(s), seed0, seed1);
}

/* ============================================================================
 * 5. PURE C HEAP ARRAY & FIXED ARRAY MACROS (Available in both C and C++)
 * ============================================================================ */

/**
 * @brief Initializes a heap array container to empty (size = 0, data = NULL).
 */
#define duo_array_init(a) \
    do { (a)->size = 0; (a)->data = NULL; } while(0)

/**
 * @brief Allocates heap memory for @p n elements in array @p a.
 * @param a Pointer to typed array container.
 * @param n Number of elements to allocate.
 * @return true on success, false on allocation failure.
 */
#ifdef __cplusplus
#define duo_array_alloc(a, n) \
    (((n) == 0) ? \
        ((a)->size = 0, (a)->data = nullptr, true) : \
        (((a)->data = static_cast<decltype((a)->data + 0)>(DUO_MALLOC((n) * sizeof(*(a)->data)))) != nullptr ? \
            ((a)->size = (n), true) : \
            ((a)->size = 0, false)))
#else
#define duo_array_alloc(a, n) \
    (((n) == 0) ? \
        ((a)->size = 0, (a)->data = NULL, true) : \
        (((a)->data = DUO_MALLOC((n) * sizeof(*(a)->data))) != NULL ? \
            ((a)->size = (n), true) : \
            ((a)->size = 0, false)))
#endif

/**
 * @brief Frees heap memory allocated for array @p a and reinitializes it to empty.
 */
#define duo_array_destroy(a) \
    do { \
        if ((a)->data) { \
            DUO_FREE((a)->data); \
        } \
        duo_array_init(a); \
    } while(0)

/**
 * @brief Invokes a custom destructor @p dtor on all elements of array @p a before freeing.
 * @param a Pointer to typed array container.
 * @param dtor Destructor callback: void (*)(T*)
 */
#define duo_array_destroy_dtor(a, dtor) \
    do { \
        if ((a)->data) { \
            for (size_t _i = 0; _i < (a)->size; ++_i) { \
                dtor(&(a)->data[_i]); \
            } \
            DUO_FREE((a)->data); \
        } \
        duo_array_init(a); \
    } while(0)

/**
 * @brief Borrows a typed mutable span from an array container pointer.
 */
#define duo_array_as_span(SpanType, a) \
    duo_as_span(SpanType, a)

/**
 * @brief Borrows a typed immutable span view from an array container pointer.
 */
#define duo_array_as_view(SpanViewType, a) \
    duo_as_view(SpanViewType, a)

/* FixedArray pure C aliases (FixedArray is a semantic alias of Span) */
#define duo_fixed_array_from(SpanType, ptr, count) duo_span_from(SpanType, ptr, count)
#define duo_fixed_array_as_span(SpanType, c)       duo_as_span(SpanType, c)
#define duo_fixed_array_as_view(SpanViewType, c)   duo_as_view(SpanViewType, c)
#define duo_fixed_array_init(a, buf, cap)          do { (a)->data = (buf); (a)->size = (cap); } while(0)

#ifndef __cplusplus
/**
 * @brief Allocates an uninitialized fixed-capacity buffer on the C stack and binds it to a typed fixed array / span.
 * @param ArrayType Typed fixed array / span type defined via DUO_C_SPAN_TYPE or DUO_C_FIXED_ARRAY_TYPE
 * @param Name Identifier of the created array variable
 * @param Capacity Compile-time element count to allocate on the stack
 */
#define DUO_C_STACK_ARRAY(ArrayType, Name, Capacity) \
    ArrayType##_basetype Name##_raw_buf_[(Capacity)]; \
    ArrayType Name = duo_span_from(ArrayType, Name##_raw_buf_, (Capacity))

/**
 * @brief Allocates and initializes a stack array from a variable list of arguments, automatically inferring size.
 * @param ArrayType Typed fixed array / span type defined via DUO_C_SPAN_TYPE or DUO_C_FIXED_ARRAY_TYPE
 * @param Name Identifier of the created array variable
 * @param ... Initializer elements (e.g. 10, 20, 30)
 */
#define DUO_C_STACK_ARRAY_INIT(ArrayType, Name, ...) \
    ArrayType##_basetype Name##_raw_buf_[] = { __VA_ARGS__ }; \
    ArrayType Name = duo_span_from(ArrayType, Name##_raw_buf_, sizeof(Name##_raw_buf_) / sizeof(Name##_raw_buf_[0]))

/**
 * @brief Allocates an explicit fixed-capacity stack buffer and initializes elements from varargs.
 * @param ArrayType Typed fixed array / span type
 * @param Name Identifier of the created array variable
 * @param Capacity Total compile-time capacity on the stack
 * @param ... Initializer elements
 */
#define DUO_C_STACK_ARRAY_OF(ArrayType, Name, Capacity, ...) \
    ArrayType##_basetype Name##_raw_buf_[(Capacity)] = { __VA_ARGS__ }; \
    ArrayType Name = duo_span_from(ArrayType, Name##_raw_buf_, (Capacity))

/**
 * @brief Allocates an uninitialized fixed-capacity buffer on the C stack frame
 * and binds a typed fixed vector to it with initial size 0 and capacity Capacity.
 * @param VecType Typed vector type defined via DUO_C_VEC_TYPE or DUO_C_FIXED_VEC_TYPE
 * @param Name Identifier of the created vector variable
 * @param Capacity Compile-time element count to allocate on the stack
 */
#define DUO_C_STACK_VECTOR(VecType, Name, Capacity) \
    VecType##_basetype Name##_raw_buf_[(Capacity)]; \
    VecType Name = (VecType){ 0, (Capacity), Name##_raw_buf_ }

/**
 * @brief Allocates and initializes a stack vector from a variable list of arguments,
 * with size and capacity equal to the number of elements.
 * @param VecType Typed vector type defined via DUO_C_VEC_TYPE or DUO_C_FIXED_VEC_TYPE
 * @param Name Identifier of the created vector variable
 * @param ... Initializer elements
 */
#define DUO_C_STACK_VECTOR_INIT(VecType, Name, ...) \
    VecType##_basetype Name##_raw_buf_[] = { __VA_ARGS__ }; \
    VecType Name = (VecType){ \
        sizeof(Name##_raw_buf_) / sizeof(Name##_raw_buf_[0]), \
        sizeof(Name##_raw_buf_) / sizeof(Name##_raw_buf_[0]), \
        Name##_raw_buf_ \
    }

/**
 * @brief Allocates an explicit fixed-capacity stack buffer and initializes elements from varargs.
 * size is set to the number of varargs elements, capacity is set to Capacity.
 * @param VecType Typed vector type defined via DUO_C_VEC_TYPE or DUO_C_FIXED_VEC_TYPE
 * @param Name Identifier of the created vector variable
 * @param Capacity Total compile-time capacity on the stack
 * @param ... Initializer elements
 */
#define DUO_C_STACK_VECTOR_OF(VecType, Name, Capacity, ...) \
    VecType##_basetype Name##_raw_buf_[(Capacity)] = { __VA_ARGS__ }; \
    VecType Name = (VecType){ \
        sizeof((VecType##_basetype[]){ __VA_ARGS__ }) / sizeof(VecType##_basetype), \
        (Capacity), \
        Name##_raw_buf_ \
    }
#endif /* !__cplusplus */

/* ============================================================================
 * 6. PURE C DYNAMIC VECTOR MACROS (Available in both C and C++)
 * ============================================================================ */

/**
 * @brief Initializes a vector container to empty (size = 0, capacity = 0, data = NULL).
 */
#define duo_vec_init(v) \
    do { (v)->size = 0; (v)->capacity = 0; (v)->data = NULL; } while(0)

/**
 * @brief Reserves memory capacity for at least @p n elements in vector @p v.
 * Returns 1 on success, 0 on failure.
 */
#define duo_vec_reserve(v, n) \
    duo_vec_reserve_impl((void**)(void*)&(v)->data, &(v)->capacity, (n), sizeof(*(v)->data))

/**
 * @brief Shrinks the capacity of vector @p v to match its current size.
 * Returns 1 on success, 0 on failure.
 */
#define duo_vec_shrink_to_fit(v) \
    duo_vec_shrink_to_fit_impl((void**)(void*)&(v)->data, &(v)->capacity, (v)->size, sizeof(*(v)->data))

/**
 * @brief Appends an element @p val to the end of vector @p v, growing geometrically if needed.
 * Returns true on success, or false on allocation failure.
 */
#define duo_vec_push_back(v, val) \
    (((v)->size < (v)->capacity || duo_vec_reserve((v), (v)->size + 1)) ? \
        ((v)->data[(v)->size++] = (val), true) : false)

/**
 * @brief Semantic alias for duo_vec_push_back.
 */
#define duo_vec_try_push_back(v, val) \
    duo_vec_push_back(v, val)

/**
 * @brief Removes the last element from vector @p v.
 * Returns true on success, or false if vector was already empty.
 */
#define duo_vec_pop_back(v) \
    (((v)->size > 0) ? (--(v)->size, true) : false)

/**
 * @brief Semantic alias for duo_vec_pop_back.
 */
#define duo_vec_try_pop_back(v) \
    duo_vec_pop_back(v)

/**
 * @brief Invokes destructor callback @p dtor on the last element, then removes it from vector @p v.
 * Returns true on success, or false if vector was already empty.
 */
#define duo_vec_pop_back_dtor(v, dtor) \
    (((v)->size > 0) ? (--(v)->size, dtor(&(v)->data[(v)->size]), true) : false)

/**
 * @brief Inserts an element @p val at index @p idx, shifting subsequent elements right.
 * Returns true on success, or false if idx > size or allocation failed.
 */
#define duo_vec_insert(v, idx, val) \
    ((duo_valid_insert_idx((idx), (v)->size) && ((v)->size < (v)->capacity || duo_vec_reserve((v), (v)->size + 1))) ? \
        (DUO_MEMMOVE(&(v)->data[(idx) + 1], &(v)->data[idx], ((v)->size - (idx)) * sizeof(*(v)->data)), \
         (v)->data[idx] = (val), \
         ++(v)->size, \
         true) : false)

/**
 * @brief Semantic alias for duo_vec_insert.
 */
#define duo_vec_try_insert(v, idx, val) \
    duo_vec_insert(v, idx, val)

/**
 * @brief Erases the element at index @p idx, shifting subsequent elements left.
 * Returns true on success, or false if idx >= size.
 */
#define duo_vec_erase(v, idx) \
    ((duo_valid_erase_idx((idx), (v)->size)) ? \
        (DUO_MEMMOVE(&(v)->data[idx], &(v)->data[(idx) + 1], ((v)->size - 1 - (idx)) * sizeof(*(v)->data)), \
         --(v)->size, \
         true) : false)

/**
 * @brief Semantic alias for duo_vec_erase.
 */
#define duo_vec_try_erase(v, idx) \
    duo_vec_erase(v, idx)

/**
 * @brief Invokes destructor callback @p dtor on element at @p idx, then erases it.
 * Returns true on success, or false if idx >= size.
 */
#define duo_vec_erase_dtor(v, idx, dtor) \
    ((duo_valid_erase_idx((idx), (v)->size)) ? \
        (dtor(&(v)->data[idx]), \
         DUO_MEMMOVE(&(v)->data[idx], &(v)->data[(idx) + 1], ((v)->size - 1 - (idx)) * sizeof(*(v)->data)), \
         --(v)->size, \
         true) : false)

/**
 * @brief Resets vector size to 0 without freeing allocated memory.
 */
#define duo_vec_clear(v) \
    do { (v)->size = 0; } while(0)

/**
 * @brief Invokes destructor callback @p dtor on all elements, then resets size to 0.
 */
#define duo_vec_clear_dtor(v, dtor) \
    do { \
        for (size_t _i = 0; _i < (v)->size; ++_i) { \
            dtor(&(v)->data[_i]); \
        } \
        (v)->size = 0; \
    } while(0)

/**
 * @brief Resizes vector @p v to @p new_size elements. If growing, new elements are initialized with @p val.
 * Returns true on success, or false on allocation failure.
 */
#define duo_vec_resize(v, new_size, val) \
    (((new_size) <= (v)->size) ? \
        ((v)->size = (new_size), true) : \
        (duo_vec_reserve((v), (new_size)) ? \
            ((v)->data[(v)->size] = (val), \
             duo_replicate_elem(&(v)->data[(v)->size], sizeof(*(v)->data), (new_size) - (v)->size), \
             (v)->size = (new_size), \
             true) : false))

/**
 * @brief Frees allocated heap memory for vector @p v and reinitializes it to empty.
 */
#define duo_vec_destroy(v) \
    do { \
        if ((v)->data) { \
            DUO_FREE((v)->data); \
        } \
        duo_vec_init(v); \
    } while(0)

/**
 * @brief Invokes destructor callback @p dtor on all elements before freeing heap memory.
 */
#define duo_vec_destroy_dtor(v, dtor) \
    do { \
        if ((v)->data) { \
            for (size_t _i = 0; _i < (v)->size; ++_i) { \
                dtor(&(v)->data[_i]); \
            } \
            DUO_FREE((v)->data); \
        } \
        duo_vec_init(v); \
    } while(0)

/**
 * @brief Borrows a typed mutable span from a vector container pointer.
 */
#define duo_vec_as_span(SpanType, v) \
    duo_as_span(SpanType, v)

/**
 * @brief Borrows a typed immutable span view from a vector container pointer.
 */
#define duo_vec_as_view(SpanViewType, v) \
    duo_as_view(SpanViewType, v)

/* ============================================================================
 * 7. PURE C STACK / FIXED VECTOR MACROS (Available in both C and C++)
 * ============================================================================ */

/**
 * @brief Initializes a fixed/stack vector by binding an external contiguous buffer.
 * size is initialized to 0, capacity to @p cap.
 */
#define duo_fixed_vec_init(v, buf, cap) \
    do { (v)->size = 0; (v)->capacity = (cap); (v)->data = (buf); } while(0)

/**
 * @brief Initializes a fixed/stack vector by binding an external contiguous buffer
 * with pre-existing element count @p sz and capacity @p cap.
 */
#define duo_fixed_vec_init_from(v, buf, sz, cap) \
    do { (v)->size = (sz); (v)->capacity = (cap); (v)->data = (buf); } while(0)

/**
 * @brief Appends element @p val to fixed vector @p v if within capacity.
 * Returns true on success, or false if capacity is full.
 */
#define duo_fixed_vec_push_back(v, val) \
    (((v)->size < (v)->capacity) ? ((v)->data[(v)->size++] = (val), true) : false)

/**
 * @brief Semantic alias for duo_fixed_vec_push_back.
 */
#define duo_fixed_vec_try_push_back(v, val) \
    duo_fixed_vec_push_back(v, val)

/**
 * @brief Removes the last element from fixed vector @p v.
 * Returns true on success, or false if vector was already empty.
 */
#define duo_fixed_vec_pop_back(v) \
    (((v)->size > 0) ? (--(v)->size, true) : false)

/**
 * @brief Semantic alias for duo_fixed_vec_pop_back.
 */
#define duo_fixed_vec_try_pop_back(v) \
    duo_fixed_vec_pop_back(v)

/**
 * @brief Invokes destructor callback @p dtor on the last element, then removes it.
 * Returns true on success, or false if vector was already empty.
 */
#define duo_fixed_vec_pop_back_dtor(v, dtor) \
    (((v)->size > 0) ? (--(v)->size, dtor(&(v)->data[(v)->size]), true) : false)

/**
 * @brief Inserts element @p val at index @p idx if within capacity, shifting subsequent elements right.
 * Returns true on success, or false if vector is full or idx > size.
 */
#define duo_fixed_vec_insert(v, idx, val) \
    ((duo_valid_insert_idx((idx), (v)->size) && (v)->size < (v)->capacity) ? \
        (DUO_MEMMOVE(&(v)->data[(idx) + 1], &(v)->data[idx], ((v)->size - (idx)) * sizeof(*(v)->data)), \
         (v)->data[idx] = (val), \
         ++(v)->size, \
         true) : false)

/**
 * @brief Semantic alias for duo_fixed_vec_insert.
 */
#define duo_fixed_vec_try_insert(v, idx, val) \
    duo_fixed_vec_insert(v, idx, val)

/**
 * @brief Erases the element at index @p idx, shifting subsequent elements left.
 * Returns true on success, or false if idx >= size.
 */
#define duo_fixed_vec_erase(v, idx) \
    ((duo_valid_erase_idx((idx), (v)->size)) ? \
        (DUO_MEMMOVE(&(v)->data[idx], &(v)->data[(idx) + 1], ((v)->size - 1 - (idx)) * sizeof(*(v)->data)), \
         --(v)->size, \
         true) : false)

/**
 * @brief Semantic alias for duo_fixed_vec_erase.
 */
#define duo_fixed_vec_try_erase(v, idx) \
    duo_fixed_vec_erase(v, idx)

/**
 * @brief Invokes destructor callback @p dtor on element at @p idx, then erases it.
 * Returns true on success, or false if idx >= size.
 */
#define duo_fixed_vec_erase_dtor(v, idx, dtor) \
    ((duo_valid_erase_idx((idx), (v)->size)) ? \
        (dtor(&(v)->data[idx]), \
         DUO_MEMMOVE(&(v)->data[idx], &(v)->data[(idx) + 1], ((v)->size - 1 - (idx)) * sizeof(*(v)->data)), \
         --(v)->size, \
         true) : false)

/**
 * @brief Resets fixed vector size to 0 without freeing memory or touching capacity/data.
 */
#define duo_fixed_vec_clear(v) \
    duo_vec_clear(v)

/**
 * @brief Invokes destructor callback @p dtor on all elements, then resets size to 0.
 */
#define duo_fixed_vec_clear_dtor(v, dtor) \
    duo_vec_clear_dtor(v, dtor)

/**
 * @brief Resizes fixed vector @p v up to capacity. If growing, new elements are set to @p val.
 * Returns true on success, or false if new_size > capacity.
 */
#define duo_fixed_vec_resize(v, new_size, val) \
    (((new_size) > (v)->capacity) ? false : \
     ((new_size) <= (v)->size) ? \
        ((v)->size = (new_size), true) : \
        ((v)->data[(v)->size] = (val), \
         duo_replicate_elem(&(v)->data[(v)->size], sizeof(*(v)->data), (new_size) - (v)->size), \
         (v)->size = (new_size), \
         true))

/**
 * @brief Resizes fixed vector @p v. If shrinking, invokes destructor callback @p dtor on removed elements.
 */
#define duo_fixed_vec_resize_dtor(v, new_size, val, dtor) \
    do { \
        size_t _ns = (new_size); \
        if (_ns > (v)->capacity) { \
            _ns = (v)->capacity; \
        } \
        if (_ns < (v)->size) { \
            for (size_t _i = _ns; _i < (v)->size; ++_i) { \
                dtor(&(v)->data[_i]); \
            } \
            (v)->size = _ns; \
        } else { \
            while ((v)->size < _ns) { \
                (v)->data[(v)->size++] = (val); \
            } \
        } \
    } while(0)

/**
 * @brief Resets fixed vector size to 0. Zero heap operations (safe for stack buffers).
 */
#define duo_fixed_vec_destroy(v) \
    do { (v)->size = 0; } while(0)

/**
 * @brief Invokes destructor callback @p dtor on all elements and resets size to 0.
 * Zero heap operations.
 */
#define duo_fixed_vec_destroy_dtor(v, dtor) \
    duo_vec_clear_dtor(v, dtor)

/**
 * @brief Borrows a typed mutable span from a fixed vector pointer.
 */
#define duo_fixed_vec_as_span(SpanType, v) \
    duo_as_span(SpanType, v)

/**
 * @brief Borrows a typed immutable span view from a fixed vector pointer.
 */
#define duo_fixed_vec_as_view(SpanViewType, v) \
    duo_as_view(SpanViewType, v)

/**
 * @struct duo_fixed_vec
 * @brief Standard-layout C mirror struct representing a fixed stack or heap-backed vector.
 * Binary-compatible with C++ duo::FixedVector<T> and c_fixed_vec_t<T>
 * ({ size_t size; size_t capacity; void* data; }, exactly 24 bytes on 64-bit platforms).
 */
typedef struct duo_fixed_vec {
    size_t size;     /**< Current number of valid elements. */
    size_t capacity; /**< Total element capacity. */
    void*  data;     /**< Contiguous buffer pointer. */
} duo_fixed_vec_t;

DUO_STATIC_ASSERT(sizeof(duo_fixed_vec_t) == 24, "duo_fixed_vec_t must be exactly 24 bytes on 64-bit systems!");

/**
 * @brief Accesses element at @p idx in generic fixed vector @p v cast to @p Type.
 */
#define duo_fixed_vec_elem(v, Type, idx) (((Type*)(v)->data)[(idx)])

/**
 * @brief Returns typed pointer to contiguous buffer of generic fixed vector @p v.
 */
#define duo_fixed_vec_typed_data(v, Type) ((Type*)(v)->data)

/**
 * @brief Computes the fine-grained stack tier capacity in bytes (up to 1024B) via bit manipulation
 * with ~1.25x growth increments (sub-power-of-two 25% steps), or 0 if capacity exceeds 1024B.
 *
 * @details
 * Instead of an expensive if-else ladder (which incurs branch misprediction penalties on runtime
 * variable inputs and L1 instruction cache bloat), this function uses branchless bit-twiddling:
 *
 * 1. Early Guards:
 *    - Clamps cap <= 8 to the minimum tier (8 bytes).
 *    - Rejects cap > 1024 (returns 0) to trigger dynamic heap fallback.
 *
 * 2. Power-of-Two Floor via Bit-Smearing:
 *    - Decrementing (cap - 1) and smearing the highest set bit downwards across 1, 2, 4, 8 positions
 *      produces an all-ones mask 2^(k+1) - 1 for any cap in (2^k, 2^(k+1)].
 *    - Halving and adding 1 (`(x >> 1) + 1`) extracts B = 2^k, the base power-of-two floor.
 *    - Clamping B to minimum 32 ensures sub-64B tiers use uniform 8-byte increments.
 *
 * 3. Sub-Octave Stepping (25% Growth / ~1.25x Multiplier):
 *    - Each power-of-two octave [B, 2B] is partitioned into 4 linear steps of size S = B / 4 (B >> 2).
 *    - Because S is always a power of two, rounding cap up to the next multiple of S is branchless:
 *      `(cap + S - 1) & ~(S - 1)`.
 *
 * This mathematically bounds stack buffer slack to at most 25%, producing the 24 discrete tiers:
 *   8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 640, 768, 896, 1024.
 */
static inline size_t duo_stack_tier_bytes(size_t cap) {
    return duo_sub_octave_tier(cap, 1024, 32);
}

/**
 * @brief Function pointer signature for vector stack dispatcher callbacks.
 */
typedef int (*duo_vec_visitor_fn)(duo_fixed_vec_t* vec, void* ctx);

/**
 * @brief Zero-heap scoped stack dispatcher for vectors with transparent dynamic heap fallback.
 *
 * Uses fine-grained tiered stack buffers (~1.25x growth from 8B up to 1024B) via a switch statement
 * if capacity <= 64 elements and total byte size (capacity * elem_size) <= 1024 bytes.
 * If requested capacity exceeds stack limits, transparently allocates via DUO_MALLOC
 * and frees via DUO_FREE on scope exit.
 *
 * @param elem_size Size of a single element in bytes.
 * @param capacity Requested element capacity.
 * @param fn Callback function invoked with the initialized duo_fixed_vec_t and user context.
 * @param ctx User-supplied context pointer passed to callback fn.
 * @return Return value from callback fn, or -1 if dynamic allocation fails or fn is NULL.
 */
static inline int duo_with_stack_vec_impl(size_t elem_size, size_t capacity, duo_vec_visitor_fn fn, void* ctx) {
    if (!fn) {
        return -1;
    }
    const size_t max_stack_elems = 64;
    if (capacity <= max_stack_elems) {
        switch (duo_stack_tier_bytes(capacity * elem_size)) {
#define DUO_VEC_STACK_TIER_CASE_(Cap) \
            case Cap: { \
                union { uint64_t u64[(Cap) / sizeof(uint64_t)]; uint8_t bytes[Cap]; } stack_buf; \
                duo_fixed_vec_t vec; \
                duo_fixed_vec_init(&vec, (capacity > 0) ? stack_buf.bytes : NULL, capacity); \
                return fn(&vec, ctx); \
            }
            DUO_VEC_STACK_TIER_CASE_(8)
            DUO_VEC_STACK_TIER_CASE_(16)
            DUO_VEC_STACK_TIER_CASE_(24)
            DUO_VEC_STACK_TIER_CASE_(32)
            DUO_VEC_STACK_TIER_CASE_(40)
            DUO_VEC_STACK_TIER_CASE_(48)
            DUO_VEC_STACK_TIER_CASE_(56)
            DUO_VEC_STACK_TIER_CASE_(64)
            DUO_VEC_STACK_TIER_CASE_(80)
            DUO_VEC_STACK_TIER_CASE_(96)
            DUO_VEC_STACK_TIER_CASE_(112)
            DUO_VEC_STACK_TIER_CASE_(128)
            DUO_VEC_STACK_TIER_CASE_(160)
            DUO_VEC_STACK_TIER_CASE_(192)
            DUO_VEC_STACK_TIER_CASE_(224)
            DUO_VEC_STACK_TIER_CASE_(256)
            DUO_VEC_STACK_TIER_CASE_(320)
            DUO_VEC_STACK_TIER_CASE_(384)
            DUO_VEC_STACK_TIER_CASE_(448)
            DUO_VEC_STACK_TIER_CASE_(512)
            DUO_VEC_STACK_TIER_CASE_(640)
            DUO_VEC_STACK_TIER_CASE_(768)
            DUO_VEC_STACK_TIER_CASE_(896)
            DUO_VEC_STACK_TIER_CASE_(1024)
#undef DUO_VEC_STACK_TIER_CASE_
            default:
                break;
        }
    }
    size_t total_bytes = capacity * elem_size;
    void* heap_buf = DUO_MALLOC(total_bytes > 0 ? total_bytes : 1);
    if (!heap_buf) {
        return -1;
    }
    duo_fixed_vec_t vec;
    duo_fixed_vec_init(&vec, heap_buf, capacity);
    int res = fn(&vec, ctx);
    DUO_FREE(heap_buf);
    return res;
}

/**
 * @brief Semantic alias for explicit element byte-size vector stack dispatching.
 */
#define duo_with_stack_vec_sized(elem_size, capacity, fn, ctx) \
    duo_with_stack_vec_impl((elem_size), (capacity), (duo_vec_visitor_fn)(fn), (ctx))

#ifndef __cplusplus
/**
 * @brief Zero-heap scoped stack dispatcher for vectors with transparent dynamic heap fallback.
 *
 * Automatically infers element size from @p VecType (defined via DUO_C_VEC_TYPE or DUO_C_FIXED_VEC_TYPE)
 * using the VecType##_basetype pattern established in stack vector creation.
 *
 * @param VecType Typed fixed vector type (e.g. int_fixed_vec_t).
 * @param capacity Requested element capacity.
 * @param fn Callback function invoked with the initialized vector pointer and user context.
 * @param ctx User-supplied context pointer passed to callback fn.
 * @return Return value from callback fn, or -1 if dynamic allocation fails or fn is NULL.
 */
#define duo_with_stack_vec(VecType, capacity, fn, ctx) \
    duo_with_stack_vec_impl(sizeof(VecType##_basetype), (capacity), (duo_vec_visitor_fn)(fn), (ctx))

/**
 * @brief Macro variant accepting raw element type (e.g. int, double) directly.
 */
#define duo_with_stack_vec_raw(Type, capacity, fn, ctx) \
    duo_with_stack_vec_impl(sizeof(Type), (capacity), (duo_vec_visitor_fn)(fn), (ctx))
#else
/**
 * @brief Zero-heap scoped stack dispatcher for vectors with transparent dynamic heap fallback.
 *
 * Automatically infers element size from element type @p Type (e.g. int, double)
 * matching C++ DUO_STACK_VECTOR conventions.
 *
 * @param Type Element type (e.g. int, double, MyClass).
 * @param capacity Requested element capacity.
 * @param fn Callback function invoked with the initialized vector pointer and user context.
 * @param ctx User-supplied context pointer passed to callback fn.
 * @return Return value from callback fn, or -1 if dynamic allocation fails or fn is NULL.
 */
#define duo_with_stack_vec(Type, capacity, fn, ctx) \
    duo_with_stack_vec_impl(sizeof(Type), (capacity), (duo_vec_visitor_fn)(fn), (ctx))
#endif



/* ============================================================================
 * 8. PURE C SBO BYTES ENGINE (duo_sbo_bytes_t / duo_bytes_t)
 *
 * 24-byte Small Buffer Optimization (SBO) binary byte buffer.
 * Provides up to 23 bytes inline storage with zero heap allocations.
 * Seamlessly promotes to dynamic heap allocation upon exceeding 23 bytes,
 * and demotes back to inline storage via duo_bytes_shrink_to_fit().
 *
 * Binary Layout (24 bytes):
 * - SBO mode:  [tag (1B)] [m_sbo (23B)]
 * - Heap mode: [m_capacity (8B)] [m_size (8B)] [m_data (8B)]
 *
 * Bit 0 of the first byte is the SBO tag:
 * - is_sbo == 1: SBO inline mode
 * - is_sbo == 0: Dynamic heap mode
 * ============================================================================ */

#define DUO_SBO_BYTES_INLINE_CAP 23

#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || \
    (defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__))
#define DUO_BYTE_ORDER_BIG 1
#else
#define DUO_BYTE_ORDER_LITTLE 1
#endif

struct duo_sbo_tag {
#if defined(DUO_BYTE_ORDER_BIG)
    uint8_t length : 7;
    uint8_t is_sbo : 1;
#else
    uint8_t is_sbo : 1;
    uint8_t length : 7;
#endif
};

struct duo_sbo_capacity {
#if defined(DUO_BYTE_ORDER_BIG)
    uint64_t value  : 63;
    uint64_t is_sbo : 1;
#else
    uint64_t is_sbo : 1;
    uint64_t value  : 63;
#endif
};

typedef struct duo_sbo_bytes_t {
    union {
        struct {
            struct duo_sbo_capacity m_capacity;
            size_t                  m_size;
            uint8_t*                m_data;
        } m_heap;
        struct {
            struct duo_sbo_tag tag;
            uint8_t            m_sbo[DUO_SBO_BYTES_INLINE_CAP];
        } m_sbo;
        uint8_t raw_bytes[24];
    };
} duo_sbo_bytes_t;

typedef duo_sbo_bytes_t duo_bytes_t;
typedef duo_sbo_bytes_t c_bytes_t;
typedef duo_sbo_bytes_t c_sbo_bytes_t;

DUO_STATIC_ASSERT(sizeof(duo_sbo_bytes_t) == 24, "duo_sbo_bytes_t must be exactly 24 bytes!");

/**
 * @brief Returns true if @p b is currently stored in inline SBO mode.
 * @param b Pointer to bytes container.
 * @return true if SBO inline mode (<= 23 bytes), false if dynamic heap mode.
 */
static inline bool duo_bytes_is_sbo(const duo_bytes_t* b) {
    return (b->m_sbo.tag.is_sbo != 0);
}

/**
 * @brief Returns current byte length stored in @p b.
 * @param b Pointer to bytes container.
 * @return Number of valid bytes.
 */
static inline size_t duo_bytes_size(const duo_bytes_t* b) {
    return duo_bytes_is_sbo(b) ? (size_t)b->m_sbo.tag.length : b->m_heap.m_size;
}

/**
 * @brief Returns current capacity of @p b in bytes.
 * @param b Pointer to bytes container.
 * @return Capacity in bytes (23 in SBO mode, or heap capacity).
 */
static inline size_t duo_bytes_capacity(const duo_bytes_t* b) {
    return duo_bytes_is_sbo(b) ? (size_t)DUO_SBO_BYTES_INLINE_CAP : (size_t)b->m_heap.m_capacity.value;
}

/**
 * @brief Returns a mutable pointer to the beginning of the byte buffer in @p b.
 * @param b Pointer to bytes container.
 * @return Mutable byte pointer.
 */
static inline uint8_t* duo_bytes_data(duo_bytes_t* b) {
    return duo_bytes_is_sbo(b) ? (uint8_t*)b->m_sbo.m_sbo : b->m_heap.m_data;
}

/**
 * @brief Returns an immutable pointer to the beginning of the byte buffer in @p b.
 * @param b Pointer to bytes container.
 * @return Const byte pointer.
 */
static inline const uint8_t* duo_bytes_data_const(const duo_bytes_t* b) {
    return duo_bytes_is_sbo(b) ? (const uint8_t*)b->m_sbo.m_sbo : (const uint8_t*)b->m_heap.m_data;
}

/**
 * @brief Initializes @p b to an empty inline buffer (size = 0, capacity = 23, SBO mode).
 * @param b Pointer to bytes container.
 */
static inline void duo_bytes_init(duo_bytes_t* b) {
    DUO_MEMSET(b->raw_bytes, 0, sizeof(b->raw_bytes));
    b->m_sbo.tag.is_sbo = 1;
    b->m_sbo.tag.length = 0;
}

/**
 * @brief Frees any allocated heap memory in @p b and reinitializes it to empty SBO mode.
 * @param b Pointer to bytes container.
 */
static inline void duo_bytes_destroy(duo_bytes_t* b) {
    if (!duo_bytes_is_sbo(b)) {
        if (b->m_heap.m_data) {
            DUO_FREE(b->m_heap.m_data);
        }
    }
    duo_bytes_init(b);
}

/**
 * @brief Resets byte length to 0 without releasing allocated memory or altering SBO/heap mode.
 * @param b Pointer to bytes container.
 */
static inline void duo_bytes_clear(duo_bytes_t* b) {
    if (duo_bytes_is_sbo(b)) {
        b->m_sbo.tag.length = 0;
    } else {
        b->m_heap.m_size = 0;
    }
}

/**
 * @brief Reserves buffer capacity for at least @p min_cap bytes.
 * Seamlessly promotes from inline SBO to dynamic heap buffer with 3-tier adaptive geometric growth
 * via duo_geometric_grow_cap (<256: 2x, <4096: 1.5x, >=4096: 1.25x).
 * Returns true on success, or false on allocation failure.
 */
static inline bool duo_bytes_reserve(duo_bytes_t* b, size_t min_cap) {
    size_t cur_cap = duo_bytes_capacity(b);
    if (min_cap <= cur_cap) {
        return true;
    }
    size_t next_cap = duo_geometric_grow_cap(cur_cap);
    size_t new_cap = (next_cap > min_cap) ? next_cap : min_cap;
    if (new_cap > (((size_t)1 << 62) - 1 + ((size_t)1 << 62))) {
        return false;
    }

    if (duo_bytes_is_sbo(b)) {
        uint8_t* new_data = (uint8_t*)DUO_MALLOC(new_cap);
        if (!new_data) {
            return false;
        }
        size_t cur_sz = (size_t)b->m_sbo.tag.length;
        if (cur_sz > 0) {
            DUO_MEMCPY(new_data, b->m_sbo.m_sbo, cur_sz);
        }
        struct duo_sbo_capacity cap;
        cap.is_sbo = 0;
        cap.value = new_cap;
        b->m_heap.m_capacity = cap;
        b->m_heap.m_size = cur_sz;
        b->m_heap.m_data = new_data;
        return true;
    } else {
        uint8_t* new_data = (uint8_t*)DUO_REALLOC(b->m_heap.m_data, new_cap);
        if (!new_data) {
            return false;
        }
        b->m_heap.m_data = new_data;
        b->m_heap.m_capacity.is_sbo = 0;
        b->m_heap.m_capacity.value = new_cap;
        return true;
    }
}

/**
 * @brief Shrinks capacity to match current size.
 * If size <= 23 bytes, demotes from heap storage back to inline SBO storage and frees heap memory.
 * Returns true on success, or false on reallocation failure.
 */
static inline bool duo_bytes_shrink_to_fit(duo_bytes_t* b) {
    if (duo_bytes_is_sbo(b)) {
        return true;
    }
    size_t cur_sz = b->m_heap.m_size;
    if (cur_sz <= DUO_SBO_BYTES_INLINE_CAP) {
        uint8_t* old_data = b->m_heap.m_data;
        uint8_t tmp[DUO_SBO_BYTES_INLINE_CAP];
        if (cur_sz > 0 && old_data) {
            DUO_MEMCPY(tmp, old_data, cur_sz);
        }
        if (old_data) {
            DUO_FREE(old_data);
        }
        duo_bytes_init(b);
        b->m_sbo.tag.length = (uint8_t)cur_sz;
        if (cur_sz > 0) {
            DUO_MEMCPY(b->m_sbo.m_sbo, tmp, cur_sz);
        }
        return true;
    }
    if (cur_sz == b->m_heap.m_capacity.value) {
        return true;
    }
    uint8_t* new_data = (uint8_t*)DUO_REALLOC(b->m_heap.m_data, cur_sz);
    if (!new_data) {
        return false;
    }
    b->m_heap.m_data = new_data;
    b->m_heap.m_capacity.value = cur_sz;
    return true;
}

/**
 * @brief Appends a single @p byte to @p b.
 * Returns true on success, or false on allocation failure.
 * @param b Pointer to bytes container.
 * @param byte Byte value to append.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bytes_push_back(duo_bytes_t* b, uint8_t byte) {
    if (duo_bytes_is_sbo(b)) {
        uint8_t len = b->m_sbo.tag.length;
        if (len < DUO_SBO_BYTES_INLINE_CAP) {
            b->m_sbo.m_sbo[len] = byte;
            b->m_sbo.tag.length = len + 1;
            return true;
        }
        if (!duo_bytes_reserve(b, (size_t)DUO_SBO_BYTES_INLINE_CAP + 1)) {
            return false;
        }
        b->m_heap.m_data[b->m_heap.m_size++] = byte;
        return true;
    } else {
        if (b->m_heap.m_size < b->m_heap.m_capacity.value || duo_bytes_reserve(b, b->m_heap.m_size + 1)) {
            b->m_heap.m_data[b->m_heap.m_size++] = byte;
            return true;
        }
        return false;
    }
}

/**
 * @brief Removes the last byte from @p b.
 * Returns true on success, or false if @p b was already empty.
 * @param b Pointer to bytes container.
 * @return true on success, false if empty.
 */
static inline bool duo_bytes_pop_back(duo_bytes_t* b) {
    if (duo_bytes_is_sbo(b)) {
        if (b->m_sbo.tag.length > 0) {
            b->m_sbo.tag.length--;
            return true;
        }
        return false;
    } else {
        if (b->m_heap.m_size > 0) {
            b->m_heap.m_size--;
            return true;
        }
        return false;
    }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overread"
#endif

/**
 * @brief Appends @p len bytes from @p data to @p b.
 * Returns true on success, or false on allocation failure.
 * @param b Pointer to bytes container.
 * @param data Pointer to buffer to copy from.
 * @param len Number of bytes to append.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bytes_append(duo_bytes_t* b, const void* data, size_t len) {
    if (len == 0) {
        return true;
    }
    if (!data) {
        return false;
    }
    size_t cur_sz = duo_bytes_size(b);
    size_t req_cap = cur_sz + len;
    if (req_cap < cur_sz) {
        return false;
    }
    if (duo_bytes_is_sbo(b)) {
        if (len <= DUO_SBO_BYTES_INLINE_CAP && req_cap <= DUO_SBO_BYTES_INLINE_CAP) {
            DUO_MEMCPY(&b->m_sbo.m_sbo[cur_sz], data, len);
            b->m_sbo.tag.length = (uint8_t)req_cap;
            return true;
        }
    }
    if (!duo_bytes_reserve(b, req_cap)) {
        return false;
    }
    uint8_t* dst = b->m_heap.m_data;
    DUO_MEMCPY(dst + cur_sz, data, len);
    b->m_heap.m_size = req_cap;
    return true;
}

/**
 * @brief Inserts @p len bytes from @p data into @p b at index @p idx.
 * Returns true on success, or false if idx > size or allocation failed.
 * @param b Pointer to bytes container.
 * @param idx Insertion index (0 <= idx <= size).
 * @param data Pointer to buffer to insert.
 * @param len Number of bytes to insert.
 * @return true on success, false on invalid index or allocation failure.
 */
static inline bool duo_bytes_insert(duo_bytes_t* b, size_t idx, const void* data, size_t len) {
    size_t cur_sz = duo_bytes_size(b);
    if (idx > cur_sz) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (!data) {
        return false;
    }
    size_t req_cap = cur_sz + len;
    if (req_cap < cur_sz) {
        return false;
    }
    if (duo_bytes_is_sbo(b)) {
        if (len <= DUO_SBO_BYTES_INLINE_CAP && req_cap <= DUO_SBO_BYTES_INLINE_CAP && (idx + len) <= DUO_SBO_BYTES_INLINE_CAP) {
            DUO_MEMMOVE(&b->m_sbo.m_sbo[idx + len], &b->m_sbo.m_sbo[idx], cur_sz - idx);
            DUO_MEMCPY(&b->m_sbo.m_sbo[idx], data, len);
            b->m_sbo.tag.length = (uint8_t)req_cap;
            return true;
        }
    }
    if (!duo_bytes_reserve(b, req_cap)) {
        return false;
    }
    uint8_t* dst = b->m_heap.m_data;
    DUO_MEMMOVE(dst + idx + len, dst + idx, cur_sz - idx);
    DUO_MEMCPY(dst + idx, data, len);
    b->m_heap.m_size = req_cap;
    return true;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

/**
 * @brief Erases @p len bytes from @p b starting at index @p idx.
 * Returns true on success, or false if idx + len > size.
 * @param b Pointer to bytes container.
 * @param idx Starting index.
 * @param len Number of bytes to erase.
 * @return true on success, false if range out of bounds.
 */
static inline bool duo_bytes_erase(duo_bytes_t* b, size_t idx, size_t len) {
    size_t cur_sz = duo_bytes_size(b);
    if (idx > cur_sz || idx + len > cur_sz || idx + len < idx) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (duo_bytes_is_sbo(b)) {
        if (cur_sz > DUO_SBO_BYTES_INLINE_CAP || idx + len > DUO_SBO_BYTES_INLINE_CAP) {
            return false;
        }
        DUO_MEMMOVE(&b->m_sbo.m_sbo[idx], &b->m_sbo.m_sbo[idx + len], cur_sz - (idx + len));
        b->m_sbo.tag.length = (uint8_t)(cur_sz - len);
        return true;
    } else {
        DUO_MEMMOVE(&b->m_heap.m_data[idx], &b->m_heap.m_data[idx + len], cur_sz - (idx + len));
        b->m_heap.m_size = cur_sz - len;
        return true;
    }
}

/**
 * @brief Resizes @p b to @p new_size bytes. Newly added bytes are zeroed.
 * Truncation is O(1) without memory reallocation.
 * Returns true on success, or false on allocation failure.
 */
static inline bool duo_bytes_resize(duo_bytes_t* b, size_t new_size) {
    size_t cur_sz = duo_bytes_size(b);
    if (new_size <= cur_sz) {
        if (duo_bytes_is_sbo(b)) {
            b->m_sbo.tag.length = (uint8_t)new_size;
        } else {
            b->m_heap.m_size = new_size;
        }
        return true;
    }
    if (!duo_bytes_reserve(b, new_size)) {
        return false;
    }
    if (duo_bytes_is_sbo(b)) {
        DUO_MEMSET(&b->m_sbo.m_sbo[cur_sz], 0, new_size - cur_sz);
        b->m_sbo.tag.length = (uint8_t)new_size;
    } else {
        DUO_MEMSET(b->m_heap.m_data + cur_sz, 0, new_size - cur_sz);
        b->m_heap.m_size = new_size;
    }
    return true;
}

/**
 * @brief Initializes @p b and copies @p len bytes from @p data.
 * Returns true on success, or false on allocation failure.
 * @param b Pointer to uninitialized bytes container.
 * @param data Pointer to buffer to copy from.
 * @param len Number of bytes to copy.
 * @return true on success, false on allocation failure.
 */
static inline bool duo_bytes_init_from(duo_bytes_t* b, const void* data, size_t len) {
    duo_bytes_init(b);
    if (len == 0) {
        return true;
    }
    if (!data) {
        return false;
    }
    return duo_bytes_append(b, data, len);
}

/**
 * @brief Borrows a generic mutable span from a byte buffer pointer.
 */
#define duo_bytes_as_span(b) \
    ((duo_span_t){ (void*)duo_bytes_data(b), duo_bytes_size(b) })

/**
 * @brief Borrows a generic immutable span view from a byte buffer pointer.
 */
#define duo_bytes_as_view(b) \
    duo_span_view_make((const void*)duo_bytes_data_const(b), duo_bytes_size(b))

/**
 * @brief Lexicographical comparison of two dynamic byte buffers.
 */
static inline int duo_bytes_cmp(const duo_bytes_t* a, const duo_bytes_t* b) {
    return duo_span_view_cmp(duo_bytes_as_view(a), duo_bytes_as_view(b));
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_bytes_t*:
 * duo_bytes_eq, duo_bytes_ne, duo_bytes_lt, duo_bytes_le,
 * duo_bytes_gt, duo_bytes_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_bytes, const duo_bytes_t*, duo_bytes_cmp)

/**
 * @brief Computes 64-bit xxHash3 digest of dynamic byte buffer @p b.
 */
static inline uint64_t duo_bytes_hash(const duo_bytes_t* b) {
    return duo_span_view_hash(duo_bytes_as_view(b));
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of dynamic byte buffer @p b.
 */
static inline uint64_t duo_bytes_hash_seed(const duo_bytes_t* b, uint64_t seed0, uint64_t seed1) {
    return duo_span_view_hash_seed(duo_bytes_as_view(b), seed0, seed1);
}

/* ============================================================================
 * 9. PURE C STACK / FIXED BYTES ENGINE (duo_fixed_bytes_t / DUO_C_STACK_BYTES)
 *
 * 24-byte non-allocating fixed-capacity byte buffer. Operates strictly within
 * a pre-allocated stack buffer or external memory block.
 *
 * Binary Layout (24 bytes on 64-bit):
 *   uint8_t* data;     (8 bytes)
 *   size_t   size;     (8 bytes)
 *   size_t   capacity; (8 bytes)
 * ============================================================================ */

/**
 * @brief Fixed-capacity, non-allocating stack- or externally-backed byte buffer.
 * Standard-layout struct ({ uint8_t* data; size_t size; size_t capacity; }, 24 bytes on 64-bit).
 */
typedef struct duo_fixed_bytes {
    uint8_t* data;     /**< Contiguous byte buffer (stack or externally owned). */
    size_t   size;     /**< Current number of valid bytes (0 <= size <= capacity). */
    size_t   capacity; /**< Maximum capacity in bytes. */
} duo_fixed_bytes_t;

/**
 * @brief Initializes a fixed byte buffer @p b with an existing buffer @p buf and @p cap.
 * @param b Pointer to fixed byte buffer.
 * @param buf Pointer to contiguous backing byte buffer.
 * @param cap Maximum capacity in bytes.
 */
static inline void duo_fixed_bytes_init(duo_fixed_bytes_t* b, uint8_t* buf, size_t cap) {
    b->data = buf;
    b->size = 0;
    b->capacity = cap;
}

/**
 * @brief Initializes @p b with @p buf and @p cap, copying @p len bytes from @p src.
 * Returns true on success, or false if len > cap or (src == NULL && len > 0).
 * @param b Pointer to fixed byte buffer.
 * @param buf Pointer to contiguous backing byte buffer.
 * @param cap Maximum capacity in bytes.
 * @param src Pointer to source data buffer.
 * @param len Number of bytes to copy.
 * @return true on success, false if capacity exceeded or invalid source pointer.
 */
static inline bool duo_fixed_bytes_init_from(duo_fixed_bytes_t* b, uint8_t* buf, size_t cap, const void* src, size_t len) {
    b->data = buf;
    b->size = 0;
    b->capacity = cap;
    if (len == 0) {
        return true;
    }
    if (!src || len > cap) {
        return false;
    }
    DUO_MEMCPY(b->data, src, len);
    b->size = len;
    return true;
}

/**
 * @brief Returns the current number of valid bytes in fixed byte buffer @p b.
 * @param b Pointer to fixed byte buffer.
 * @return Number of active bytes.
 */
static inline size_t duo_fixed_bytes_size(const duo_fixed_bytes_t* b) {
    return b->size;
}

/**
 * @brief Returns the maximum capacity in bytes of fixed byte buffer @p b.
 * @param b Pointer to fixed byte buffer.
 * @return Total capacity in bytes.
 */
static inline size_t duo_fixed_bytes_capacity(const duo_fixed_bytes_t* b) {
    return b->capacity;
}

/**
 * @brief Returns the maximum capacity in bytes of fixed byte buffer @p b (STL compatibility).
 * @param b Pointer to fixed byte buffer.
 * @return Maximum capacity in bytes.
 */
static inline size_t duo_fixed_bytes_max_size(const duo_fixed_bytes_t* b) {
    return b->capacity;
}

/**
 * @brief Returns the remaining available byte capacity in fixed byte buffer @p b.
 * @param b Pointer to fixed byte buffer.
 * @return Unused capacity in bytes.
 */
static inline size_t duo_fixed_bytes_remaining(const duo_fixed_bytes_t* b) {
    return (b->capacity >= b->size) ? (b->capacity - b->size) : 0;
}

/**
 * @brief Returns true if fixed byte buffer @p b contains 0 bytes.
 * @param b Pointer to fixed byte buffer.
 * @return true if size == 0, false otherwise.
 */
static inline bool duo_fixed_bytes_empty(const duo_fixed_bytes_t* b) {
    return b->size == 0;
}

/**
 * @brief Returns true if fixed byte buffer @p b is full (size >= capacity).
 * @param b Pointer to fixed byte buffer.
 * @return true if size >= capacity, false otherwise.
 */
static inline bool duo_fixed_bytes_full(const duo_fixed_bytes_t* b) {
    return b->size >= b->capacity;
}

/**
 * @brief Returns a mutable pointer to the contiguous byte data of @p b.
 * @param b Pointer to fixed byte buffer.
 * @return Mutable uint8_t pointer to start of buffer.
 */
static inline uint8_t* duo_fixed_bytes_data(duo_fixed_bytes_t* b) {
    return b->data;
}

/**
 * @brief Returns an immutable pointer to the contiguous byte data of @p b.
 * @param b Pointer to fixed byte buffer.
 * @return Const uint8_t pointer to start of buffer.
 */
static inline const uint8_t* duo_fixed_bytes_data_const(const duo_fixed_bytes_t* b) {
    return (const uint8_t*)b->data;
}

/**
 * @brief Returns a mutable pointer to byte at index @p idx, or NULL if idx >= size.
 * @param b Pointer to fixed byte buffer.
 * @param idx Zero-based byte index.
 * @return Pointer to byte or NULL if out of bounds.
 */
static inline uint8_t* duo_fixed_bytes_at(duo_fixed_bytes_t* b, size_t idx) {
    return (idx < b->size) ? &b->data[idx] : NULL;
}

/**
 * @brief Returns an immutable pointer to byte at index @p idx, or NULL if idx >= size.
 * @param b Pointer to fixed byte buffer.
 * @param idx Zero-based byte index.
 * @return Const pointer to byte or NULL if out of bounds.
 */
static inline const uint8_t* duo_fixed_bytes_at_const(const duo_fixed_bytes_t* b, size_t idx) {
    return (idx < b->size) ? &b->data[idx] : NULL;
}

/**
 * @brief Resets size to 0 without modifying the underlying buffer or capacity.
 * @param b Pointer to fixed byte buffer.
 */
static inline void duo_fixed_bytes_clear(duo_fixed_bytes_t* b) {
    b->size = 0;
}

/**
 * @brief Appends @p len bytes from @p src into @p b.
 * Returns true on success, or false if buffer would exceed capacity or src is NULL.
 * @param b Pointer to fixed byte buffer.
 * @param src Pointer to source data.
 * @param len Number of bytes to append.
 * @return true on success, false if capacity exceeded or invalid src pointer.
 */
static inline bool duo_fixed_bytes_append(duo_fixed_bytes_t* b, const void* src, size_t len) {
    if (len == 0) {
        return true;
    }
    if (!src) {
        return false;
    }
    if (b->size + len > b->capacity || b->size + len < b->size) {
        return false;
    }
    DUO_MEMCPY(b->data + b->size, src, len);
    b->size += len;
    return true;
}

/**
 * @brief Pushes a single byte to the end of @p b.
 * Returns true on success, or false if full.
 * @param b Pointer to fixed byte buffer.
 * @param byte Byte value to append.
 * @return true on success, false if capacity exceeded.
 */
static inline bool duo_fixed_bytes_push_back(duo_fixed_bytes_t* b, uint8_t byte) {
    if (b->size >= b->capacity) {
        return false;
    }
    b->data[b->size++] = byte;
    return true;
}

/**
 * @brief Appends a single byte to the end of @p b (semantic alias of push_back).
 * @param b Pointer to fixed byte buffer.
 * @param byte Byte value to append.
 * @return true on success, false if capacity exceeded.
 */
static inline bool duo_fixed_bytes_append_byte(duo_fixed_bytes_t* b, uint8_t byte) {
    return duo_fixed_bytes_push_back(b, byte);
}

/**
 * @brief Removes the last byte from @p b.
 * Returns true on success, or false if empty.
 * @param b Pointer to fixed byte buffer.
 * @return true on success, false if empty.
 */
static inline bool duo_fixed_bytes_pop_back(duo_fixed_bytes_t* b) {
    if (b->size == 0) {
        return false;
    }
    --b->size;
    return true;
}

/**
 * @brief Inserts @p len bytes from @p src at index @p idx.
 * Returns true on success, or false if idx > size or size + len > capacity or src is NULL.
 * @param b Pointer to fixed byte buffer.
 * @param idx Insertion index (0 <= idx <= size).
 * @param src Pointer to source data.
 * @param len Number of bytes to insert.
 * @return true on success, false on invalid index or capacity overflow.
 */
static inline bool duo_fixed_bytes_insert(duo_fixed_bytes_t* b, size_t idx, const void* src, size_t len) {
    if (idx > b->size) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (!src) {
        return false;
    }
    if (b->size + len > b->capacity || b->size + len < b->size) {
        return false;
    }
    DUO_MEMMOVE(b->data + idx + len, b->data + idx, b->size - idx);
    DUO_MEMCPY(b->data + idx, src, len);
    b->size += len;
    return true;
}

/**
 * @brief Erases @p len bytes starting at index @p idx.
 * Returns true on success, or false if idx + len > size.
 * @param b Pointer to fixed byte buffer.
 * @param idx Starting index.
 * @param len Number of bytes to erase.
 * @return true on success, false if range out of bounds.
 */
static inline bool duo_fixed_bytes_erase(duo_fixed_bytes_t* b, size_t idx, size_t len) {
    if (idx > b->size || idx + len > b->size || idx + len < idx) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    DUO_MEMMOVE(b->data + idx, b->data + idx + len, b->size - (idx + len));
    b->size -= len;
    return true;
}

/**
 * @brief Resizes @p b to @p new_size. If growing, new elements are set to @p fill_byte.
 * Returns true on success, or false if new_size > capacity.
 * @param b Pointer to fixed byte buffer.
 * @param new_size Target byte count.
 * @param fill_byte Value to initialize newly added bytes.
 * @return true on success, false if new_size exceeds capacity.
 */
static inline bool duo_fixed_bytes_resize(duo_fixed_bytes_t* b, size_t new_size, uint8_t fill_byte) {
    if (new_size > b->capacity) {
        return false;
    }
    if (new_size > b->size) {
        DUO_MEMSET(b->data + b->size, fill_byte, new_size - b->size);
    }
    b->size = new_size;
    return true;
}

/**
 * @brief Borrows a generic mutable span from a fixed byte buffer pointer.
 */
#define duo_fixed_bytes_as_span(b) \
    ((duo_span_t){ (void*)(b)->data, (b)->size })

/**
 * @brief Borrows a generic immutable span view from a fixed byte buffer pointer.
 */
#define duo_fixed_bytes_as_view(b) \
    duo_span_view_make((const void*)(b)->data, (b)->size)

/**
 * @brief Lexicographical comparison of two fixed byte buffers.
 */
static inline int duo_fixed_bytes_cmp(const duo_fixed_bytes_t* a, const duo_fixed_bytes_t* b) {
    return duo_span_view_cmp(duo_fixed_bytes_as_view(a), duo_fixed_bytes_as_view(b));
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_fixed_bytes_t*:
 * duo_fixed_bytes_eq, duo_fixed_bytes_ne, duo_fixed_bytes_lt,
 * duo_fixed_bytes_le, duo_fixed_bytes_gt, duo_fixed_bytes_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_fixed_bytes, const duo_fixed_bytes_t*, duo_fixed_bytes_cmp)

/**
 * @brief Computes 64-bit xxHash3 digest of fixed byte buffer @p b.
 */
static inline uint64_t duo_fixed_bytes_hash(const duo_fixed_bytes_t* b) {
    return duo_span_view_hash(duo_fixed_bytes_as_view(b));
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of fixed byte buffer @p b.
 */
static inline uint64_t duo_fixed_bytes_hash_seed(const duo_fixed_bytes_t* b, uint64_t seed0, uint64_t seed1) {
    return duo_span_view_hash_seed(duo_fixed_bytes_as_view(b), seed0, seed1);
}

/**
 * @brief Allocates an uninitialized fixed-capacity buffer on the stack and binds a duo_fixed_bytes_t to it.
 * @param Name Identifier of the created duo_fixed_bytes_t variable
 * @param Capacity Compile-time byte count to allocate on the stack
 */
#define DUO_C_STACK_BYTES(Name, Capacity) \
    uint8_t Name##_raw_buf_[(Capacity)]; \
    duo_fixed_bytes_t Name = { Name##_raw_buf_, 0, (Capacity) }

/**
 * @brief Allocates and initializes a stack byte buffer from a variable list of byte values.
 * @param Name Identifier of the created duo_fixed_bytes_t variable
 * @param ... Initializer byte elements (e.g. 0x01, 0x02, 0x03)
 */
#define DUO_C_STACK_BYTES_INIT(Name, ...) \
    uint8_t Name##_raw_buf_[] = { __VA_ARGS__ }; \
    duo_fixed_bytes_t Name = { Name##_raw_buf_, sizeof(Name##_raw_buf_), sizeof(Name##_raw_buf_) }

/**
 * @brief Allocates an explicit fixed-capacity stack buffer and initializes initial bytes from varargs.
 * @param Name Identifier of the created duo_fixed_bytes_t variable
 * @param Capacity Total compile-time capacity on the stack
 * @param ... Initializer byte elements
 */
#define DUO_C_STACK_BYTES_OF(Name, Capacity, ...) \
    uint8_t Name##_raw_buf_[(Capacity)] = { __VA_ARGS__ }; \
    duo_fixed_bytes_t Name = { Name##_raw_buf_, sizeof((uint8_t[]){ __VA_ARGS__ }), (Capacity) }

/**
 * @brief Function pointer signature for fixed bytes stack dispatcher callbacks.
 */
typedef int (*duo_bytes_visitor_fn)(duo_fixed_bytes_t* bytes, void* ctx);

/**
 * @brief Zero-heap scoped stack dispatcher for bytes with transparent dynamic heap fallback.
 *
 * Uses tiered stack buffers (32B, 64B, 128B, 256B, 512B, 1024B) via a switch statement
 * if capacity <= 1024 bytes. If requested capacity exceeds 1024 bytes, transparently
 * allocates via DUO_MALLOC and frees via DUO_FREE on scope exit.
 *
 * @param capacity Requested byte capacity.
 * @param fn Callback function invoked with the initialized duo_fixed_bytes_t and user context.
 * @param ctx User-supplied context pointer passed to callback fn.
 * @return Return value from callback fn, or -1 if dynamic allocation fails or fn is NULL.
 */
static inline int duo_with_stack_bytes(size_t capacity, duo_bytes_visitor_fn fn, void* ctx) {
    if (!fn) {
        return -1;
    }
    switch (duo_stack_tier_bytes(capacity)) {
#define DUO_BYTES_STACK_TIER_CASE_(Cap) \
        case Cap: { \
            uint8_t stack_buf[Cap]; \
            duo_fixed_bytes_t b; \
            duo_fixed_bytes_init(&b, stack_buf, capacity); \
            return fn(&b, ctx); \
        }
        DUO_BYTES_STACK_TIER_CASE_(8)
        DUO_BYTES_STACK_TIER_CASE_(16)
        DUO_BYTES_STACK_TIER_CASE_(24)
        DUO_BYTES_STACK_TIER_CASE_(32)
        DUO_BYTES_STACK_TIER_CASE_(40)
        DUO_BYTES_STACK_TIER_CASE_(48)
        DUO_BYTES_STACK_TIER_CASE_(56)
        DUO_BYTES_STACK_TIER_CASE_(64)
        DUO_BYTES_STACK_TIER_CASE_(80)
        DUO_BYTES_STACK_TIER_CASE_(96)
        DUO_BYTES_STACK_TIER_CASE_(112)
        DUO_BYTES_STACK_TIER_CASE_(128)
        DUO_BYTES_STACK_TIER_CASE_(160)
        DUO_BYTES_STACK_TIER_CASE_(192)
        DUO_BYTES_STACK_TIER_CASE_(224)
        DUO_BYTES_STACK_TIER_CASE_(256)
        DUO_BYTES_STACK_TIER_CASE_(320)
        DUO_BYTES_STACK_TIER_CASE_(384)
        DUO_BYTES_STACK_TIER_CASE_(448)
        DUO_BYTES_STACK_TIER_CASE_(512)
        DUO_BYTES_STACK_TIER_CASE_(640)
        DUO_BYTES_STACK_TIER_CASE_(768)
        DUO_BYTES_STACK_TIER_CASE_(896)
        DUO_BYTES_STACK_TIER_CASE_(1024)
#undef DUO_BYTES_STACK_TIER_CASE_
        default: {
            uint8_t* heap_buf = (uint8_t*)DUO_MALLOC(capacity > 0 ? capacity : 1);
            if (!heap_buf) {
                return -1;
            }
            duo_fixed_bytes_t b;
            duo_fixed_bytes_init(&b, heap_buf, capacity);
            int res = fn(&b, ctx);
            DUO_FREE(heap_buf);
            return res;
        }
    }
}



/* ============================================================================
 * 10. PURE C STRING VIEW ENGINE (duo_str_view_t)
 *
 * 16-byte non-owning read-only string slice. Binary compatible with C++ duo::StringView.
 * Layout: { const char* data; size_t size; }
 * Does NOT require null termination.
 * ============================================================================ */

/**
 * @brief 16-byte non-owning read-only character string slice.
 *
 * Encapsulates a contiguous character pointer and size. Compatible with
 * string literals, substrings, and heap string slices without allocating.
 */
typedef struct duo_str_view {
    const char* data; /**< Pointer to character sequence (not required to be null-terminated). */
    size_t      size; /**< Number of characters in the view. */
} duo_str_view_t;

DUO_STATIC_ASSERT(sizeof(duo_str_view_t) == 16, "duo_str_view_t must be exactly 16 bytes!");

/**
 * @brief Constructs a non-owning string view from a pointer and explicit character count.
 * @param data Contiguous character buffer (can be non-null-terminated).
 * @param size Number of characters in the slice.
 * @return A 16-byte duo_str_view_t instance.
 */
static inline duo_str_view_t duo_str_view_make(const char* data, size_t size) {
    duo_str_view_t v;
    v.data = data;
    v.size = size;
    return v;
}

/**
 * @brief Constructs a non-owning string view from a null-terminated C string.
 * @param str Null-terminated C string, or NULL for an empty view.
 * @return A 16-byte duo_str_view_t instance.
 */
static inline duo_str_view_t duo_str_view_make_cstr(const char* str) {
    duo_str_view_t v;
    v.data = str;
    v.size = 0;
    if (str) {
        while (str[v.size] != '\0') {
            ++v.size;
        }
    }
    return v;
}

/**
 * @brief Lexicographically compares two string views.
 * @param a First string view.
 * @param b Second string view.
 * @return Negative if a < b, positive if a > b, 0 if identical.
 */
static inline int duo_str_view_cmp(duo_str_view_t a, duo_str_view_t b) {
    size_t min_len = a.size < b.size ? a.size : b.size;
    if (min_len > 0 && a.data && b.data) {
        int r = memcmp(a.data, b.data, min_len);
        if (r != 0) return r;
    }
    return (a.size < b.size) ? -1 : ((a.size > b.size) ? 1 : 0);
}

/**
 * @brief Helper to convert an ASCII character to lowercase.
 */
static inline int duo_ascii_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

/**
 * @brief Case-insensitive lexicographical comparison of two string views.
 * @param a First string view.
 * @param b Second string view.
 * @return Negative if a < b, positive if a > b, 0 if identical (case-insensitive).
 */
static inline int duo_str_view_casecmp(duo_str_view_t a, duo_str_view_t b) {
    size_t min_len = a.size < b.size ? a.size : b.size;
    if (min_len > 0 && a.data && b.data) {
        for (size_t i = 0; i < min_len; ++i) {
            int ca = duo_ascii_tolower((unsigned char)a.data[i]);
            int cb = duo_ascii_tolower((unsigned char)b.data[i]);
            if (ca != cb) {
                return ca - cb;
            }
        }
    }
    return (a.size < b.size) ? -1 : ((a.size > b.size) ? 1 : 0);
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_str_view_t:
 * duo_str_view_eq, duo_str_view_ne, duo_str_view_lt, duo_str_view_le,
 * duo_str_view_gt, duo_str_view_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_str_view, duo_str_view_t, duo_str_view_cmp)

/**
 * @brief Case-insensitive equality check for two string views.
 */
static inline bool duo_str_view_case_eq(duo_str_view_t a, duo_str_view_t b) {
    return duo_str_view_casecmp(a, b) == 0;
}

/**
 * @brief Computes 64-bit xxHash3 digest of string view @p v.
 */
static inline uint64_t duo_str_view_hash(duo_str_view_t v) {
    return duo_hash_xxhash3(v.data, v.size, 0, 0);
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of string view @p v.
 */
static inline uint64_t duo_str_view_hash_seed(duo_str_view_t v, uint64_t seed0, uint64_t seed1) {
    return duo_hash_xxhash3(v.data, v.size, seed0, seed1);
}

/**
 * @brief Checks if string view @p v starts with @p prefix.
 * @param v Haystack string view.
 * @param prefix Needle prefix view.
 * @return True if @p v begins with @p prefix; false otherwise.
 */
static inline bool duo_str_view_starts_with(duo_str_view_t v, duo_str_view_t prefix) {
    if (prefix.size > v.size) return false;
    if (prefix.size == 0) return true;
    return memcmp(v.data, prefix.data, prefix.size) == 0;
}

/**
 * @brief Checks if string view @p v ends with @p suffix.
 * @param v Haystack string view.
 * @param suffix Needle suffix view.
 * @return True if @p v ends with @p suffix; false otherwise.
 */
static inline bool duo_str_view_ends_with(duo_str_view_t v, duo_str_view_t suffix) {
    if (suffix.size > v.size) return false;
    if (suffix.size == 0) return true;
    return memcmp(v.data + (v.size - suffix.size), suffix.data, suffix.size) == 0;
}

/**
 * @brief Searches for character @p ch in string view @p v.
 * @param v String view to search.
 * @param ch Character to find.
 * @return Index of the first occurrence of @p ch, or (size_t)-1 if not found.
 */
static inline size_t duo_str_view_find(duo_str_view_t v, char ch) {
    if (v.data) {
        for (size_t i = 0; i < v.size; ++i) {
            if (v.data[i] == ch) return i;
        }
    }
    return (size_t)-1;
}

/**
 * @brief Returns a subview starting at @p offset with length @p count.
 * @param v Source string view.
 * @param offset Start byte offset.
 * @param count Number of bytes to include.
 * @return Sliced string view.
 */
static inline duo_str_view_t duo_str_view_subview(duo_str_view_t v, size_t offset, size_t count) {
    if (offset >= v.size) {
        return duo_str_view_make(NULL, 0);
    }
    if (offset + count > v.size) {
        count = v.size - offset;
    }
    return duo_str_view_make(v.data + offset, count);
}

/* ============================================================================
 * 11. PURE C SBO STRING ENGINE (duo_string_t)
 *
 * 24-byte Small Buffer Optimized (SBO) null-terminated string engine.
 *
 * Inline Mode (size <= 22):
 *   - Tag: 1 byte (bit 0 is_sbo = 1, bits 1-7 length)
 *   - Characters: 22 bytes inline buffer (m_sbo[0..21])
 *   - Null Terminator: Guaranteed at m_sbo[length] (up to index 22)
 *   - Zero heap allocations.
 *
 * Heap Mode (size > 22):
 *   - m_capacity: 8 bytes (bit 0 is_sbo = 0, bits 1-63 capacity)
 *   - m_size: 8 bytes
 *   - m_data: 8 bytes (allocated with capacity + 1 bytes)
 *   - Null Terminator: Guaranteed at m_data[m_size] == '\0'
 *
 * Invariants:
 *   - Guaranteed '\0' null termination in O(1) in both modes.
 *   - duo_string_c_str() never allocates and is zero-overhead.
 *   - Demotion via duo_string_shrink_to_fit() migrates heap memory back to
 *     inline storage when size drops to <= 22 bytes.
 * ============================================================================ */

#define DUO_SBO_STRING_INLINE_CAP 22

typedef duo_sbo_bytes_t duo_string_t;
typedef duo_sbo_bytes_t c_string_t;

DUO_STATIC_ASSERT(sizeof(duo_string_t) == 24, "duo_string_t must be exactly 24 bytes!");

/**
 * @brief Returns true if string @p s is currently stored in inline SBO mode.
 * @param s Pointer to string.
 * @return True if inline SBO mode; false if dynamic heap mode.
 */
static inline bool duo_string_is_sbo(const duo_string_t* s) {
    return (s->m_sbo.tag.is_sbo != 0);
}

/**
 * @brief Returns the current number of characters in string @p s (excluding null terminator).
 * @param s Pointer to string.
 * @return String length in bytes.
 */
static inline size_t duo_string_size(const duo_string_t* s) {
    return duo_string_is_sbo(s) ? (size_t)s->m_sbo.tag.length : s->m_heap.m_size;
}

/**
 * @brief Semantic alias for duo_string_size.
 * @param s Pointer to string.
 * @return String length in bytes.
 */
static inline size_t duo_string_length(const duo_string_t* s) {
    return duo_string_size(s);
}

/**
 * @brief Returns the maximum character capacity of string @p s without reallocation.
 * @param s Pointer to string.
 * @return Total capacity in bytes (excluding null terminator space).
 */
static inline size_t duo_string_capacity(const duo_string_t* s) {
    return duo_string_is_sbo(s) ? (size_t)DUO_SBO_STRING_INLINE_CAP : (size_t)s->m_heap.m_capacity.value;
}

/**
 * @brief Checks if string @p s is empty (size == 0).
 * @param s Pointer to string.
 * @return True if length is 0; false otherwise.
 */
static inline bool duo_string_empty(const duo_string_t* s) {
    return duo_string_size(s) == 0;
}

/**
 * @brief Returns a guaranteed null-terminated C string pointer in O(1) without allocations.
 * @param s Pointer to string.
 * @return Pointer to contiguous null-terminated character sequence.
 */
static inline const char* duo_string_c_str(const duo_string_t* s) {
    return duo_string_is_sbo(s) ? (const char*)s->m_sbo.m_sbo : (const char*)s->m_heap.m_data;
}

/**
 * @brief Returns a mutable character pointer to the string's buffer.
 * @param s Pointer to string.
 * @return Mutable character pointer.
 */
static inline char* duo_string_data(duo_string_t* s) {
    return duo_string_is_sbo(s) ? (char*)s->m_sbo.m_sbo : (char*)s->m_heap.m_data;
}

/**
 * @brief Returns an immutable character pointer to the string's buffer.
 * @param s Pointer to string.
 * @return Immutable character pointer.
 */
static inline const char* duo_string_data_const(const duo_string_t* s) {
    return duo_string_c_str(s);
}

/**
 * @brief Initializes string @p s to empty in inline SBO mode with guaranteed null terminator.
 * @param s Pointer to string.
 */
static inline void duo_string_init(duo_string_t* s) {
    s->m_sbo.tag.is_sbo = 1;
    s->m_sbo.tag.length = 0;
    s->m_sbo.m_sbo[0] = '\0';
}

/**
 * @brief Releases any dynamic heap memory allocated by @p s and resets to empty SBO mode.
 * @param s Pointer to string.
 */
static inline void duo_string_destroy(duo_string_t* s) {
    if (!duo_string_is_sbo(s) && s->m_heap.m_data) {
        DUO_FREE(s->m_heap.m_data);
    }
    duo_string_init(s);
}

/**
 * @brief Resets string length to 0, maintaining allocated heap capacity if present.
 * @param s Pointer to string.
 */
static inline void duo_string_clear(duo_string_t* s) {
    if (duo_string_is_sbo(s)) {
        s->m_sbo.tag.length = 0;
        s->m_sbo.m_sbo[0] = '\0';
    } else {
        s->m_heap.m_size = 0;
        if (s->m_heap.m_data) {
            s->m_heap.m_data[0] = '\0';
        }
    }
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wstringop-overread"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Walloc-size-larger-than="
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

/**
 * @brief Reserves buffer capacity for at least @p min_cap characters (plus null terminator).
 * Seamlessly promotes from inline SBO to dynamic heap buffer with 3-tier adaptive geometric growth
 * via duo_geometric_grow_cap (<256: 2x, <4096: 1.5x, >=4096: 1.25x).
 * @param s Pointer to string.
 * @param min_cap Desired minimum character capacity.
 * @return True on success; false on memory allocation failure.
 */
static inline bool duo_string_reserve(duo_string_t* s, size_t min_cap) {
    size_t cur_cap = duo_string_capacity(s);
    if (min_cap <= cur_cap) {
        return true;
    }
    size_t next_cap = duo_geometric_grow_cap(cur_cap);
    size_t new_cap = (next_cap > min_cap) ? next_cap : min_cap;
    if (new_cap > (((size_t)1 << 62) - 1 + ((size_t)1 << 62)) - 1) {
        return false;
    }
    if (duo_string_is_sbo(s)) {
        char* ptr = (char*)DUO_MALLOC(new_cap + 1);
        if (!ptr) {
            return false;
        }
        size_t cur_sz = (size_t)s->m_sbo.tag.length;
        DUO_MEMCPY(ptr, s->m_sbo.m_sbo, cur_sz);
        ptr[cur_sz] = '\0';
        s->m_heap.m_capacity.is_sbo = 0;
        s->m_heap.m_capacity.value = new_cap;
        s->m_heap.m_size = cur_sz;
        s->m_heap.m_data = (uint8_t*)ptr;
        return true;
    } else {
        char* ptr = (char*)DUO_REALLOC(s->m_heap.m_data, new_cap + 1);
        if (!ptr) {
            return false;
        }
        s->m_heap.m_data = (uint8_t*)ptr;
        s->m_heap.m_capacity.value = new_cap;
        s->m_heap.m_capacity.is_sbo = 0;
        ptr[s->m_heap.m_size] = '\0';
        return true;
    }
}

/**
 * @brief Shrinks string storage to fit active characters. Demotes to SBO if size <= 22.
 * @param s Pointer to string.
 * @return True on success; false on reallocation failure.
 */
static inline bool duo_string_shrink_to_fit(duo_string_t* s) {
    if (duo_string_is_sbo(s)) {
        return true;
    }
    size_t cur_sz = s->m_heap.m_size;
    if (cur_sz <= DUO_SBO_STRING_INLINE_CAP) {
        uint8_t temp[DUO_SBO_STRING_INLINE_CAP + 1];
        DUO_MEMCPY(temp, s->m_heap.m_data, cur_sz);
        DUO_FREE(s->m_heap.m_data);
        s->m_sbo.tag.is_sbo = 1;
        s->m_sbo.tag.length = (uint8_t)cur_sz;
        DUO_MEMCPY(s->m_sbo.m_sbo, temp, cur_sz);
        s->m_sbo.m_sbo[cur_sz] = '\0';
        return true;
    } else {
        if (s->m_heap.m_capacity.value > cur_sz) {
            char* ptr = (char*)DUO_REALLOC(s->m_heap.m_data, cur_sz + 1);
            if (ptr) {
                s->m_heap.m_data = (uint8_t*)ptr;
                s->m_heap.m_capacity.value = cur_sz;
                s->m_heap.m_capacity.is_sbo = 0;
                ptr[cur_sz] = '\0';
            }
        }
        return true;
    }
}

/**
 * @brief Appends a single character to the end of string @p s.
 * @param s Pointer to string.
 * @param ch Character to append.
 * @return True on success; false on memory allocation failure.
 */
static inline bool duo_string_push_back(duo_string_t* s, char ch) {
    size_t sz = duo_string_size(s);
    if (duo_string_is_sbo(s)) {
        if (sz < DUO_SBO_STRING_INLINE_CAP) {
            s->m_sbo.m_sbo[sz] = (uint8_t)ch;
            s->m_sbo.m_sbo[sz + 1] = '\0';
            s->m_sbo.tag.length = (uint8_t)(sz + 1);
            return true;
        }
    } else {
        if (sz < s->m_heap.m_capacity.value) {
            s->m_heap.m_data[sz] = (uint8_t)ch;
            s->m_heap.m_data[sz + 1] = '\0';
            s->m_heap.m_size = sz + 1;
            return true;
        }
    }
    if (!duo_string_reserve(s, sz + 1)) {
        return false;
    }
    s->m_heap.m_data[sz] = (uint8_t)ch;
    s->m_heap.m_data[sz + 1] = '\0';
    s->m_heap.m_size = sz + 1;
    return true;
}

/**
 * @brief Removes the last character from string @p s.
 * @param s Pointer to string.
 * @return True if a character was removed; false if string was already empty.
 */
static inline bool duo_string_pop_back(duo_string_t* s) {
    size_t sz = duo_string_size(s);
    if (sz == 0) {
        return false;
    }
    --sz;
    if (duo_string_is_sbo(s)) {
        s->m_sbo.m_sbo[sz] = '\0';
        s->m_sbo.tag.length = (uint8_t)sz;
    } else {
        s->m_heap.m_data[sz] = '\0';
        s->m_heap.m_size = sz;
    }
    return true;
}

/**
 * @brief Appends @p len characters from @p str to string @p s.
 * @param s Pointer to string.
 * @param str Character buffer to append.
 * @param len Number of characters to append.
 * @return True on success; false on memory allocation failure.
 */
static inline bool duo_string_append_len(duo_string_t* s, const char* str, size_t len) {
    if (len == 0) {
        return true;
    }
    if (!str) {
        return false;
    }
    size_t cur_sz = duo_string_size(s);
    size_t req_cap = cur_sz + len;
    if (req_cap < cur_sz) {
        return false;
    }
    if (duo_string_is_sbo(s)) {
        if (len <= DUO_SBO_STRING_INLINE_CAP && req_cap <= DUO_SBO_STRING_INLINE_CAP) {
            DUO_MEMCPY(&s->m_sbo.m_sbo[cur_sz], str, len);
            s->m_sbo.m_sbo[req_cap] = '\0';
            s->m_sbo.tag.length = (uint8_t)req_cap;
            return true;
        }
    }
    if (!duo_string_reserve(s, req_cap)) {
        return false;
    }
    DUO_MEMCPY(s->m_heap.m_data + cur_sz, str, len);
    s->m_heap.m_data[req_cap] = '\0';
    s->m_heap.m_size = req_cap;
    return true;
}

/**
 * @brief Appends null-terminated C string @p str to string @p s.
 * @param s Pointer to string.
 * @param str Null-terminated string to append.
 * @return True on success; false on memory allocation failure.
 */
static inline bool duo_string_append(duo_string_t* s, const char* str) {
    if (!str) {
        return true;
    }
    size_t len = 0;
    while (str[len] != '\0') {
        ++len;
    }
    return duo_string_append_len(s, str, len);
}

/**
 * @brief Inserts @p len characters from @p str at index @p idx in string @p s.
 * @param s Pointer to string.
 * @param idx Insertion index (0 <= idx <= size).
 * @param str Characters to insert.
 * @param len Number of characters to insert.
 * @return True on success; false on invalid index or memory failure.
 */
static inline bool duo_string_insert(duo_string_t* s, size_t idx, const char* str, size_t len) {
    size_t cur_sz = duo_string_size(s);
    if (idx > cur_sz) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (!str) {
        return false;
    }
    size_t req_cap = cur_sz + len;
    if (req_cap < cur_sz) {
        return false;
    }
    if (duo_string_is_sbo(s)) {
        if (len <= DUO_SBO_STRING_INLINE_CAP && req_cap <= DUO_SBO_STRING_INLINE_CAP) {
            DUO_MEMMOVE(&s->m_sbo.m_sbo[idx + len], &s->m_sbo.m_sbo[idx], cur_sz - idx);
            DUO_MEMCPY(&s->m_sbo.m_sbo[idx], str, len);
            s->m_sbo.m_sbo[req_cap] = '\0';
            s->m_sbo.tag.length = (uint8_t)req_cap;
            return true;
        }
    }
    if (!duo_string_reserve(s, req_cap)) {
        return false;
    }
    uint8_t* dst = s->m_heap.m_data;
    DUO_MEMMOVE(dst + idx + len, dst + idx, cur_sz - idx);
    DUO_MEMCPY(dst + idx, str, len);
    dst[req_cap] = '\0';
    s->m_heap.m_size = req_cap;
    return true;
}

/**
 * @brief Erases @p len characters from string @p s starting at index @p idx.
 * @param s Pointer to string.
 * @param idx Starting index.
 * @param len Number of characters to erase.
 * @return True on success; false if range is out of bounds.
 */
static inline bool duo_string_erase(duo_string_t* s, size_t idx, size_t len) {
    size_t cur_sz = duo_string_size(s);
    if (idx > cur_sz || idx + len > cur_sz || idx + len < idx) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    size_t new_sz = cur_sz - len;
    if (duo_string_is_sbo(s)) {
        DUO_MEMMOVE(&s->m_sbo.m_sbo[idx], &s->m_sbo.m_sbo[idx + len], cur_sz - (idx + len));
        s->m_sbo.m_sbo[new_sz] = '\0';
        s->m_sbo.tag.length = (uint8_t)new_sz;
    } else {
        DUO_MEMMOVE(&s->m_heap.m_data[idx], &s->m_heap.m_data[idx + len], cur_sz - (idx + len));
        s->m_heap.m_data[new_sz] = '\0';
        s->m_heap.m_size = new_sz;
    }
    return true;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

/**
 * @brief Initializes string @p s by copying @p len characters from @p str.
 * @param s Pointer to string.
 * @param str Character buffer.
 * @param len Number of characters to copy.
 * @return True on success; false on memory failure.
 */
static inline bool duo_string_init_len(duo_string_t* s, const char* str, size_t len) {
    duo_string_init(s);
    if (len == 0) {
        return true;
    }
    if (!str) {
        return false;
    }
    return duo_string_append_len(s, str, len);
}

/**
 * @brief Initializes string @p s from null-terminated C string @p str.
 * @param s Pointer to string.
 * @param str Null-terminated string.
 * @return True on success; false on memory failure.
 */
static inline bool duo_string_init_cstr(duo_string_t* s, const char* str) {
    duo_string_init(s);
    return duo_string_append(s, str);
}

/**
 * @brief Borrows an immutable duo_str_view_t from string @p s in O(1).
 */
#define duo_string_as_view(s) \
    duo_str_view_make(duo_string_c_str(s), duo_string_size(s))

/**
 * @brief Lexicographical comparison of two dynamic strings.
 */
static inline int duo_string_cmp(const duo_string_t* a, const duo_string_t* b) {
    return duo_str_view_cmp(duo_string_as_view(a), duo_string_as_view(b));
}

/**
 * @brief Case-insensitive comparison of two dynamic strings.
 */
static inline int duo_string_casecmp(const duo_string_t* a, const duo_string_t* b) {
    return duo_str_view_casecmp(duo_string_as_view(a), duo_string_as_view(b));
}

/**
 * @brief Case-insensitive equality of two dynamic strings.
 */
static inline bool duo_string_case_eq(const duo_string_t* a, const duo_string_t* b) {
    return duo_string_casecmp(a, b) == 0;
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_string_t*:
 * duo_string_eq, duo_string_ne, duo_string_lt, duo_string_le,
 * duo_string_gt, duo_string_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_string, const duo_string_t*, duo_string_cmp)

/**
 * @brief Checks equality of a dynamic string with a null-terminated C string.
 */
static inline bool duo_string_eq_cstr(const duo_string_t* a, const char* str) {
    return duo_str_view_eq(duo_string_as_view(a), duo_str_view_make_cstr(str));
}

/**
 * @brief Computes 64-bit xxHash3 digest of dynamic string @p s.
 */
static inline uint64_t duo_string_hash(const duo_string_t* s) {
    return duo_str_view_hash(duo_string_as_view(s));
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of dynamic string @p s.
 */
static inline uint64_t duo_string_hash_seed(const duo_string_t* s, uint64_t seed0, uint64_t seed1) {
    return duo_str_view_hash_seed(duo_string_as_view(s), seed0, seed1);
}

/* ============================================================================
 * 12. PURE C STACK / FIXED STRING ENGINE (duo_fixed_string_t / DUO_C_STACK_STRING)
 *
 * 24-byte non-allocating fixed-capacity character buffer. Operates strictly within
 * a pre-allocated stack buffer or external memory block.
 *
 * Invariant:
 *   The underlying buffer must have at least (capacity + 1) bytes allocated.
 *   data[size] is ALWAYS guaranteed to be '\0' at all times, ensuring O(1)
 *   zero-copy C-string compatibility (duo_fixed_string_c_str).
 *
 * Binary Layout (24 bytes on 64-bit):
 *   char*  data;     (8 bytes)
 *   size_t size;     (8 bytes)
 *   size_t capacity; (8 bytes)
 * ============================================================================ */

/**
 * @brief Fixed-capacity, non-allocating stack- or externally-backed character string.
 * Standard-layout struct ({ char* data; size_t size; size_t capacity; }, 24 bytes on 64-bit).
 */
typedef struct duo_fixed_string {
    char*  data;     /**< Contiguous character buffer (size >= capacity + 1). */
    size_t size;     /**< Current number of valid characters (0 <= size <= capacity). */
    size_t capacity; /**< Maximum character capacity (excluding null terminator). */
} duo_fixed_string_t;

/**
 * @brief Initializes a fixed string @p s with an existing buffer @p buf and capacity @p cap.
 * Sets size to 0 and writes '\0' at buf[0].
 */
static inline void duo_fixed_string_init(duo_fixed_string_t* s, char* buf, size_t cap) {
    s->data = buf;
    s->size = 0;
    s->capacity = cap;
    if (buf) {
        buf[0] = '\0';
    }
}

/**
 * @brief Initializes @p s with @p buf and @p cap, copying null-terminated @p str.
 * Returns true on success, or false if strlen(str) > cap or buf is NULL.
 */
static inline bool duo_fixed_string_init_from(duo_fixed_string_t* s, char* buf, size_t cap, const char* str) {
    s->data = buf;
    s->size = 0;
    s->capacity = cap;
    if (!buf) {
        return false;
    }
    buf[0] = '\0';
    if (!str) {
        return true;
    }
    size_t len = strlen(str);
    if (len > cap) {
        return false;
    }
    DUO_MEMCPY(buf, str, len);
    buf[len] = '\0';
    s->size = len;
    return true;
}

/**
 * @brief Initializes @p s with @p buf and @p cap, copying @p len characters from @p str.
 * Returns true on success, or false if len > cap or buf is NULL or (str == NULL && len > 0).
 */
static inline bool duo_fixed_string_init_from_len(duo_fixed_string_t* s, char* buf, size_t cap, const char* str, size_t len) {
    s->data = buf;
    s->size = 0;
    s->capacity = cap;
    if (!buf) {
        return false;
    }
    buf[0] = '\0';
    if (len > cap) {
        return false;
    }
    if (len > 0) {
        if (!str) {
            return false;
        }
        DUO_MEMCPY(buf, str, len);
    }
    buf[len] = '\0';
    s->size = len;
    return true;
}

/**
 * @brief Returns the current number of characters in fixed string @p s (excluding null terminator).
 */
static inline size_t duo_fixed_string_size(const duo_fixed_string_t* s) {
    return s->size;
}

/**
 * @brief Semantic alias for duo_fixed_string_size.
 */
static inline size_t duo_fixed_string_length(const duo_fixed_string_t* s) {
    return s->size;
}

/**
 * @brief Returns the maximum character capacity of fixed string @p s.
 */
static inline size_t duo_fixed_string_capacity(const duo_fixed_string_t* s) {
    return s->capacity;
}

/**
 * @brief Semantic alias for duo_fixed_string_capacity.
 */
static inline size_t duo_fixed_string_max_size(const duo_fixed_string_t* s) {
    return s->capacity;
}

/**
 * @brief Returns the remaining available character capacity in fixed string @p s.
 */
static inline size_t duo_fixed_string_remaining(const duo_fixed_string_t* s) {
    return (s->capacity >= s->size) ? (s->capacity - s->size) : 0;
}

/**
 * @brief Returns true if fixed string @p s contains 0 characters.
 */
static inline bool duo_fixed_string_empty(const duo_fixed_string_t* s) {
    return s->size == 0;
}

/**
 * @brief Returns true if fixed string @p s is full (size >= capacity).
 */
static inline bool duo_fixed_string_full(const duo_fixed_string_t* s) {
    return s->size >= s->capacity;
}

/**
 * @brief Returns a guaranteed null-terminated C string pointer in O(1) without allocations.
 * @param s Pointer to fixed string.
 * @return Const pointer to null-terminated character array.
 */
static inline const char* duo_fixed_string_c_str(const duo_fixed_string_t* s) {
    return s->data ? (const char*)s->data : "";
}

/**
 * @brief Returns a mutable character pointer to the buffer of @p s.
 * @param s Pointer to fixed string.
 * @return Mutable character pointer.
 */
static inline char* duo_fixed_string_data(duo_fixed_string_t* s) {
    return s->data;
}

/**
 * @brief Returns an immutable character pointer to the buffer of @p s.
 * @param s Pointer to fixed string.
 * @return Const character pointer.
 */
static inline const char* duo_fixed_string_data_const(const duo_fixed_string_t* s) {
    return duo_fixed_string_c_str(s);
}

/**
 * @brief Returns a mutable pointer to character at index @p idx, or NULL if idx >= size.
 * @param s Pointer to fixed string.
 * @param idx Zero-based character index.
 * @return Pointer to character or NULL if out of bounds.
 */
static inline char* duo_fixed_string_at(duo_fixed_string_t* s, size_t idx) {
    return (idx < s->size) ? &s->data[idx] : NULL;
}

/**
 * @brief Returns an immutable pointer to character at index @p idx, or NULL if idx >= size.
 * @param s Pointer to fixed string.
 * @param idx Zero-based character index.
 * @return Const pointer to character or NULL if out of bounds.
 */
static inline const char* duo_fixed_string_at_const(const duo_fixed_string_t* s, size_t idx) {
    return (idx < s->size) ? &s->data[idx] : NULL;
}

/**
 * @brief Returns a mutable pointer to the first character of @p s, or NULL if empty.
 * @param s Pointer to fixed string.
 * @return Pointer to first character or NULL if empty.
 */
static inline char* duo_fixed_string_front(duo_fixed_string_t* s) {
    return (s->size > 0) ? &s->data[0] : NULL;
}

/**
 * @brief Returns an immutable pointer to the first character of @p s, or NULL if empty.
 * @param s Pointer to fixed string.
 * @return Const pointer to first character or NULL if empty.
 */
static inline const char* duo_fixed_string_front_const(const duo_fixed_string_t* s) {
    return (s->size > 0) ? &s->data[0] : NULL;
}

/**
 * @brief Returns a mutable pointer to the last character of @p s, or NULL if empty.
 * @param s Pointer to fixed string.
 * @return Pointer to last character or NULL if empty.
 */
static inline char* duo_fixed_string_back(duo_fixed_string_t* s) {
    return (s->size > 0) ? &s->data[s->size - 1] : NULL;
}

/**
 * @brief Returns an immutable pointer to the last character of @p s, or NULL if empty.
 * @param s Pointer to fixed string.
 * @return Const pointer to last character or NULL if empty.
 */
static inline const char* duo_fixed_string_back_const(const duo_fixed_string_t* s) {
    return (s->size > 0) ? &s->data[s->size - 1] : NULL;
}

/**
 * @brief Resets size to 0 and writes '\0' at index 0.
 */
static inline void duo_fixed_string_clear(duo_fixed_string_t* s) {
    s->size = 0;
    if (s->data) {
        s->data[0] = '\0';
    }
}

/**
 * @brief Appends @p len characters from @p str to fixed string @p s.
 * Returns true on success, or false if buffer would exceed capacity or str is NULL.
 */
static inline bool duo_fixed_string_append_len(duo_fixed_string_t* s, const char* str, size_t len) {
    if (len == 0) {
        return true;
    }
    if (!str || !s->data) {
        return false;
    }
    if (s->size + len > s->capacity || s->size + len < s->size) {
        return false;
    }
    DUO_MEMCPY(s->data + s->size, str, len);
    s->size += len;
    s->data[s->size] = '\0';
    return true;
}

/**
 * @brief Appends null-terminated C string @p str to fixed string @p s.
 * @param s Pointer to fixed string.
 * @param str Null-terminated string to append.
 * @return true on success, false if capacity exceeded.
 */
static inline bool duo_fixed_string_append(duo_fixed_string_t* s, const char* str) {
    if (!str) {
        return true;
    }
    size_t len = 0;
    while (str[len] != '\0') {
        ++len;
    }
    return duo_fixed_string_append_len(s, str, len);
}

/**
 * @brief Appends a single character @p ch to fixed string @p s.
 * Returns true on success, or false if full.
 * @param s Pointer to fixed string.
 * @param ch Character to append.
 * @return true on success, false if capacity exceeded.
 */
static inline bool duo_fixed_string_push_back(duo_fixed_string_t* s, char ch) {
    if (!s->data || s->size >= s->capacity) {
        return false;
    }
    s->data[s->size++] = ch;
    s->data[s->size] = '\0';
    return true;
}

/**
 * @brief Semantic alias for duo_fixed_string_push_back.
 * @param s Pointer to fixed string.
 * @param ch Character to append.
 * @return true on success, false if capacity exceeded.
 */
static inline bool duo_fixed_string_append_char(duo_fixed_string_t* s, char ch) {
    return duo_fixed_string_push_back(s, ch);
}

/**
 * @brief Removes the last character from fixed string @p s.
 * Returns true on success, or false if empty.
 * @param s Pointer to fixed string.
 * @return true on success, false if string was already empty.
 */
static inline bool duo_fixed_string_pop_back(duo_fixed_string_t* s) {
    if (s->size == 0) {
        return false;
    }
    --s->size;
    s->data[s->size] = '\0';
    return true;
}

/**
 * @brief Inserts @p len characters from @p str at index @p idx in fixed string @p s.
 * Returns true on success, or false if idx > size, str is NULL, or capacity is exceeded.
 * @param s Pointer to fixed string.
 * @param idx Insertion index (0 <= idx <= size).
 * @param str Pointer to characters to insert.
 * @param len Number of characters to insert.
 * @return true on success, false on invalid index or capacity overflow.
 */
static inline bool duo_fixed_string_insert_len(duo_fixed_string_t* s, size_t idx, const char* str, size_t len) {
    if (!s->data || idx > s->size) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (!str) {
        return false;
    }
    if (s->size + len > s->capacity || s->size + len < s->size) {
        return false;
    }
    DUO_MEMMOVE(s->data + idx + len, s->data + idx, s->size - idx);
    DUO_MEMCPY(s->data + idx, str, len);
    s->size += len;
    s->data[s->size] = '\0';
    return true;
}

/**
 * @brief Inserts null-terminated C string @p str at index @p idx in fixed string @p s.
 * @param s Pointer to fixed string.
 * @param idx Insertion index (0 <= idx <= size).
 * @param str Null-terminated string to insert.
 * @return true on success, false on invalid index, str == NULL, or capacity overflow.
 */
static inline bool duo_fixed_string_insert(duo_fixed_string_t* s, size_t idx, const char* str) {
    if (!str) {
        return false;
    }
    size_t len = 0;
    while (str[len] != '\0') {
        ++len;
    }
    return duo_fixed_string_insert_len(s, idx, str, len);
}

/**
 * @brief Erases @p len characters from fixed string @p s starting at index @p idx.
 * Returns true on success, or false if idx + len > size.
 */
static inline bool duo_fixed_string_erase(duo_fixed_string_t* s, size_t idx, size_t len) {
    if (!s->data || idx > s->size || idx + len > s->size || idx + len < idx) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    DUO_MEMMOVE(s->data + idx, s->data + idx + len, s->size - (idx + len));
    s->size -= len;
    s->data[s->size] = '\0';
    return true;
}

/**
 * @brief Resizes @p s to @p new_size. If growing, new elements are set to @p fill_char.
 * Returns true on success, or false if new_size > capacity.
 */
static inline bool duo_fixed_string_resize(duo_fixed_string_t* s, size_t new_size, char fill_char) {
    if (!s->data || new_size > s->capacity) {
        return false;
    }
    if (new_size > s->size) {
        DUO_MEMSET(s->data + s->size, fill_char, new_size - s->size);
    }
    s->size = new_size;
    s->data[s->size] = '\0';
    return true;
}

/**
 * @brief Borrows an immutable duo_str_view_t from fixed string @p s in O(1).
 */
#define duo_fixed_string_as_view(s) \
    duo_str_view_make(duo_fixed_string_c_str(s), duo_fixed_string_size(s))

/**
 * @brief Borrows a generic mutable byte span from fixed string @p s in O(1).
 */
#define duo_fixed_string_as_span(s) \
    duo_span_make((void*)(s)->data, (s)->size)

/**
 * @brief Borrows a generic immutable byte span view from fixed string @p s in O(1).
 */
#define duo_fixed_string_as_span_view(s) \
    duo_span_view_make((const void*)(s)->data, (s)->size)

/**
 * @brief Lexicographical comparison of two fixed strings.
 * Returns <0 if a < b, 0 if a == b, >0 if a > b.
 * @param a First fixed string.
 * @param b Second fixed string.
 * @return Negative if a < b, positive if a > b, 0 if identical.
 */
static inline int duo_fixed_string_cmp(const duo_fixed_string_t* a, const duo_fixed_string_t* b) {
    return duo_str_view_cmp(duo_fixed_string_as_view(a), duo_fixed_string_as_view(b));
}

/**
 * @brief Case-insensitive comparison of two fixed strings.
 */
static inline int duo_fixed_string_casecmp(const duo_fixed_string_t* a, const duo_fixed_string_t* b) {
    return duo_str_view_casecmp(duo_fixed_string_as_view(a), duo_fixed_string_as_view(b));
}

/**
 * @brief Case-insensitive equality of two fixed strings.
 */
static inline bool duo_fixed_string_case_eq(const duo_fixed_string_t* a, const duo_fixed_string_t* b) {
    return duo_fixed_string_casecmp(a, b) == 0;
}

/**
 * @brief Synthesized 6-way relational comparison operations for duo_fixed_string_t*:
 * duo_fixed_string_eq, duo_fixed_string_ne, duo_fixed_string_lt,
 * duo_fixed_string_le, duo_fixed_string_gt, duo_fixed_string_ge.
 */
DUO_C_DERIVE_CMP_OPS(duo_fixed_string, const duo_fixed_string_t*, duo_fixed_string_cmp)

/**
 * @brief Computes 64-bit xxHash3 digest of fixed string @p s.
 */
static inline uint64_t duo_fixed_string_hash(const duo_fixed_string_t* s) {
    return duo_str_view_hash(duo_fixed_string_as_view(s));
}

/**
 * @brief Computes 64-bit seeded xxHash3 digest of fixed string @p s.
 */
static inline uint64_t duo_fixed_string_hash_seed(const duo_fixed_string_t* s, uint64_t seed0, uint64_t seed1) {
    return duo_str_view_hash_seed(duo_fixed_string_as_view(s), seed0, seed1);
}

/**
 * @brief Checks equality of a fixed string with a null-terminated C string.
 * @param a Fixed string.
 * @param str Null-terminated C string.
 * @return true if matching content, false otherwise.
 */
static inline bool duo_fixed_string_eq_cstr(const duo_fixed_string_t* a, const char* str) {
    return duo_str_view_eq(duo_fixed_string_as_view(a), duo_str_view_make_cstr(str));
}

/**
 * @brief Searches for character @p ch in fixed string @p s.
 * Returns index of first occurrence, or (size_t)-1 if not found.
 * @param s Pointer to fixed string.
 * @param ch Character to find.
 * @return Zero-based index of first occurrence, or (size_t)-1 if not found.
 */
static inline size_t duo_fixed_string_find(const duo_fixed_string_t* s, char ch) {
    return duo_str_view_find(duo_fixed_string_as_view(s), ch);
}

/**
 * @brief Allocates an uninitialized fixed-capacity buffer on the stack and binds a duo_fixed_string_t to it.
 * Allocates (Capacity + 1) bytes on the stack, ensuring space for the null terminator.
 * @param Name Identifier of the created duo_fixed_string_t variable
 * @param Capacity Maximum character capacity (excluding null terminator)
 */
#define DUO_C_STACK_STRING(Name, Capacity) \
    char Name##_raw_buf_[(Capacity) + 1] = { 0 }; \
    duo_fixed_string_t Name = { Name##_raw_buf_, 0, (Capacity) }

/**
 * @brief Allocates and initializes a stack string from a string literal.
 * Allocates sizeof(Str) bytes on the stack.
 * @param Name Identifier of the created duo_fixed_string_t variable
 * @param Str String literal
 */
#define DUO_C_STACK_STRING_INIT(Name, Str) \
    char Name##_raw_buf_[] = Str; \
    duo_fixed_string_t Name = { Name##_raw_buf_, sizeof(Name##_raw_buf_) - 1, sizeof(Name##_raw_buf_) - 1 }

/**
 * @brief Allocates an explicit fixed-capacity stack string and initializes it with a string literal.
 * Allocates (Capacity + 1) bytes on the stack.
 * @param Name Identifier of the created duo_fixed_string_t variable
 * @param Capacity Total character capacity (excluding null terminator)
 * @param Str String literal
 */
#define DUO_C_STACK_STRING_OF(Name, Capacity, Str) \
    char Name##_raw_buf_[(Capacity) + 1] = Str; \
    duo_fixed_string_t Name = { Name##_raw_buf_, sizeof(Str) - 1, (Capacity) }

/**
 * @brief Function pointer signature for fixed string stack dispatcher callbacks.
 */
typedef int (*duo_str_visitor_fn)(duo_fixed_string_t* str, void* ctx);

/**
 * @brief Zero-heap scoped stack dispatcher for strings with transparent dynamic heap fallback.
 *
 * Uses tiered stack buffers (33B, 65B, 129B, 257B, 513B, 1025B for chars + null terminator)
 * via a switch statement if capacity <= 1024. If requested capacity exceeds 1024 characters,
 * transparently allocates via DUO_MALLOC(capacity + 1) and frees via DUO_FREE on scope exit.
 *
 * @param capacity Requested character capacity (excluding null terminator).
 * @param fn Callback function invoked with the initialized duo_fixed_string_t and user context.
 * @param ctx User-supplied context pointer passed to callback fn.
 * @return Return value from callback fn, or -1 if dynamic allocation fails or fn is NULL.
 */
static inline int duo_with_stack_str(size_t capacity, duo_str_visitor_fn fn, void* ctx) {
    if (!fn) {
        return -1;
    }
    switch (duo_stack_tier_bytes(capacity)) {
#define DUO_STR_STACK_TIER_CASE_(Cap) \
        case Cap: { \
            char stack_buf[(Cap) + 1]; \
            duo_fixed_string_t s; \
            duo_fixed_string_init(&s, stack_buf, capacity); \
            return fn(&s, ctx); \
        }
        DUO_STR_STACK_TIER_CASE_(8)
        DUO_STR_STACK_TIER_CASE_(16)
        DUO_STR_STACK_TIER_CASE_(24)
        DUO_STR_STACK_TIER_CASE_(32)
        DUO_STR_STACK_TIER_CASE_(40)
        DUO_STR_STACK_TIER_CASE_(48)
        DUO_STR_STACK_TIER_CASE_(56)
        DUO_STR_STACK_TIER_CASE_(64)
        DUO_STR_STACK_TIER_CASE_(80)
        DUO_STR_STACK_TIER_CASE_(96)
        DUO_STR_STACK_TIER_CASE_(112)
        DUO_STR_STACK_TIER_CASE_(128)
        DUO_STR_STACK_TIER_CASE_(160)
        DUO_STR_STACK_TIER_CASE_(192)
        DUO_STR_STACK_TIER_CASE_(224)
        DUO_STR_STACK_TIER_CASE_(256)
        DUO_STR_STACK_TIER_CASE_(320)
        DUO_STR_STACK_TIER_CASE_(384)
        DUO_STR_STACK_TIER_CASE_(448)
        DUO_STR_STACK_TIER_CASE_(512)
        DUO_STR_STACK_TIER_CASE_(640)
        DUO_STR_STACK_TIER_CASE_(768)
        DUO_STR_STACK_TIER_CASE_(896)
        DUO_STR_STACK_TIER_CASE_(1024)
#undef DUO_STR_STACK_TIER_CASE_
        default: {
            char* heap_buf = (char*)DUO_MALLOC((capacity > 0 ? capacity : 1) + 1);
            if (!heap_buf) {
                return -1;
            }
            duo_fixed_string_t s;
            duo_fixed_string_init(&s, heap_buf, capacity);
            int res = fn(&s, ctx);
            DUO_FREE(heap_buf);
            return res;
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* DUO_LINEAR_H */
