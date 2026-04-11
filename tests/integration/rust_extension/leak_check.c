#include <dlfcn.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Minimal C harness for valgrind leak-checking the Rust extension.
 *
 * Takes the extension path as argv[1] so the Makefile can point it at the
 * freshly-built .so without worrying about cwd. After closing the
 * connection we also call sqlite3_shutdown() so SQLite releases its
 * global caches (page cache, mutex subsystem, pcache1) — without it,
 * valgrind reports those as "definitely lost" even though they are
 * intentional on-exit retention inside libsqlite3.
 *
 * We also dlopen the extension ourselves with RTLD_NODELETE before
 * handing it to sqlite3_load_extension. This pins the .so in memory so
 * that when sqlite3_close later dlcloses it as part of its own teardown,
 * the mapping (and crucially the DWARF debug info) survives until the
 * process exits — which is what lets valgrind actually symbolize any
 * leaks that originated inside our extension instead of showing `???`.
 */
int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <path-to-extension.so>\n", argv[0]);
    return 2;
  }
  const char *ext_path = argv[1];

  void *pin = dlopen(ext_path, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
  if (!pin) {
    fprintf(stderr, "dlopen(RTLD_NODELETE) failed: %s\n", dlerror());
    return 1;
  }

  sqlite3 *db;
  int rc = sqlite3_open(":memory:", &db);
  if (rc) {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    return 1;
  }

  sqlite3_enable_load_extension(db, 1);
  char *errmsg = NULL;
  rc = sqlite3_load_extension(db, ext_path, 0, &errmsg);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "Extension load failed: %s\n", errmsg);
    sqlite3_free(errmsg);
    sqlite3_close(db);
    return 1;
  }

  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "SELECT test_counter();", -1, &stmt, NULL);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  sqlite3_prepare_v2(db, "SELECT test_counter();", -1, &stmt, NULL);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  sqlite3_close(db);

  /*
   * sqlite3_shutdown() is what lets valgrind see a clean heap on exit.
   *
   * Closing the connection is NOT enough: libsqlite3 intentionally retains
   * several pieces of process-wide state as a performance optimization so
   * that the *next* sqlite3_open() in the same process doesn't have to pay
   * the allocation cost again. Specifically it holds onto:
   *
   *   - the pcache1 page-cache allocator pool
   *   - the mutex subsystem's static mutex table
   *   - a handful of global malloc bookkeeping structures
   *
   * These are not memory leaks in any practical sense — libsqlite3 knows
   * exactly where they live and would reuse them on the next open — but
   * from valgrind's perspective they are "definitely lost": at process
   * exit, the root pointers that libsqlite3 tracks internally go away
   * when the .bss segment is torn down, so valgrind sees ~62 KB of
   * allocations with no live references and flags them as leaks.
   *
   * sqlite3_shutdown() is the documented way to tell libsqlite3 "I am
   * done with SQLite entirely, release every byte you are holding". It
   * walks those internal pools and frees them. After that, the only
   * heap state left for valgrind to classify is the allocations that
   * actually originated in *our* code — which is what we want to check.
   *
   * Safety contract: sqlite3_shutdown() must only be called when there
   * are no open connections and no threads currently inside the SQLite
   * library. We close the single connection we opened on the line above,
   * and this harness is single-threaded, so the contract is satisfied.
   * Calling it with a live connection or concurrent SQLite activity is
   * undefined behavior.
   *
   * This call should NEVER appear in application code. It only makes
   * sense in a leak-check harness like this one that needs a clean heap
   * at process exit for instrumentation purposes.
   */
  sqlite3_shutdown();
  return 0;
}
