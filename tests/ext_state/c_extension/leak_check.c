#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <dlfcn.h>
#else
#include <windows.h>
#endif

/*
 * Minimal C harness for valgrind leak-checking the C extension.
 */
int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <path-to-extension>\n", argv[0]);
    return 2;
  }
  const char *ext_path = argv[1];

#ifndef _WIN32
  void *pin = dlopen(ext_path, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
  if (!pin) {
    fprintf(stderr, "dlopen(RTLD_NODELETE) failed: %s\n", dlerror());
    return 1;
  }
#else
  HMODULE pin = LoadLibraryExA(ext_path, NULL, 0);
  if (!pin) {
    fprintf(stderr, "LoadLibrary failed\n");
    return 1;
  }
#endif

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

  sqlite3_shutdown();
  return 0;
}
