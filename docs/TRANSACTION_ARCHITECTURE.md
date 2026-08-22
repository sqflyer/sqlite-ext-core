# Transaction Architecture

The C++ Transaction ecosystem in this repository is built around strict **RAII (Resource Acquisition Is Initialization)** principles to ensure that database states remain consistent, even in the face of C++ exceptions or early function returns.

## Core Design Principles

1. **Zero-Overhead Abstraction**: The `SqliteTransaction` and `SqliteSavepoint` classes do not perform any heap allocations. They hold a single `sqlite3*` pointer (8 bytes) and an `m_active` boolean (1 byte, padded to 8). At `-O2`, the compiler entirely optimizes them out.
2. **Exception Safety**: If a C++ function returns early or throws an exception, the destructors automatically issue a `ROLLBACK` command, preventing the database from remaining locked in an open transaction indefinitely.
3. **Explicit Commitment**: Both classes default to rolling back. You must explicitly call `.commit()` or `.release()` to permanently save changes.

## `SqliteTransaction`

Wraps standard `BEGIN`, `COMMIT`, and `ROLLBACK` operations.

### Transaction Behaviors
The constructor accepts a `SqliteTransactionBehavior` enum that dictates how the transaction is initiated:
- **`DEFERRED` (Default)**: `BEGIN DEFERRED;` - The transaction starts as a read transaction and only upgrades to a write lock when a write actually occurs. Ideal for high concurrency.
- **`IMMEDIATE`**: `BEGIN IMMEDIATE;` - Instantly acquires a `RESERVED` lock. No other connections can write, but they can still read.
- **`EXCLUSIVE`**: `BEGIN EXCLUSIVE;` - Instantly acquires an `EXCLUSIVE` lock. No other connections can read or write.

## `SqliteSavepoint`

Wraps `SAVEPOINT`, `RELEASE`, and `ROLLBACK TO` operations to allow for **nested transactions**.

In SQLite, transactions cannot be natively nested. Instead, you use Savepoints. Our `SqliteSavepoint` wrapper allows developers to create deeply nested hierarchical transactions.
- **Safe Identifier Escaping**: The constructor takes a string name for the savepoint and safely escapes it using `sqlite3_mprintf` with the `%w` specifier to prevent SQL injection or parsing errors.
- **Destructor Safety**: If the guard goes out of scope without `.release()` being called, it issues a `ROLLBACK TO "name"` command to undo all changes within its specific scope, leaving outer transactions entirely untouched.
