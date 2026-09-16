#ifndef DIRECT_DISPATCH_CONTEXT_HPP
#define DIRECT_DISPATCH_CONTEXT_HPP

#include <sqlite3.h>
#include "sqlite3_value.hpp"
#include "sqlite3_conn_state.hpp"
#include "sqlite3_ext_state.hpp"

/**
 * @class DirectDispatchContext
 * @brief Zero-overhead direct in-memory execution context initialized with sqlite3* db and user_data.
 * 
 * 1:1 mimics the modern SqliteContext API contract while storing the execution result directly in
 * an in-situ 24-byte SqliteValueOwned member. Decoupled from SQLite's Virtual Database Engine (VDBE),
 * opcode execution loop, and bytecode compilation.
 *
 * Can be passed by reference (DirectDispatchContext&) interchangeably into any C++ UDF written against
 * SqliteContext or generic templated UDF implementations.
 */
class DirectDispatchContext {
private:
    sqlite3*         m_db;
    void*            m_user_data;
    SqliteValueOwned m_result;
    const char*      m_error_msg;
    int              m_error_code;
    bool             m_has_error;

public:
    /** 
     * @brief Constructs a direct dispatch context bound to a database connection and optional user data.
     * @param db The raw SQLite database connection handle (defaults to nullptr).
     * @param user_data Optional injected state/aux pointer (defaults to nullptr).
     */
    inline explicit DirectDispatchContext(sqlite3* db = nullptr, void* user_data = nullptr) noexcept
        : m_db(db),
          m_user_data(user_data),
          m_result(),
          m_error_msg(nullptr),
          m_error_code(SQLITE_OK),
          m_has_error(false) {}

    // Non-copyable to prevent accidental slicing/deep copies, movable
    DirectDispatchContext(const DirectDispatchContext&) = delete;
    DirectDispatchContext& operator=(const DirectDispatchContext&) = delete;
    DirectDispatchContext(DirectDispatchContext&&) noexcept = default;
    DirectDispatchContext& operator=(DirectDispatchContext&&) noexcept = default;

    // ========================================================================
    // Database Handle & State Resolution (SqliteContext 1:1 Parity)
    // ========================================================================

    /** @brief Retrieves the parent sqlite3* database connection handle. */
    inline sqlite3* db_handle() const noexcept { return m_db; }

    /** @brief Alias for db_handle(). */
    inline sqlite3* db() const noexcept { return m_db; }

    /** @brief Binds/updates the raw SQLite database connection handle. */
    inline void set_db(sqlite3* db) noexcept { m_db = db; }

    /** @brief Retrieves the injected user data / state pointer. */
    inline void* user_data() const noexcept { return m_user_data; }

    /** @brief Binds/updates the injected user data / state pointer. */
    inline void set_user_data(void* data) noexcept { m_user_data = data; }

    /**
     * @brief Resolves strongly-typed shared extension state associated with this context or database.
     * Checks the injected user_data first, then sqlite3_get_clientdata (SQLite 3.44+),
     * and falls back to SqliteExtState<State>::from_db(m_db).
     *
     * @tparam State User-defined shared state struct type.
     * @return Strongly-typed State* pointer, or nullptr if unavailable.
     */
    template <typename State>
    inline State* state() const noexcept {
        if (m_user_data) {
            return static_cast<State*>(m_user_data);
        }
#if defined(SQLITE_VERSION_NUMBER) && (SQLITE_VERSION_NUMBER >= 3044000)
        const char* tag = SqlitePointerTraits<State>::name();
        if (tag && m_db) {
            void* cd = sqlite3_get_clientdata(m_db, tag);
            if (cd) return static_cast<State*>(cd);
        }
#endif
        if (m_db) {
            return SqliteExtState<State>::from_db(nullptr, m_db);
        }
        return nullptr;
    }

    /**
     * @brief Resolves per-connection unique state associated with this database connection.
     *
     * @tparam State User-defined per-connection state struct type.
     * @return Strongly-typed State* pointer, or nullptr if unavailable.
     */
    template <typename State>
    inline State* conn_state() const noexcept {
        if (m_db) {
            return SqliteConnState<State>::from_db(m_db);
        }
        return nullptr;
    }

    // ========================================================================
    // Result Setters - Primitives (SqliteContext 1:1 Parity)
    // ========================================================================

    /** @brief Sets a 32-bit signed integer result. */
    inline void result_int(int iVal) noexcept {
        result_int64(static_cast<sqlite3_int64>(iVal));
    }

    /** @brief Sets a 64-bit signed integer result. */
    inline void result_int64(sqlite3_int64 iVal) noexcept {
        m_result.set_integer(iVal);
        m_has_error = false;
    }

    /** @brief Sets a 64-bit IEEE floating-point double result. */
    inline void result_double(double dVal) noexcept {
        m_result.set_float(dVal);
        m_has_error = false;
    }

    /** @brief Sets a boolean result tagged with SQLITE_SUBTYPE_BOOL. */
    inline void result_bool(bool val) noexcept {
        m_result = SqliteValueOwned::from_bool(val);
        m_has_error = false;
    }

    /** @brief Sets an SQL NULL result. */
    inline void result_null() noexcept {
        m_result.set_null();
        m_has_error = false;
    }

    // ========================================================================
    // Result Setters - Strings & Blobs (SqliteContext 1:1 Parity)
    // ========================================================================

    /**
     * @brief Sets a UTF-8 text string result on this context.
     * 
     * Passing SQLITE_STATIC automatically adopts the external buffer zero-copy without allocation
     * using SqliteValueOwned::from_borrowed_text().
     *
     * @param z Pointer to UTF-8 text buffer.
     * @param n Length in bytes (or -1 for null-terminated).
     * @param free_func Ownership callback (SQLITE_TRANSIENT, SQLITE_STATIC, or custom cleanup function).
     */
    inline void result_text(const char* z, int n = -1, void (*free_func)(void*) = SQLITE_TRANSIENT) {
        if (!z) {
            result_null();
            return;
        }
        if (free_func == SQLITE_STATIC) {
            m_result = SqliteValueOwned::from_borrowed_text(z, n);
            m_has_error = false;
            return;
        }
        m_result.set_text(z, n);
        m_has_error = false;
        if (free_func != SQLITE_TRANSIENT && free_func != nullptr) {
            free_func(const_cast<char*>(z));
        }
    }

    /** @brief Sets a UTF-8 text string result from a SqliteStringView. */
    inline void result_text(SqliteStringView str, void (*free_func)(void*) = SQLITE_TRANSIENT) {
        result_text(str.data(), str.length(), free_func);
    }

    /**
     * @brief Sets a binary BLOB result on this context.
     * 
     * Passing SQLITE_STATIC automatically adopts the external buffer zero-copy without allocation
     * using SqliteValueOwned::from_borrowed_blob().
     *
     * @param z Pointer to raw binary payload buffer.
     * @param n Byte length of payload.
     * @param free_func Ownership callback (SQLITE_TRANSIENT, SQLITE_STATIC, or custom cleanup function).
     */
    inline void result_blob(const void* z, int n, void (*free_func)(void*) = SQLITE_TRANSIENT) {
        if (!z) {
            result_null();
            return;
        }
        if (free_func == SQLITE_STATIC) {
            m_result = SqliteValueOwned::from_borrowed_blob(z, n);
            m_has_error = false;
            return;
        }
        m_result.set_blob(z, n);
        m_has_error = false;
        if (free_func != SQLITE_TRANSIENT && free_func != nullptr) {
            free_func(const_cast<void*>(z));
        }
    }

    /** @brief Sets a binary BLOB result from a SqliteBlobView. */
    inline void result_blob(const SqliteBlobView& blob, void (*free_func)(void*) = SQLITE_TRANSIENT) {
        result_blob(blob.data(), blob.size(), free_func);
    }

    /**
     * @brief Sets a zero-filled binary BLOB of specified length.
     * @param n Number of zero bytes to allocate.
     */
    inline void result_zeroblob(int n) {
        if (n <= 0) {
            result_blob("", 0);
            return;
        }
        if (n <= 22) {
            uint8_t zero_buf[22] = {0};
            m_result.set_blob(zero_buf, n);
            m_has_error = false;
            return;
        }
        char* buf = static_cast<char*>(sqlite3_malloc64(static_cast<sqlite3_uint64>(n)));
        if (!buf) {
            result_error_nomem();
            return;
        }
        memset(buf, 0, static_cast<size_t>(n));
        m_result.set_blob(buf, n);
        sqlite3_free(buf);
        m_has_error = false;
    }

    /** @brief Explicit zero-copy borrowed text setter. */
    inline void result_borrowed_text(const char* text, int len = -1, uint8_t subtype = SQLITE_SUBTYPE_NONE) noexcept {
        m_result = SqliteValueOwned::from_borrowed_text(text, len, subtype);
        m_has_error = false;
    }

    /** @brief Explicit zero-copy borrowed blob setter. */
    inline void result_borrowed_blob(const void* data, int len, uint8_t subtype = SQLITE_SUBTYPE_NONE) noexcept {
        m_result = SqliteValueOwned::from_borrowed_blob(data, len, subtype);
        m_has_error = false;
    }

    // ========================================================================
    // Result Setters - Values, Pointers & Subtypes (SqliteContext 1:1 Parity)
    // ========================================================================

    /** @brief Sets an owned value by deep-copying. */
    inline void result_value(const SqliteValueOwned& val) {
        m_result = val.clone();
        m_has_error = false;
    }

    /** @brief Sets an owned value by moving. */
    inline void result_value(SqliteValueOwned&& val) noexcept {
        m_result = sqlite_move(val);
        m_has_error = false;
    }

    /** @brief Sets a value from a transient SqliteValueView. */
    inline void result_value(const SqliteValueView& val) {
        m_result = val.to_owned();
        m_has_error = false;
    }

    /**
     * @brief Sets an opaque typed C/C++ pointer as the return result.
     * @tparam T Pointer payload type.
     * @param ptr Raw pointer.
     * @param type_name Optional pointer type name (defaults to SqlitePointerTraits<T>::name()).
     * @param dtor Optional destructor callback (ignored in direct execution).
     */
    template <typename T>
    inline void result_pointer(T* ptr, const char* type_name = nullptr, void (*dtor)(void*) = nullptr) noexcept {
        (void)type_name;
        (void)dtor;
        m_result.set_pointer(ptr);
        m_has_error = false;
    }

    /** @brief Assigns SQLite subtype metadata (JSON, UUID, VECTOR, etc.) to the result. */
    inline void result_subtype(uint8_t sub) noexcept {
        m_result.set_subtype(sub);
    }

    // ========================================================================
    // Error Reporting (SqliteContext 1:1 Parity)
    // ========================================================================

    /**
     * @brief Sets a custom error message on this context (defaulting to SQLITE_ERROR).
     * @param z Null-terminated UTF-8 error string description.
     */
    inline void result_error(const char* z) noexcept {
        result_error(z, SQLITE_ERROR);
    }

    /**
     * @brief Sets a custom error message and explicit SQLite error code.
     * @param msg Error description string.
     * @param err_code SQLite error code (e.g. SQLITE_CONSTRAINT, SQLITE_MISMATCH).
     */
    inline void result_error(const char* msg, int err_code) noexcept {
        m_has_error = true;
        m_error_msg = msg ? msg : "Unknown direct dispatch error";
        m_error_code = err_code;
        m_result.set_null();
    }

    /**
     * @brief Sets a custom error message with byte length (SqliteContext parity).
     * @param z UTF-8 error string description.
     * @param n Length in bytes (ignored if negative).
     */
    inline void result_error_sized(const char* z, int n) noexcept {
        (void)n;
        result_error(z, SQLITE_ERROR);
    }

    /** @brief Reports an out-of-memory error (SQLITE_NOMEM) on this context. */
    inline void result_error_nomem() noexcept {
        result_error("out of memory", SQLITE_NOMEM);
    }

    /** @brief Reports a size overflow error (SQLITE_TOOBIG) on this context. */
    inline void result_error_toobig() noexcept {
        result_error("string or blob too big", SQLITE_TOOBIG);
    }

    /** @brief Sets a specific numeric error code on this context. */
    inline void result_error_code(int errCode) noexcept {
        m_has_error = true;
        m_error_code = errCode;
    }

    // ========================================================================
    // Introspection & Result Extraction
    // ========================================================================

    /** @brief Returns true if an error was reported on this context. */
    inline bool is_error() const noexcept { return m_has_error; }

    /** @brief Returns the error message string, or nullptr if no error occurred. */
    inline const char* error_message() const noexcept { return m_error_msg; }

    /** @brief Returns the SQLite error code (SQLITE_OK if successful). */
    inline int error_code() const noexcept { return m_error_code; }

    /** @brief Returns immutable reference to the stored result value. */
    inline const SqliteValueOwned& result() const noexcept { return m_result; }

    /** @brief Returns mutable reference to the stored result value. */
    inline SqliteValueOwned& result() noexcept { return m_result; }

    /**
     * @brief Moves out the stored result value and resets this context's result to SQLITE_NULL.
     * @return Moved SqliteValueOwned instance.
     */
    inline SqliteValueOwned take_result() noexcept { return m_result.take(); }
};

#endif // DIRECT_DISPATCH_CONTEXT_HPP
