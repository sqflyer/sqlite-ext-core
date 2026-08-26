#ifndef SQLITE3_EXT_H
#define SQLITE3_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sqlite3.h>

// Core pure C subsystem headers
#include "sqlite3_atomic.h"
#include "sqlite3_time.h"
#include "sqlite3_tiny_lock.h"
#include "sqlite3_rw_lock.h"
#include "sqlite3_mutex_lock.h"
#include "sqlite3_smart_ptr.h"
#include "sqlite3_ext_state.h"

#ifdef __cplusplus
}
#endif

#endif // SQLITE3_EXT_H
