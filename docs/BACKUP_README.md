# Online Backup API (`sqlite3_backup.hpp`)

This header provides a zero-allocation, exception-safe RAII wrapper for SQLite's Online Backup API (`sqlite3_backup_init`, `sqlite3_backup_step`, `sqlite3_backup_finish`).

It guarantees that `sqlite3_backup_finish` is called exactly once when the wrapper goes out of scope, preventing the source database from remaining locked indefinitely if an error or exception occurs mid-backup.

## Core Features
1. **Resource Safety**: Complete RAII lifecycle management.
2. **Page-Level Granularity**: Allows copying the database page-by-page.
3. **Move Semantics**: Move-constructible and move-assignable for clean ownership transfer.

## Usage Guide

### 1. Simple Synchronous Backup
The easiest way to copy a database is to perform the entire backup in a single blocking step.

```cpp
SqliteDatabase dest_db("backup.sqlite");
SqliteDatabase src_db("main.sqlite");

{
    // Initialize the backup from src_db "main" into dest_db "main"
    SqliteBackup backup(dest_db, "main", src_db, "main");
    if (!backup) {
        // Handle initialization failure
        return;
    }

    // Step with -1 to copy all remaining pages synchronously
    backup.step(-1);

    // backup.finish() is automatically called when the scope exits!
}
```

### 2. Incremental Background Backup
For massive databases, locking the database for a complete blocking backup might block other writers. You can use the `step()` method to copy the database in smaller chunks, allowing other transactions to interleave.

```cpp
SqliteBackup backup(dest_db, "main", src_db, "main");
if (backup) {
    int rc;
    do {
        // Copy 5 pages at a time
        rc = backup.step(5);
        
        printf("Progress: %d / %d pages remaining.\n", 
               backup.remaining(), backup.pagecount());
               
        // Optionally sleep or yield to other threads here
        
    } while (rc == SQLITE_OK);

    if (rc == SQLITE_DONE) {
        printf("Backup completed successfully!\n");
    } else {
        printf("Backup failed with code: %d\n", rc);
    }
}
```

### 3. Early Termination
If you need to manually inspect the final result code of the cleanup phase, you can manually call `.finish()`. The destructor will safely no-op.

```cpp
SqliteBackup backup(dest_db, "main", src_db, "main");
backup.step(-1);

int final_rc = backup.finish();
if (final_rc != SQLITE_OK) {
    // Handle error
}
```
