# Backup Architecture

The SQLite Online Backup API (`sqlite3_backup_*`) provides a robust mechanism to copy the contents of one database to another, even while the source database is actively being used by other connections.

## Architectural Justification

While backing up a database seems trivial, the raw C API lifecycle is notoriously dangerous if mismanaged:

1. **`sqlite3_backup_init`**: Opens a read-transaction on the source database.
2. **`sqlite3_backup_step`**: Copies pages.
3. **`sqlite3_backup_finish`**: Closes the read-transaction and frees resources.

If a developer calls `init` but forgets to call `finish` (perhaps due to an early `return` when `step` fails, or an unhandled C++ exception), the source database will be left with an active read-transaction. In SQLite's journaling modes (like WAL), long-running read-transactions prevent the WAL from being checkpointed, eventually causing the WAL file to grow infinitely until the disk is full.

## Zero-Cost RAII Design

`SqliteBackup` completely eliminates this risk by binding the SQLite C API lifecycle directly to C++ scope rules.

1. **No Memory Allocations**: It holds exactly one 8-byte pointer (`sqlite3_backup*`). No dynamic memory is used.
2. **Exception Safety**: The destructor guarantees that `sqlite3_backup_finish` is called exactly once.
3. **Move Semantics**: The copy constructors are explicitly deleted to prevent double-free bugs (`SQLITE_MISUSE`). The backup state can be safely passed across scopes using `std::move`.
4. **Manual Override**: Developers can manually call `.finish()` to extract the final SQLite error code. The destructor safely detects that the pointer is `nullptr` and no-ops.
