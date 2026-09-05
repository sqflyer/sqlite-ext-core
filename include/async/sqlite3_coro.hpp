#ifndef SQLITE3_CORO_HPP
#define SQLITE3_CORO_HPP

/**
 * @file sqlite3_coro.hpp
 * @brief Zero-dependency C++11/C++20 Coroutine, Generator & Fiber Subsystem for SQLite Extensions.
 *
 * ## Architectural Overview
 * Modern, high-performance C++ wrapper providing ergonomic coroutine and generator abstractions
 * specifically engineered for SQLite table-valued functions (TVFs), stream processing, and background tasks:
 *
 * 1. **C++11 Stackful Coroutines (`SqliteCoroutine`)**:
 *    - Move-only RAII wrappers around underlying native fibers (`sqlite3_coro.h`).
 *    - Type-erased closure execution without virtual tables (`vtable`) or `<functional>` overhead.
 *    - Capturing lambdas and move-only functors allocated strictly via `sqlite_new` and `sqlite_delete`.
 *
 * 2. **C++11 Range-Based Iterators & TVF Streaming (`SqliteFiberGenerator<T>`)**:
 *    - Allows database extensions to yield rows/values cleanly across yields:
 *      `for (const MyRow& row : gen) { ... }`
 *    - Eliminates complex state-machine boilerplate in SQLite Virtual Table `xNext` implementations.
 *
 * 3. **Freestanding C++20 Stackless Coroutines (`SqliteGenerator<T>`, `co_yield`)**:
 *    - Minimalist compiler coroutine traits defined without standard library headers (`<coroutine>`).
 *    - Routes frame allocations directly through `sqlite3_malloc64` and `sqlite3_free`.
 *    - 100% compliant with `-nostdlib`, `-nostdlib++`, `-fno-exceptions`, and `-fno-rtti`.
 *
 * @code
 * // C++11 Stackful Generator Example:
 * SqliteFiberGenerator<int> gen([](const SqliteFiberGenerator<int>::YieldHandle& yield) {
 *     for (int i = 0; i < 5; ++i) {
 *         yield(i * 10);
 *     }
 * });
 * for (int val : gen) {
 *     printf("Yielded: %d\n", val);
 * }
 * @endcode
 */

#include "sqlite3_coro.h"
#include "../sqlite3_allocator.hpp"
#include <sqlite3.h>

// ============================================================================
// 1. C++11 Stackful Coroutine Wrapper (`SqliteCoroutine`)
// ============================================================================

/**
 * @class SqliteCoroutine
 * @brief Move-only RAII wrapper around a freestanding stackful coroutine/fiber.
 *
 * Supports free functions, stateless lambdas, and capturing closures without
 * virtual tables (`vtable`) or standard library runtime dependencies. All dynamic
 * closure holders are managed strictly via `sqlite_new` and `sqlite_delete` (sqlite3_malloc/free).
 *
 * @code
 * int counter = 0;
 * SqliteCoroutine coro([&counter]() {
 *     counter += 10;
 *     SqliteCoroutine::yield();
 *     counter += 20;
 * });
 * coro.resume();
 * assert(counter == 10);
 * coro.resume();
 * assert(counter == 30);
 * @endcode
 */
class SqliteCoroutine {
private:
    sqlite3_coro_t m_coro;   /**< Underlying Pure C coroutine handle. */
    bool           m_valid;  /**< Tracks validity of the active fiber handle. */

    /**
     * @struct CallableHolderBase
     * @brief Type-erased non-virtual function-pointer table for closure execution.
     */
    struct CallableHolderBase {
        void (*invoke_fn)(CallableHolderBase*);   /**< Invocation function pointer. */
        void (*destroy_fn)(CallableHolderBase*);  /**< Destruction function pointer. */
    };

    /**
     * @struct CallableHolder
     * @brief Templated closure container storing capturing callable objects.
     * @tparam F Type of the callable closure.
     */
    template <typename F>
    struct CallableHolder : public CallableHolderBase {
        F func;  /**< Stored callable object / closure. */

        /**
         * @brief Constructs the holder and binds static invocation/destruction trampolines.
         * @param f Rvalue reference to callable object.
         */
        CallableHolder(F&& f) : func(sqlite_forward<F>(f)) {
            this->invoke_fn = &invoke_impl;
            this->destroy_fn = &destroy_impl;
        }

        /** @brief Static trampoline invoking the stored callable. */
        static void invoke_impl(CallableHolderBase* self) {
            static_cast<CallableHolder<F>*>(self)->func();
        }

        /** @brief Static trampoline freeing the holder via sqlite_delete. */
        static void destroy_impl(CallableHolderBase* self) {
            sqlite_delete(static_cast<CallableHolder<F>*>(self));
        }
    };

    /**
     * @brief Static entry trampoline for coroutine execution.
     * @param arg Pointer to CallableHolderBase instance.
     */
    static void coro_trampoline(void* arg) {
        CallableHolderBase* holder = static_cast<CallableHolderBase*>(arg);
        if (holder) {
            if (holder->invoke_fn) {
                holder->invoke_fn(holder);
            }
            if (holder->destroy_fn) {
                holder->destroy_fn(holder);
            }
        }
    }

    /**
     * @brief Static entry trampoline for parameterless function pointers.
     * @param arg Raw function pointer cast to `void*`.
     */
    static void fn_trampoline(void* arg) {
        typedef void (*RawFn)();
        RawFn fn = reinterpret_cast<RawFn>(arg);
        if (fn) fn();
    }

public:
    /**
     * @brief Constructs an uninitialized, invalid coroutine handle.
     */
    inline SqliteCoroutine() noexcept : m_valid(false) {
        m_coro.state = nullptr;
    }

    /**
     * @brief Constructs and initializes a stackful coroutine from a raw function pointer.
     * @param fn Function pointer of type void (*fn)().
     * @param stack_size Stack size in bytes (defaults to 64KB).
     */
    inline explicit SqliteCoroutine(void (*fn)(), size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE)
        : m_valid(false)
    {
        m_coro.state = nullptr;
        if (!fn) return;
        int rc = sqlite3_coro_create(&m_coro, stack_size, fn_trampoline, reinterpret_cast<void*>(fn));
        m_valid = (rc == SQLITE_OK);
    }

    /**
     * @brief Constructs and initializes a stackful coroutine with a callable closure or lambda.
     *
     * @tparam Callable Callable type (lambda or functor).
     * @param callable The callable closure to execute cooperatively.
     * @param stack_size Stack size in bytes (defaults to 64KB).
     */
    template <typename Callable>
    explicit SqliteCoroutine(Callable callable, size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE)
        : m_valid(false)
    {
        m_coro.state = nullptr;
        CallableHolder<Callable>* holder = sqlite_new<CallableHolder<Callable>>(sqlite_forward<Callable>(callable));
        if (!holder) return;

        int rc = sqlite3_coro_create(&m_coro, stack_size, coro_trampoline, holder);
        if (rc == SQLITE_OK) {
            m_valid = true;
        } else {
            sqlite_delete(holder);
        }
    }

    /** @brief Copy construction is prohibited (move-only semantics). */
    SqliteCoroutine(const SqliteCoroutine&) = delete;

    /** @brief Copy assignment is prohibited (move-only semantics). */
    SqliteCoroutine& operator=(const SqliteCoroutine&) = delete;

    /**
     * @brief Move constructor. Transfers coroutine ownership in 1 CPU cycle.
     * @param other Rvalue reference to source coroutine.
     */
    inline SqliteCoroutine(SqliteCoroutine&& other) noexcept
        : m_coro(other.m_coro), m_valid(other.m_valid)
    {
        other.m_valid = false;
        other.m_coro.state = nullptr;
    }

    /**
     * @brief Move assignment operator. Transfers ownership, safely releasing existing resources.
     * @param other Rvalue reference to source coroutine.
     * @return Reference to this instance.
     */
    inline SqliteCoroutine& operator=(SqliteCoroutine&& other) noexcept {
        if (this != &other) {
            destroy();
            m_coro = other.m_coro;
            m_valid = other.m_valid;
            other.m_valid = false;
            other.m_coro.state = nullptr;
        }
        return *this;
    }

    /**
     * @brief Destructor. Destroys the underlying fiber/stack resources.
     */
    inline ~SqliteCoroutine() {
        destroy();
    }

    /**
     * @brief Resumes the coroutine from its last yield point.
     * @return SQLITE_OK on success, or SQLITE_MISUSE if already finished or invalid.
     */
    inline int resume() noexcept {
        if (!m_valid || is_done()) return SQLITE_MISUSE;
        return sqlite3_coro_resume(&m_coro);
    }

    /**
     * @brief Yields execution back to the caller from inside the active coroutine.
     */
    static inline void yield() noexcept {
        sqlite3_coro_yield();
    }

    /**
     * @brief Yields execution and transfers a pointer value to the caller.
     * @param val Pointer to pass to caller.
     */
    static inline void yield_value(void* val) noexcept {
        sqlite3_coro_yield_value(val);
    }

    /**
     * @brief Retrieves the last value yielded by this coroutine.
     * @return Pointer yielded by the coroutine, or nullptr if none.
     */
    inline void* get_value() const noexcept {
        return m_valid ? sqlite3_coro_get_value(&m_coro) : nullptr;
    }

    /**
     * @brief Returns true if the coroutine has finished execution.
     * @return True if done or uninitialized.
     */
    inline bool is_done() const noexcept {
        return !m_valid || sqlite3_coro_is_done(&m_coro) != 0;
    }

    /**
     * @brief Returns true if this handle holds an active coroutine.
     * @return True if initialized and valid.
     */
    inline bool is_valid() const noexcept {
        return m_valid;
    }

    /**
     * @brief Explicit boolean conversion checking validity and active state.
     * @return True if valid and not yet finished.
     */
    inline explicit operator bool() const noexcept {
        return m_valid && !is_done();
    }

    /**
     * @brief Releases fiber resources and marks the handle invalid.
     */
    inline void destroy() noexcept {
        if (m_valid) {
            sqlite3_coro_destroy(&m_coro);
            m_valid = false;
        }
    }
};

// ============================================================================
// 2. C++11 Stackful Generator (`SqliteFiberGenerator<T>`)
// ============================================================================

/**
 * @class SqliteFiberGenerator
 * @brief Typed cooperative generator producing a sequence of T values via stackful fibers.
 *
 * Supports standard C++11 range-based for loops (`for (const T& val : gen)`).
 * Ideal for streaming Table-Valued Functions (TVF) and virtual tables.
 *
 * @tparam T Value type yielded by the generator.
 */
template <typename T>
class SqliteFiberGenerator {
public:
    /**
     * @class YieldHandle
     * @brief Functor passed to generator routines to yield values cleanly.
     */
    class YieldHandle {
    public:
        /**
         * @brief Yields an lvalue reference value to the consumer.
         * 
         * The yielded value's address is preserved across the stackful context switch.
         * The fiber's stack frame is frozen in place, ensuring the reference remains valid
         * while the consumer caller inspects the value.
         */
        inline void operator()(const T& val) const {
            sqlite3_coro_yield_value(const_cast<void*>(static_cast<const void*>(&val)));
        }

        /**
         * @brief Yields an rvalue reference value to the consumer.
         * 
         * The temporary's address remains valid on the frozen fiber stack until the
         * fiber is resumed and the temporary expression completes.
         */
        inline void operator()(T&& val) const {
            sqlite3_coro_yield_value(const_cast<void*>(static_cast<const void*>(&val)));
        }
    };

private:
    SqliteCoroutine m_coro;          /**< Underlying stackful coroutine instance. */
    const T*        m_current_val;   /**< Pointer to the most recently yielded value. */

public:
    /**
     * @brief Constructs and starts a stackful generator from a callable.
     * @tparam F Callable taking `const YieldHandle&`.
     * @param func Generator function body.
     * @param stack_size Stack size in bytes (defaults to 64KB).
     */
    template <typename F>
    explicit SqliteFiberGenerator(F&& func, size_t stack_size = SQLITE3_CORO_DEFAULT_STACK_SIZE)
        : m_current_val(nullptr)
    {
        YieldHandle y;
        m_coro = SqliteCoroutine([func, y]() {
            func(y);
        }, stack_size);
        advance();
    }

    /** @brief Non-copyable. */
    SqliteFiberGenerator(const SqliteFiberGenerator&) = delete;
    SqliteFiberGenerator& operator=(const SqliteFiberGenerator&) = delete;

    /**
     * @brief Move constructor.
     * @param other Source generator.
     */
    inline SqliteFiberGenerator(SqliteFiberGenerator&& other) noexcept
        : m_coro(sqlite_move(other.m_coro)), m_current_val(other.m_current_val)
    {
        other.m_current_val = nullptr;
    }

    /**
     * @brief Move assignment operator.
     * @param other Source generator.
     * @return Reference to this instance.
     */
    inline SqliteFiberGenerator& operator=(SqliteFiberGenerator&& other) noexcept {
        if (this != &other) {
            m_coro = sqlite_move(other.m_coro);
            m_current_val = other.m_current_val;
            other.m_current_val = nullptr;
        }
        return *this;
    }

    /**
     * @brief Advances the generator to the next yielded item.
     * @return True if a new value is available, false if finished.
     */
    inline bool next() noexcept {
        advance();
        return m_current_val != nullptr;
    }

    /**
     * @brief Returns the currently active yielded value.
     * @return Const reference to current value.
     */
    inline const T& value() const noexcept {
        return *m_current_val;
    }

    /**
     * @brief Returns true if the generator has reached completion.
     * @return True if no more values will be produced.
     */
    inline bool is_done() const noexcept {
        return m_current_val == nullptr && m_coro.is_done();
    }

    /**
     * @class Iterator
     * @brief Forward iterator adapter for standard C++11 range-based for loops.
     */
    class Iterator {
    private:
        SqliteFiberGenerator* m_gen;  /**< Pointer to parent generator. */
    public:
        /** @brief Constructs an iterator pointing to generator state. */
        inline explicit Iterator(SqliteFiberGenerator* gen) noexcept : m_gen(gen) {}

        /** @brief Dereferences current value. */
        inline const T& operator*() const noexcept { return m_gen->value(); }

        /** @brief Prefix increment advancing to next yielded value. */
        inline Iterator& operator++() noexcept { m_gen->advance(); return *this; }

        /** @brief Equality operator for loop termination. */
        inline bool operator==(const Iterator& o) const noexcept {
            bool a_end = !m_gen || m_gen->is_done();
            bool b_end = !o.m_gen || o.m_gen->is_done();
            return (a_end && b_end) || (m_gen == o.m_gen);
        }

        /** @brief Inequality operator for loop execution. */
        inline bool operator!=(const Iterator& o) const noexcept { return !(*this == o); }
    };

    /** @brief Returns an iterator to the first yielded value. */
    inline Iterator begin() noexcept { return Iterator(this); }

    /** @brief Returns an iterator representing the end sentinel. */
    inline Iterator end() noexcept { return Iterator(nullptr); }

private:
    /** @brief Internal helper to resume fiber and read next yielded value. */
    inline void advance() noexcept {
        if (m_coro.is_done()) {
            m_current_val = nullptr;
            return;
        }
        m_coro.resume();
        if (m_coro.is_done()) {
            m_current_val = nullptr;
        } else {
            m_current_val = static_cast<const T*>(m_coro.get_value());
        }
    }
};

// ============================================================================
// 3. Freestanding C++20 Stackless Coroutines (`co_yield` / `SqliteGenerator<T>`)
// ============================================================================

#if defined(__cpp_impl_coroutine) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L) || (defined(__cplusplus) && __cplusplus >= 202002L)

#define SQLITE_HAS_CPP20_COROUTINES 1

/**
 * @brief Minimal freestanding C++20 coroutine traits defined without standard library headers.
 */
namespace std {
    template <typename ReturnType, typename... Args>
    struct coroutine_traits;

    template <typename Promise = void>
    struct coroutine_handle;

    template <>
    struct coroutine_handle<void> {
        void* _ptr = nullptr;
        constexpr coroutine_handle() noexcept : _ptr(nullptr) {}
        constexpr coroutine_handle(void* p) noexcept : _ptr(p) {}
        static coroutine_handle from_address(void* p) noexcept { return coroutine_handle(p); }
        constexpr void* address() const noexcept { return _ptr; }
        constexpr explicit operator bool() const noexcept { return _ptr != nullptr; }
        void resume() const { __builtin_coro_resume(_ptr); }
        void destroy() const { __builtin_coro_destroy(_ptr); }
        bool done() const { return __builtin_coro_done(_ptr); }
    };

    template <typename Promise>
    struct coroutine_handle {
        void* _ptr = nullptr;
        constexpr coroutine_handle() noexcept : _ptr(nullptr) {}
        constexpr coroutine_handle(void* p) noexcept : _ptr(p) {}
        static coroutine_handle from_address(void* p) noexcept { return coroutine_handle(p); }
        static coroutine_handle from_promise(Promise& p) noexcept {
            return coroutine_handle(__builtin_coro_promise(&p, alignof(Promise), true));
        }
        constexpr void* address() const noexcept { return _ptr; }
        constexpr explicit operator bool() const noexcept { return _ptr != nullptr; }
        void resume() const { __builtin_coro_resume(_ptr); }
        void destroy() const { __builtin_coro_destroy(_ptr); }
        bool done() const { return __builtin_coro_done(_ptr); }
        Promise& promise() const {
            return *static_cast<Promise*>(__builtin_coro_promise(_ptr, alignof(Promise), false));
        }
    };

    struct suspend_always {
        constexpr bool await_ready() const noexcept { return false; }
        constexpr void await_suspend(coroutine_handle<>) const noexcept {}
        constexpr void await_resume() const noexcept {}
    };

    struct suspend_never {
        constexpr bool await_ready() const noexcept { return true; }
        constexpr void await_suspend(coroutine_handle<>) const noexcept {}
        constexpr void await_resume() const noexcept {}
    };
} // namespace std

template <typename T>
class SqliteGenerator;

namespace std {
    template <typename T, typename... Args>
    struct coroutine_traits<SqliteGenerator<T>, Args...> {
        using promise_type = typename SqliteGenerator<T>::promise_type;
    };
}

/**
 * @class SqliteGenerator
 * @brief C++20 Stackless Coroutine Generator using `co_yield` with 0 standard library headers.
 *
 * Routes compiler coroutine frame allocation strictly through sqlite3_malloc64 and sqlite3_free.
 *
 * @tparam T Value type yielded by the generator.
 */
template <typename T>
class SqliteGenerator {
public:
    /**
     * @struct promise_type
     * @brief Coroutine promise managing value yielding and frame memory lifecycles.
     */
    struct promise_type {
        const T* current_val = nullptr;  /**< Pointer to last yielded item. */

        /** @brief Routes frame memory allocation through sqlite3_malloc64. */
        void* operator new(size_t sz) {
            return sqlite3_malloc64(static_cast<sqlite3_uint64>(sz));
        }

        /** @brief Routes frame memory deallocation through sqlite3_free. */
        void operator delete(void* ptr) {
            sqlite3_free(ptr);
        }

        /** @brief Returns generator return object. */
        SqliteGenerator get_return_object() {
            return SqliteGenerator(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        /** @brief Suspends initially on creation (lazy evaluation). */
        std::suspend_always initial_suspend() noexcept { return {}; }

        /** @brief Suspends on completion prior to destruction. */
        std::suspend_always final_suspend() noexcept { return {}; }

        /** @brief Handles co_return. */
        void return_void() noexcept {}

        /** @brief Handles exceptions in -fno-exceptions environments. */
        void unhandled_exception() noexcept {}

        /** @brief Intercepts co_yield expressions. */
        std::suspend_always yield_value(const T& val) noexcept {
            current_val = &val;
            return {};
        }
    };

private:
    std::coroutine_handle<promise_type> m_handle;  /**< Compiler coroutine handle. */

public:
    /** @brief Constructs generator from a compiler coroutine handle. */
    inline explicit SqliteGenerator(std::coroutine_handle<promise_type> h) noexcept : m_handle(h) {}

    /** @brief Non-copyable. */
    SqliteGenerator(const SqliteGenerator&) = delete;
    SqliteGenerator& operator=(const SqliteGenerator&) = delete;

    /** @brief Move constructor. */
    inline SqliteGenerator(SqliteGenerator&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    /** @brief Move assignment operator. */
    inline SqliteGenerator& operator=(SqliteGenerator&& other) noexcept {
        if (this != &other) {
            if (m_handle) m_handle.destroy();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    /** @brief Destructor. Destroys the compiler coroutine frame via sqlite3_free. */
    inline ~SqliteGenerator() {
        if (m_handle) m_handle.destroy();
    }

    /**
     * @brief Advances the generator to the next co_yield point.
     * @return True if a new value was produced, false if finished.
     */
    inline bool next() noexcept {
        if (!m_handle || m_handle.done()) return false;
        m_handle.resume();
        return !m_handle.done();
    }

    /**
     * @brief Retrieves the active yielded value.
     * @return Const reference to value.
     */
    inline const T& value() const noexcept {
        return *m_handle.promise().current_val;
    }

    /**
     * @brief Returns true if the generator has reached completion.
     * @return True if done.
     */
    inline bool is_done() const noexcept {
        return !m_handle || m_handle.done();
    }

    /**
     * @class Iterator
     * @brief Forward iterator adapter for standard C++11/C++20 range-based for loops.
     */
    class Iterator {
    private:
        SqliteGenerator* m_gen;  /**< Pointer to active generator. */
    public:
        /** @brief Constructs iterator pointing to generator. */
        inline explicit Iterator(SqliteGenerator* gen) noexcept : m_gen(gen) {}

        /** @brief Dereferences current value. */
        inline const T& operator*() const noexcept { return m_gen->value(); }

        /** @brief Advances generator. */
        inline Iterator& operator++() noexcept { m_gen->next(); return *this; }

        /** @brief Equality check. */
        inline bool operator==(const Iterator& o) const noexcept {
            bool a_end = !m_gen || m_gen->is_done();
            bool b_end = !o.m_gen || o.m_gen->is_done();
            return (a_end && b_end) || (m_gen == o.m_gen);
        }

        /** @brief Inequality check. */
        inline bool operator!=(const Iterator& o) const noexcept { return !(*this == o); }
    };

    /** @brief Returns an iterator to the first yielded value. */
    inline Iterator begin() noexcept {
        if (m_handle && !m_handle.done()) {
            m_handle.resume();
        }
        return Iterator(this);
    }

    /** @brief Returns an iterator representing the end sentinel. */
    inline Iterator end() noexcept { return Iterator(nullptr); }
};

#else

/**
 * @brief C++11 fallback alias: SqliteGenerator maps directly to SqliteFiberGenerator<T>.
 */
template <typename T>
using SqliteGenerator = SqliteFiberGenerator<T>;

#endif /* SQLITE_HAS_CPP20_COROUTINES */

#include "sqlite3_coro_sched.hpp"

#endif /* SQLITE3_CORO_HPP */
