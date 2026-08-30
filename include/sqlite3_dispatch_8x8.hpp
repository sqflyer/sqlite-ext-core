#ifndef SQLITE3_DISPATCH_8X8_HPP
#define SQLITE3_DISPATCH_8X8_HPP

#include <sqlite3.h>
#include <stddef.h>
#include "sqlite3_value_containers.hpp"
#include "sqlite3_allocator.hpp"

// ============================================================================
// 1. 1D GENERIC COMPILE-TIME DISPATCHER (Arity 1..8 + Fallback >= 9)
// ============================================================================

/**
 * @def SQLITE_DISPATCH_1D_8
 * @brief Dispatches a runtime count (1..8) to a compile-time constexpr size_t 'N'.
 * 
 * Usage:
 *   SQLITE_DISPATCH_1D_8(ColsN, runtime_count, {
 *       return sqlite_new<MyContainer<SqliteValueVec<ColsN>>>(arg1, arg2);
 *   });
 */
#define SQLITE_DISPATCH_1D_8(N, runtime_count, ...) \
    switch ((runtime_count) <= 0 ? 1 : (runtime_count)) { \
        case 1:  { constexpr size_t N = 1; __VA_ARGS__; } break; \
        case 2:  { constexpr size_t N = 2; __VA_ARGS__; } break; \
        case 3:  { constexpr size_t N = 3; __VA_ARGS__; } break; \
        case 4:  { constexpr size_t N = 4; __VA_ARGS__; } break; \
        case 5:  { constexpr size_t N = 5; __VA_ARGS__; } break; \
        case 6:  { constexpr size_t N = 6; __VA_ARGS__; } break; \
        case 7:  { constexpr size_t N = 7; __VA_ARGS__; } break; \
        case 8:  { constexpr size_t N = 8; __VA_ARGS__; } break; \
        default: { constexpr size_t N = 0; __VA_ARGS__; } break; /* Dynamic Heap Fallback */ \
    }

// ============================================================================
// 2. 2D GENERIC COMPILE-TIME DISPATCHER (8x8 = 64 Matrix Combinations)
// ============================================================================

/**
 * @def SQLITE_DISPATCH_2D_8X8
 * @brief Dispatches runtime key_count (1..8) and val_count (1..8) to compile-time 
 *        constexpr size_t 'KeyN' and 'ValN' variables inside the provided block.
 * 
 * Works with ANY container, template types, or custom factory logic.
 * 
 * Usage:
 *   SQLITE_DISPATCH_2D_8X8(KeyN, ValN, pk_count, val_count, {
 *       return sqlite_new<MyContainer<SqliteValueTuple<KeyN>, SqliteValueVec<ValN>>>(args...);
 *   });
 */
#define SQLITE_DISPATCH_2D_8X8(KeyN, ValN, pk_count, val_count, ...) \
    switch ((pk_count) <= 0 ? 1 : (pk_count)) { \
        case 1:  { constexpr size_t KeyN = 1; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 2:  { constexpr size_t KeyN = 2; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 3:  { constexpr size_t KeyN = 3; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 4:  { constexpr size_t KeyN = 4; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 5:  { constexpr size_t KeyN = 5; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 6:  { constexpr size_t KeyN = 6; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 7:  { constexpr size_t KeyN = 7; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        case 8:  { constexpr size_t KeyN = 8; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
        default: { constexpr size_t KeyN = 0; SQLITE_DISPATCH_1D_8(ValN, val_count, __VA_ARGS__) } break; \
    }

// ============================================================================
// 3. SHORTHAND FACTORY MACROS (Direct sqlite_new Instantiation)
// ============================================================================

/**
 * @def SQLITE_MAKE_STORAGE_8X8
 * @brief Generic 8x8 instantiator passing explicit Container, Key, and Value templates.
 */
#define SQLITE_MAKE_STORAGE_8X8(ContainerT, KeyT, ValT, pk_count, val_count, ...) \
    SQLITE_DISPATCH_2D_8X8(_K_N, _V_N, pk_count, val_count, { \
        return sqlite_new<ContainerT<KeyT<_K_N>, ValT<_V_N>>>(__VA_ARGS__); \
    })

/**
 * @def SQLITE_MAKE_DEFAULT_STORAGE_8X8
 * @brief Shorthand 8x8 instantiator using standard SqliteValueTuple for Key and SqliteValueVec for Value.
 */
#define SQLITE_MAKE_DEFAULT_STORAGE_8X8(ContainerT, pk_count, val_count, ...) \
    SQLITE_MAKE_STORAGE_8X8(ContainerT, SqliteValueTuple, SqliteValueVec, pk_count, val_count, __VA_ARGS__)

#endif // SQLITE3_DISPATCH_8X8_HPP
