#ifndef SQLITE3_SMART_PTR_HPP
#define SQLITE3_SMART_PTR_HPP

#include "sqlite3_allocator.hpp"
#include "sqlite3_atomic.h"
#include "sqlite3ext.h"

/**
 * @brief Default deleter for SqliteSmartPtrs.
 * Calls the pseudo-destructor and then sqlite3_free.
 */
template <typename T> inline void sqlite_default_deleter(T *ptr) {
  sqlite_delete(ptr);
}

/**
 * @brief Control Block for Shared and Weak pointers.
 */
template <typename T> struct SqlitePtrControlBlock {
  T *ptr;
  int strong_count;
  int weak_count;
  void (*deleter)(T *);
};

template <typename T> class SqliteWeakPtr;

/**
 * @brief Zero-dependency C++ shared pointer backed by sqlite3_malloc and
 * sqlite3_mutex.
 *
 * Manages shared ownership of an object dynamically allocated via
 * sqlite3_malloc. Multiple SqliteSharedPtr objects may own the same object. The
 * object is destroyed and its memory deallocated when the last remaining
 * SqliteSharedPtr owning it is destroyed or reset.
 */
template <typename T> class SqliteSharedPtr {
  friend class SqliteWeakPtr<T>;

private:
  using ControlBlock = SqlitePtrControlBlock<T>;
  ControlBlock *m_cb;

  explicit SqliteSharedPtr(ControlBlock *cb) : m_cb(cb) {}

  /** @brief Decrements the strong reference count, destroying the object if it
   * reaches 0. */
  void release() {
    if (m_cb) {
      if (sqlite_atomic_decrement_32(&m_cb->strong_count) == 0) {
        if (m_cb->ptr && m_cb->deleter) {
          m_cb->deleter(m_cb->ptr);
        }
        m_cb->ptr = nullptr;
        SqliteWeakPtr<T> wp;
        wp.m_cb = m_cb;
        wp.release();
      }
      m_cb = nullptr;
    }
  }

  /** @brief Increments the strong reference count. */
  void retain() {
    if (m_cb) {
      sqlite_atomic_increment_32(&m_cb->strong_count);
    }
  }

public:
  /** @brief Constructs an empty shared pointer. */
  SqliteSharedPtr() : m_cb(nullptr) {}

  /** @brief Constructs a shared pointer that owns the given pointer using the
   * default deleter. */
  explicit SqliteSharedPtr(T *ptr)
      : SqliteSharedPtr(ptr, sqlite_default_deleter<T>) {}

  /** @brief Constructs a shared pointer that owns the given pointer using a
   * custom deleter. */
  SqliteSharedPtr(T *ptr, void (*deleter)(T *)) : m_cb(nullptr) {
    if (!ptr)
      return;
    m_cb = (ControlBlock *)sqlite3_malloc64(sizeof(ControlBlock));
    if (!m_cb) {
      if (deleter)
        deleter(ptr);
      return;
    }
    m_cb->ptr = ptr;
    m_cb->strong_count = 1;
    m_cb->weak_count =
        1; // The strong references collectively hold 1 weak reference
    m_cb->deleter = deleter;
  }

  /** @brief Copy constructs a shared pointer, incrementing the reference count.
   */
  SqliteSharedPtr(const SqliteSharedPtr &other) : m_cb(other.m_cb) { retain(); }

  /** @brief Move constructs a shared pointer, taking ownership of the reference
   * count. */
  SqliteSharedPtr(SqliteSharedPtr &&other) noexcept : m_cb(other.m_cb) {
    other.m_cb = nullptr;
  }

  /** @brief Destroys the shared pointer, decrementing the reference count. */
  ~SqliteSharedPtr() { release(); }

  /** @brief Copy assigns a shared pointer, incrementing the new reference count
   * and decrementing the old. */
  SqliteSharedPtr &operator=(const SqliteSharedPtr &other) {
    if (this != &other) {
      release();
      m_cb = other.m_cb;
      retain();
    }
    return *this;
  }

  /** @brief Move assigns a shared pointer, transferring ownership of the
   * reference count. */
  SqliteSharedPtr &operator=(SqliteSharedPtr &&other) noexcept {
    if (this != &other) {
      release();
      m_cb = other.m_cb;
      other.m_cb = nullptr;
    }
    return *this;
  }

  /** @brief Returns a pointer to the managed object. */
  T *get() const { return m_cb ? m_cb->ptr : nullptr; }

  /** @brief Dereferences the managed object. Behavior is undefined if get() ==
   * nullptr. */
  T &operator*() const { return *get(); }

  /** @brief Dereferences the managed object. Behavior is undefined if get() ==
   * nullptr. */
  T *operator->() const { return get(); }

  /** @brief Checks if the shared pointer currently owns an object. */
  explicit operator bool() const { return get() != nullptr; }

  /** @brief Resets the shared pointer, releasing its ownership. */
  void reset() { release(); }

  /** @brief Replaces the managed object, releasing the old one. */
  void reset(T *ptr) {
    release();
    *this = SqliteSharedPtr(ptr);
  }

  /** @brief Returns the current number of strong references to the managed
   * object. */
  int use_count() const { return m_cb ? m_cb->strong_count : 0; }
};

/**
 * @brief Zero-dependency C++ weak pointer.
 *
 * A non-owning observer to an object managed by a SqliteSharedPtr.
 * Used to break reference cycles and check object lifetime safely.
 */
template <typename T> class SqliteWeakPtr {
  friend class SqliteSharedPtr<T>;

private:
  using ControlBlock = SqlitePtrControlBlock<T>;
  ControlBlock *m_cb;

  /** @brief Decrements the weak reference count, destroying the control block
   * if total references reach 0. */
  void release() {
    if (m_cb) {
      if (sqlite_atomic_decrement_32(&m_cb->weak_count) == 0) {
        sqlite3_free(m_cb);
      }
      m_cb = nullptr;
    }
  }

  /** @brief Increments the weak reference count. */
  void retain() {
    if (m_cb) {
      sqlite_atomic_increment_32(&m_cb->weak_count);
    }
  }

public:
  /** @brief Constructs an empty weak pointer. */
  SqliteWeakPtr() : m_cb(nullptr) {}

  /** @brief Constructs a weak pointer from a strong shared pointer,
   * incrementing the weak count. */
  SqliteWeakPtr(const SqliteSharedPtr<T> &other) : m_cb(other.m_cb) {
    retain();
  }

  /** @brief Copy constructs a weak pointer, incrementing the weak count. */
  SqliteWeakPtr(const SqliteWeakPtr &other) : m_cb(other.m_cb) { retain(); }

  /** @brief Move constructs a weak pointer, transferring the weak count. */
  SqliteWeakPtr(SqliteWeakPtr &&other) noexcept : m_cb(other.m_cb) {
    other.m_cb = nullptr;
  }

  /** @brief Destroys the weak pointer, decrementing the weak count. */
  ~SqliteWeakPtr() { release(); }

  /** @brief Assigns a weak pointer from a strong shared pointer, decrementing
   * the old weak count. */
  SqliteWeakPtr &operator=(const SqliteSharedPtr<T> &other) {
    release();
    m_cb = other.m_cb;
    retain();
    return *this;
  }

  /** @brief Copy assigns a weak pointer, decrementing the old weak count and
   * incrementing the new. */
  SqliteWeakPtr &operator=(const SqliteWeakPtr &other) {
    if (this != &other) {
      release();
      m_cb = other.m_cb;
      retain();
    }
    return *this;
  }

  /** @brief Move assigns a weak pointer, transferring the weak count. */
  SqliteWeakPtr &operator=(SqliteWeakPtr &&other) noexcept {
    if (this != &other) {
      release();
      m_cb = other.m_cb;
      other.m_cb = nullptr;
    }
    return *this;
  }

  /** @brief Releases the weak reference. */
  void reset() { release(); }

  /** @brief Checks if the managed object has already been deleted. */
  bool expired() const {
    if (!m_cb)
      return true;
    return m_cb->strong_count == 0;
  }

  /** @brief Upgrades to a strong SqliteSharedPtr if the object is still alive.
   * Returns empty if expired. */
  SqliteSharedPtr<T> lock() const {
    if (!m_cb)
      return SqliteSharedPtr<T>();

    int current = m_cb->strong_count;
    while (current > 0) {
      if (sqlite_atomic_cas_weak_32(&m_cb->strong_count, &current,
                                    current + 1)) {
        return SqliteSharedPtr<T>(m_cb);
      }
    }

    return SqliteSharedPtr<T>();
  }
};

/**
 * @brief Zero-dependency C++ unique pointer with zero overhead.
 *
 * Exclusively owns a dynamically allocated object and guarantees it is deleted
 * upon going out of scope. Has exactly zero memory overhead compared to a raw
 * pointer.
 */
template <typename T> class SqliteUniquePtr {
private:
  T *m_ptr;
  void (*m_deleter)(T *);

public:
  /** @brief Constructs an empty unique pointer. */
  SqliteUniquePtr() : m_ptr(nullptr), m_deleter(nullptr) {}

  /** @brief Constructs a unique pointer taking ownership of ptr. */
  explicit SqliteUniquePtr(T *ptr)
      : m_ptr(ptr), m_deleter(sqlite_default_deleter<T>) {}

  /** @brief Constructs a unique pointer taking ownership of ptr with a custom
   * deleter. */
  SqliteUniquePtr(T *ptr, void (*deleter)(T *))
      : m_ptr(ptr), m_deleter(deleter) {}

  // Disable copy
  SqliteUniquePtr(const SqliteUniquePtr &) = delete;
  SqliteUniquePtr &operator=(const SqliteUniquePtr &) = delete;

  // Enable move
  /** @brief Move constructs a unique pointer, transferring ownership. */
  SqliteUniquePtr(SqliteUniquePtr &&other) noexcept
      : m_ptr(other.m_ptr), m_deleter(other.m_deleter) {
    other.m_ptr = nullptr;
  }

  /** @brief Move assigns a unique pointer, destroying the currently held object
   * and transferring ownership. */
  SqliteUniquePtr &operator=(SqliteUniquePtr &&other) noexcept {
    if (this != &other) {
      reset();
      m_ptr = other.m_ptr;
      m_deleter = other.m_deleter;
      other.m_ptr = nullptr;
    }
    return *this;
  }

  /** @brief Destroys the unique pointer and its managed object. */
  ~SqliteUniquePtr() { reset(); }

  /** @brief Replaces the managed object, destroying the previous one. */
  void reset(T *ptr = nullptr,
             void (*deleter)(T *) = sqlite_default_deleter<T>) {
    if (m_ptr && m_deleter) {
      m_deleter(m_ptr);
    }
    m_ptr = ptr;
    m_deleter = deleter;
  }

  /** @brief Releases ownership of the managed object without destroying it. */
  T *release() {
    T *temp = m_ptr;
    m_ptr = nullptr;
    return temp;
  }

  /** @brief Returns a pointer to the managed object. */
  T *get() const { return m_ptr; }

  /** @brief Dereferences the managed object. Behavior is undefined if get() ==
   * nullptr. */
  T &operator*() const { return *m_ptr; }

  /** @brief Dereferences the managed object. Behavior is undefined if get() ==
   * nullptr. */
  T *operator->() const { return m_ptr; }

  /** @brief Checks if the unique pointer currently owns an object. */
  explicit operator bool() const { return m_ptr != nullptr; }
};

/**
 * @brief Constructs an object of type `T` and wraps it in a
 * `SqliteSharedPtr<T>`.
 */
template <typename T, typename... Args>
inline SqliteSharedPtr<T> sqlite_make_shared(Args &&...args) {
  T *ptr = sqlite_new<T>(sqlite_forward<Args>(args)...);
  if (!ptr)
    return SqliteSharedPtr<T>();
  return SqliteSharedPtr<T>(ptr);
}

/**
 * @brief Constructs an object of type `T` and wraps it in a
 * `SqliteUniquePtr<T>`.
 */
template <typename T, typename... Args>
inline SqliteUniquePtr<T> sqlite_make_unique(Args &&...args) {
  T *ptr = sqlite_new<T>(sqlite_forward<Args>(args)...);
  return SqliteUniquePtr<T>(ptr);
}

/**
 * @brief Attempts to construct an object of type `T` wrapped in
 * `SqliteSharedPtr<T>`, returning SqliteResult.
 */
template <typename T, typename... Args>
inline SqliteResult<SqliteSharedPtr<T>> sqlite_try_make_shared(Args &&...args) {
  auto res = sqlite_try_new<T>(sqlite_forward<Args>(args)...);
  if (res.is_err()) {
    return SqliteResult<SqliteSharedPtr<T>>::err(res.err_code(),
                                                 res.err_message());
  }
  SqliteSharedPtr<T> sp(res.value());
  if (!sp.get()) {
    return SqliteResult<SqliteSharedPtr<T>>::nomem(
        "Control block allocation failed in sqlite_try_make_shared");
  }
  return SqliteResult<SqliteSharedPtr<T>>::ok(sqlite_move(sp));
}

/**
 * @brief Attempts to construct an object of type `T` wrapped in
 * `SqliteUniquePtr<T>`, returning SqliteResult.
 */
template <typename T, typename... Args>
inline SqliteResult<SqliteUniquePtr<T>> sqlite_try_make_unique(Args &&...args) {
  auto res = sqlite_try_new<T>(sqlite_forward<Args>(args)...);
  if (res.is_err()) {
    return SqliteResult<SqliteUniquePtr<T>>::err(res.err_code(),
                                                 res.err_message());
  }
  return SqliteResult<SqliteUniquePtr<T>>::ok(SqliteUniquePtr<T>(res.value()));
}

#endif // SQLITE3_SMART_PTR_HPP
