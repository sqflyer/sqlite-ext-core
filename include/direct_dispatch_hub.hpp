#ifndef DIRECT_DISPATCH_HUB_HPP
#define DIRECT_DISPATCH_HUB_HPP

#include "stl/duo_hash.hpp"
#include "stl/duo_linear.hpp"
#include "sqlite3_allocator.hpp"
#include "sqlite3_tiny_lock.hpp"
#include "sqlite3_row.hpp"
#include "sqlite3_value_containers.hpp"
#include "direct_dispatch_context.hpp"

/**
 * @class DirectDispatchHub
 * @brief High-performance in-process function registry and zero-overhead dispatcher.
 * 
 * Backed by DuoSTL's freestanding Robin Hood hash map (duo::HashMap<duo::String, DirectDispatchHandler>)
 * with an atomic spinlock (SqliteTinyLock) protecting function registration, lookup, and deletion.
 * Dispatches function invocations using CPU stack-allocated argument spans
 * via withSqliteRowOwned, bypassing SQLite's VDBE virtual machine and bytecode instruction cycles.
 * Fully overloaded across const char*, duo::String, and duo::StringView.
 */
class DirectDispatchHub {
public:
    /** @brief Direct UDF function handler signature. */
    typedef void (*DirectDispatchHandler)(DirectDispatchContext& ctx, SqliteRowOwnedWrapper args);

private:
    static duo::HashMap<duo::String, DirectDispatchHandler> s_entries;
    static SqliteTinyLock s_lock;

    template <typename ArgFiller>
    static inline bool dispatch_impl(DirectDispatchHandler handler, int argc, ArgFiller&& filler, DirectDispatchContext* out_ctx) {
        if (!handler) {
            if (out_ctx) out_ctx->result_error("Function not found in DirectDispatchHub", SQLITE_NOTFOUND);
            return false;
        }

        // Stack-allocates 1..16 arguments with zero heap allocation (falls back to heap for >16)
        return withSqliteRowOwned(argc, [&](SqliteRowOwnedWrapper row) {
            filler(row);

            if (out_ctx) {
                handler(*out_ctx, row);
                return !out_ctx->is_error();
            } else {
                DirectDispatchContext local_ctx;
                handler(local_ctx, row);
                return !local_ctx.is_error();
            }
        });
    }

    static inline bool invoke_impl(DirectDispatchHandler handler, DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
        if (!handler) {
            ctx.result_error("Function not found in DirectDispatchHub", SQLITE_NOTFOUND);
            return false;
        }
        handler(ctx, args);
        return !ctx.is_error();
    }

public:
    DirectDispatchHub() = delete;
    ~DirectDispatchHub() = delete;
    DirectDispatchHub(const DirectDispatchHub&) = delete;
    DirectDispatchHub& operator=(const DirectDispatchHub&) = delete;

    // ------------------------------------------------------------------------
    // Registration Methods
    // ------------------------------------------------------------------------

    /**
     * @brief Registers a direct dispatch handler function pointer with a C-string name.
     * @return true on successful registration, false if name already exists or invalid arguments.
     */
    static inline bool register_function(const char* name, DirectDispatchHandler handler) noexcept {
        if (!name || !handler) return false;
        duo::String key(name);
        SqliteTinyLockGuard guard(s_lock);
        if (s_entries.contains(key)) return false; // Reject duplicate/overwrite
        return s_entries.insert(duo::move(key), handler);
    }

    /**
     * @brief Registers a direct dispatch handler function pointer with a duo::String name.
     * @return true on successful registration, false if name already exists or invalid arguments.
     */
    static inline bool register_function(const duo::String& name, DirectDispatchHandler handler) noexcept {
        if (!handler) return false;
        SqliteTinyLockGuard guard(s_lock);
        if (s_entries.contains(name)) return false; // Reject duplicate/overwrite
        return s_entries.insert(name, handler);
    }

    /**
     * @brief Registers a direct dispatch handler function pointer with a duo::StringView name.
     * @return true on successful registration, false if name already exists or invalid arguments.
     */
    static inline bool register_function(duo::StringView name, DirectDispatchHandler handler) noexcept {
        if (!handler) return false;
        duo::String key(name);
        SqliteTinyLockGuard guard(s_lock);
        if (s_entries.contains(key)) return false; // Reject duplicate/overwrite
        return s_entries.insert(duo::move(key), handler);
    }

    /**
     * @brief Template helper for direct registration of compatible UDF functions or lambdas.
     */
    template <auto UdfFunc>
    static inline bool register_udf(const char* name) noexcept {
        return register_function(name, [](DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
            UdfFunc(ctx, args);
        });
    }

    /**
     * @brief Template helper for direct registration with a duo::String name.
     */
    template <auto UdfFunc>
    static inline bool register_udf(const duo::String& name) noexcept {
        return register_function(name, [](DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
            UdfFunc(ctx, args);
        });
    }

    /**
     * @brief Template helper for direct registration with a duo::StringView name.
     */
    template <auto UdfFunc>
    static inline bool register_udf(duo::StringView name) noexcept {
        return register_function(name, [](DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
            UdfFunc(ctx, args);
        });
    }

    // ------------------------------------------------------------------------
    // Lookup & Inspection Methods
    // ------------------------------------------------------------------------

    /**
     * @brief Performs an O(1) Robin Hood hash lookup for a registered handler with a const char*.
     */
    static inline DirectDispatchHandler find(const char* name) noexcept {
        if (!name) return nullptr;
        duo::String key(name);
        SqliteTinyLockGuard guard(s_lock);
        const DirectDispatchHandler* h = s_entries.get(key);
        return h ? *h : nullptr;
    }

    /**
     * @brief Performs an O(1) Robin Hood hash lookup with a duo::String key.
     */
    static inline DirectDispatchHandler find(const duo::String& name) noexcept {
        SqliteTinyLockGuard guard(s_lock);
        const DirectDispatchHandler* h = s_entries.get(name);
        return h ? *h : nullptr;
    }

    /**
     * @brief Performs an O(1) Robin Hood hash lookup with a duo::StringView key.
     */
    static inline DirectDispatchHandler find(duo::StringView name) noexcept {
        duo::String key(name);
        SqliteTinyLockGuard guard(s_lock);
        const DirectDispatchHandler* h = s_entries.get(key);
        return h ? *h : nullptr;
    }

    /** @brief Checks if a function name is registered in the hub (const char*). */
    static inline bool contains(const char* name) noexcept {
        if (!name) return false;
        duo::String key(name);
        SqliteTinyLockGuard guard(s_lock);
        return s_entries.contains(key);
    }

    /** @brief Checks if a function name is registered in the hub (duo::String). */
    static inline bool contains(const duo::String& name) noexcept {
        SqliteTinyLockGuard guard(s_lock);
        return s_entries.contains(name);
    }

    /** @brief Checks if a function name is registered in the hub (duo::StringView). */
    static inline bool contains(duo::StringView name) noexcept {
        duo::String key(name);
        SqliteTinyLockGuard guard(s_lock);
        return s_entries.contains(key);
    }

    /** @brief Returns the total number of registered functions. */
    static inline size_t size() noexcept {
        SqliteTinyLockGuard guard(s_lock);
        return s_entries.size();
    }

    /** @brief Returns true if the registry contains no functions. */
    static inline bool empty() noexcept {
        SqliteTinyLockGuard guard(s_lock);
        return s_entries.empty();
    }

    // ------------------------------------------------------------------------
    // Unregistration & Removal Methods
    // ------------------------------------------------------------------------

    /** @brief Unregisters a function from the hub by name (const char*). */
    static inline bool unregister_function(const char* name) noexcept {
        return erase(name);
    }

    /** @brief Unregisters a function from the hub by name (duo::String). */
    static inline bool unregister_function(const duo::String& name) noexcept {
        return erase(name);
    }

    /** @brief Unregisters a function from the hub by name (duo::StringView). */
    static inline bool unregister_function(duo::StringView name) noexcept {
        return erase(name);
    }

    /** @brief Alias for unregister_function (const char*). */
    static inline bool unregister(const char* name) noexcept {
        return erase(name);
    }

    /** @brief Alias for unregister_function (duo::String). */
    static inline bool unregister(const duo::String& name) noexcept {
        return erase(name);
    }

    /** @brief Alias for unregister_function (duo::StringView). */
    static inline bool unregister(duo::StringView name) noexcept {
        return erase(name);
    }

    /** @brief Template helper to unregister a UDF by name. */
    template <auto UdfFunc = nullptr>
    static inline bool unregister_udf(const char* name) noexcept {
        return erase(name);
    }

    /** @brief Template helper to unregister a UDF by name. */
    template <auto UdfFunc = nullptr>
    static inline bool unregister_udf(const duo::String& name) noexcept {
        return erase(name);
    }

    /** @brief Template helper to unregister a UDF by name. */
    template <auto UdfFunc = nullptr>
    static inline bool unregister_udf(duo::StringView name) noexcept {
        return erase(name);
    }

    /** @brief Removes a registered function from the hub (const char*). */
    static inline bool erase(const char* name) noexcept {
        if (!name) return false;
        duo::String key(name);
        SqliteTinyLockGuard guard(s_lock);
        return s_entries.erase(key);
    }

    /** @brief Removes a registered function from the hub (duo::String). */
    static inline bool erase(const duo::String& name) noexcept {
        SqliteTinyLockGuard guard(s_lock);
        return s_entries.erase(name);
    }

    /** @brief Removes a registered function from the hub (duo::StringView). */
    static inline bool erase(duo::StringView name) noexcept {
        duo::String key(name);
        SqliteTinyLockGuard guard(s_lock);
        return s_entries.erase(key);
    }

    /** @brief Clears all registered functions from the hub. */
    static inline void clear() noexcept {
        SqliteTinyLockGuard guard(s_lock);
        s_entries.clear();
    }

    /**
     * @brief Dispatches a direct function call using withSqliteRowOwned for stack argument allocation.
     * 
     * Stack-allocates 1..16 arguments directly on the CPU stack with 0 heap allocations, executes
     * @p filler to populate row[0..argc-1], and dispatches execution into the target handler.
     *
     * @tparam ArgFiller Functor with signature `void(SqliteRowOwnedWrapper row)` or `void(SqliteRowOwnedWrapper& row)`
     * @param name Registered function name identifier.
     * @param argc Number of arguments to allocate on the stack.
     * @param filler Functor populating row elements.
     * @param out_ctx Optional caller-provided context receiving the execution result.
     * @return true if function was executed successfully without error, false otherwise.
     */
    template <typename ArgFiller>
    static inline bool dispatch(const char* name, int argc, ArgFiller&& filler, DirectDispatchContext* out_ctx = nullptr) {
        return dispatch_impl(find(name), argc, static_cast<ArgFiller&&>(filler), out_ctx);
    }

    /**
     * @brief Dispatches a direct function call with a duo::String name.
     */
    template <typename ArgFiller>
    static inline bool dispatch(const duo::String& name, int argc, ArgFiller&& filler, DirectDispatchContext* out_ctx = nullptr) {
        return dispatch_impl(find(name), argc, static_cast<ArgFiller&&>(filler), out_ctx);
    }

    /**
     * @brief Dispatches a direct function call with a duo::StringView name.
     */
    template <typename ArgFiller>
    static inline bool dispatch(duo::StringView name, int argc, ArgFiller&& filler, DirectDispatchContext* out_ctx = nullptr) {
        return dispatch_impl(find(name), argc, static_cast<ArgFiller&&>(filler), out_ctx);
    }

    // ------------------------------------------------------------------------
    // Invoke Methods (Pre-Populated Span)
    // ------------------------------------------------------------------------

    /**
     * @brief Invokes the handler directly with a const char* name.
     */
    static inline bool invoke(const char* name, DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
        return invoke_impl(find(name), ctx, args);
    }

    /**
     * @brief Invokes the handler directly with a duo::String name.
     */
    static inline bool invoke(const duo::String& name, DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
        return invoke_impl(find(name), ctx, args);
    }

    /**
     * @brief Invokes the handler directly with a duo::StringView name.
     */
    static inline bool invoke(duo::StringView name, DirectDispatchContext& ctx, SqliteRowOwnedWrapper args) {
        return invoke_impl(find(name), ctx, args);
    }
};

// Static member definitions
inline duo::HashMap<duo::String, DirectDispatchHub::DirectDispatchHandler> DirectDispatchHub::s_entries;
inline SqliteTinyLock DirectDispatchHub::s_lock;

#endif // DIRECT_DISPATCH_HUB_HPP
