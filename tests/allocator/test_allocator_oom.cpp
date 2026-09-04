#define SQLITE_CORE
#include "../../include/sqlite3_allocator.hpp"
#include "../../include/sqlite3_ext.hpp"
#include "../../include/sqlite3_ext_state.h"
#include "../../include/sqlite3_smart_ptr.h"
#include "../../include/sqlite3_value.hpp"
#include "../../include/sqlite3_value_containers.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// TEST FIXTURES & INSTRUMENTATION
// ============================================================================

struct TrackerStruct {
  int id;
  static int construct_count;
  static int destruct_count;

  TrackerStruct() : id(0) { construct_count++; }
  TrackerStruct(int i) : id(i) { construct_count++; }
  TrackerStruct(const TrackerStruct &other) : id(other.id) {
    construct_count++;
  }
  TrackerStruct(TrackerStruct &&other) noexcept : id(other.id) {
    other.id = -1;
    construct_count++;
  }
  ~TrackerStruct() { destruct_count++; }
};

int TrackerStruct::construct_count = 0;
int TrackerStruct::destruct_count = 0;

static void reset_tracker_counts() {
  TrackerStruct::construct_count = 0;
  TrackerStruct::destruct_count = 0;
}

// ============================================================================
// 1. HARD HEAP LIMIT & ALLOCATION FAILURE REJECTION
// ============================================================================

void test_allocator_hard_heap_limit() {
  printf("1. Testing SqliteAllocator hard heap limit & OOM failure "
         "rejection...\n");
  reset_tracker_counts();
  sqlite3_initialize();

  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. Set a restrictive hard heap limit equal to current usage + 1 byte
  //    Any allocation exceeding 1 byte will be blocked by SQLite's memory
  //    manager.
  sqlite3_hard_heap_limit64(mem_baseline + 1);

  // 2. sqlite_new<T>() should fail, return nullptr, and NOT invoke constructor
  TrackerStruct *obj = sqlite_new<TrackerStruct>(999);
  assert(obj == nullptr);
  assert(TrackerStruct::construct_count == 0);

  // 3. sqlite_new_array<T>() should fail and return nullptr
  TrackerStruct *arr = sqlite_new_array<TrackerStruct>(10);
  assert(arr == nullptr);

  // 4. sqlite_new_zeroed<T>() should fail and return nullptr
  TrackerStruct *zeroed = sqlite_new_zeroed<TrackerStruct>();
  assert(zeroed == nullptr);

  // 5. sqlite_new_array_zeroed<T>() should fail and return nullptr
  TrackerStruct *arr_zeroed = sqlite_new_array_zeroed<TrackerStruct>(10);
  assert(arr_zeroed == nullptr);

  // 6. Raw byte allocations should fail and return nullptr
  void *raw = sqlite_malloc_zeroed(1024);
  assert(raw == nullptr);

  // 7. Reallocations should fail and return nullptr
  void *realloc_ptr = sqlite_realloc_zeroed(nullptr, 0, 1024);
  assert(realloc_ptr == nullptr);

  TrackerStruct *realloc_arr =
      sqlite_reallocate_array<TrackerStruct>(nullptr, 10);
  assert(realloc_arr == nullptr);

  // 8. SqliteAllocator<T> STL interface should return nullptr without throwing
  // std::bad_alloc
  SqliteAllocator<TrackerStruct> alloc;
  TrackerStruct *alloc_buf = alloc.allocate(5);
  assert(alloc_buf == nullptr);

  TrackerStruct *alloc_zeroed_buf = alloc.allocate_zeroed(5);
  assert(alloc_zeroed_buf == nullptr);

  // 9. Remove hard heap limit (set to 0 for unlimited)
  sqlite3_hard_heap_limit64(0);

  // 10. Verify allocations succeed normally after removing the limit
  TrackerStruct *valid_obj = sqlite_new<TrackerStruct>(42);
  assert(valid_obj != nullptr);
  assert(valid_obj->id == 42);
  assert(TrackerStruct::construct_count == 1);
  sqlite_delete(valid_obj);
  assert(TrackerStruct::destruct_count == 1);

  TrackerStruct *valid_arr = alloc.allocate(3);
  assert(valid_arr != nullptr);
  alloc.deallocate(valid_arr, 3);

  // Verify clean memory baseline
  assert(sqlite3_memory_used() == mem_baseline);
  printf("   [PASS] SqliteAllocator hard heap limit rejection verified.\n");
}

// ============================================================================
// 2. INTEGER MULTIPLICATION OVERFLOW SAFETY
// ============================================================================

void test_allocator_overflow_safety() {
  printf("2. Testing size_t multiplication overflow safety...\n");

  size_t overflow_count = (static_cast<size_t>(-1) / sizeof(TrackerStruct)) + 1;

  // 1. sqlite_new_array overflow
  assert(sqlite_new_array<TrackerStruct>(overflow_count) == nullptr);

  // 2. sqlite_new_array_zeroed overflow
  assert(sqlite_new_array_zeroed<TrackerStruct>(overflow_count) == nullptr);

  // 3. sqlite_reallocate_array overflow
  assert(sqlite_reallocate_array<TrackerStruct>(nullptr, overflow_count) ==
         nullptr);

  // 4. sqlite_reallocate_array_zeroed overflow
  assert(sqlite_reallocate_array_zeroed<TrackerStruct>(
             nullptr, 0, overflow_count) == nullptr);

  // 5. SqliteAllocator<T> STL interface overflow
  SqliteAllocator<TrackerStruct> alloc;
  assert(alloc.allocate(overflow_count) == nullptr);
  assert(alloc.allocate_zeroed(overflow_count) == nullptr);

  printf("   [PASS] Multiplication overflow safety verified.\n");
}

// ============================================================================
// 3. SBO CONTAINER RESILIENCE UNDER MEMORY LIMITS
// ============================================================================

void test_container_sbo_resilience_under_oom() {
  printf("3. Testing SBO container resilience under hard memory limits...\n");
  sqlite3_initialize();
  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. Set hard heap limit to baseline + 1 byte
  sqlite3_hard_heap_limit64(mem_baseline + 1);

  // 2. SqliteValueVec<8> inline operations must succeed 100% on the stack with
  // 0 heap allocations
  {
    SqliteValueVec<8> vec;
    assert(!vec.is_heap_allocated());
    assert(vec.capacity() == 8);
    assert(vec.size() == 0);

    for (int i = 0; i < 8; ++i) {
      vec.push_back(SqliteValueOwned(static_cast<sqlite3_int64>(i * 10)));
    }

    assert(vec.size() == 8);
    assert(!vec.is_heap_allocated());
    for (int i = 0; i < 8; ++i) {
      assert(vec[i].as_int64() == i * 10);
    }
  }

  // 3. SqliteValueOwned inline text (<= 21 bytes) and inline blob (<= 22 bytes)
  //    must succeed without heap allocations even when heap limit is reached
  {
    SqliteValueOwned text_sbo =
        SqliteValueOwned::from_text("Short inline text");
    assert(!text_sbo.is_heap_allocated());
    assert(text_sbo.as_text() == "Short inline text");

    const uint8_t raw_bytes[16] = {1, 2,  3,  4,  5,  6,  7,  8,
                                   9, 10, 11, 12, 13, 14, 15, 16};
    SqliteValueOwned blob_sbo = SqliteValueOwned::from_blob(raw_bytes, 16);
    assert(!blob_sbo.is_heap_allocated());
    assert(blob_sbo.as_blob().size() == 16);
  }

  // 4. Remove limit
  sqlite3_hard_heap_limit64(0);
  assert(sqlite3_memory_used() == mem_baseline);
  printf("   [PASS] SBO container resilience under OOM verified.\n");
}

// ============================================================================
// 4. SQLITE STRING BUILDER OOM TRACKING
// ============================================================================

void test_sqlite_string_owned_oom() {
  printf("4. Testing SqliteStringOwned OOM error code tracking...\n");
  sqlite3_initialize();
  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. Constrain heap limit to current usage + 1 byte
  sqlite3_hard_heap_limit64(mem_baseline + 1);

  // 2. SqliteStringOwned attempting dynamic expansion should fail cleanly
  SqliteStringOwned str;
  str.append("This is a long string that will definitely exceed the 1-byte "
             "heap limit constraint",
             85);

  // SQLite string builder tracks NOMEM error code internally
  assert(str.errcode() == SQLITE_NOMEM);

  // 3. Finish on NOMEM string returns nullptr
  char *finished = str.finish();
  assert(finished == nullptr);

  // 4. Remove limit
  sqlite3_hard_heap_limit64(0);

  // 5. Normal string builder usage succeeds after limit removal
  SqliteStringOwned valid_str;
  valid_str.append("Hello World", 11);
  assert(valid_str.errcode() == SQLITE_OK);
  assert(valid_str.length() == 11);
  char *valid_raw = valid_str.finish();
  assert(valid_raw != nullptr);
  assert(strcmp(valid_raw, "Hello World") == 0);
  sqlite3_free(valid_raw);

  assert(sqlite3_memory_used() == mem_baseline);
  printf("   [PASS] SqliteStringOwned OOM tracking verified.\n");
}

// ============================================================================
// 5. SQLITE ALLOCATOR CLASS API UNDER OOM
// ============================================================================

void test_allocator_class_oom_methods() {
  printf("5. Testing SqliteAllocator class methods & rebinds under OOM...\n");
  sqlite3_initialize();
  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. Constrain heap limit
  sqlite3_hard_heap_limit64(mem_baseline + 1);

  SqliteAllocator<TrackerStruct> struct_alloc;
  SqliteAllocator<TrackerStruct>::rebind<double>::other dbl_alloc(struct_alloc);
  SqliteExt::Allocator<int> int_alloc;

  // 2. All allocate() calls across base and rebound types return nullptr
  assert(struct_alloc.allocate(10) == nullptr);
  assert(struct_alloc.allocate_zeroed(10) == nullptr);
  assert(dbl_alloc.allocate(20) == nullptr);
  assert(dbl_alloc.allocate_zeroed(20) == nullptr);
  assert(int_alloc.allocate(50) == nullptr);
  assert(int_alloc.allocate_zeroed(50) == nullptr);

  // 3. Deallocation of nullptr is a safe no-op under OOM
  struct_alloc.deallocate(nullptr, 10);
  dbl_alloc.deallocate(nullptr, 20);
  int_alloc.deallocate(nullptr, 50);

  // 4. Equality operators remain valid
  assert(struct_alloc == struct_alloc);
  assert(!(struct_alloc != struct_alloc));

  // 5. Remove limit and verify recovery
  sqlite3_hard_heap_limit64(0);

  double *dbl_buf = dbl_alloc.allocate(5);
  assert(dbl_buf != nullptr);
  dbl_alloc.deallocate(dbl_buf, 5);

  int *int_buf = int_alloc.allocate_zeroed(10);
  assert(int_buf != nullptr);
  for (int i = 0; i < 10; ++i) {
    assert(int_buf[i] == 0);
  }
  int_alloc.deallocate(int_buf, 10);

  assert(sqlite3_memory_used() == mem_baseline);
  printf("   [PASS] SqliteAllocator class methods under OOM verified.\n");
}

// ============================================================================
// 6. SQLITE_TRY_NEW AND SQLITE_TRY_NEW_ARRAY UNDER OOM
// ============================================================================

void test_try_new_under_oom() {
  printf("6. Testing sqlite_try_new & sqlite_try_new_array with custom "
         "messages under OOM...\n");
  sqlite3_initialize();
  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. Constrain heap limit
  sqlite3_hard_heap_limit64(mem_baseline + 1);

  // 2. sqlite_try_new should return SqliteResult with SQLITE_NOMEM and custom
  // error message
  SqliteResult<TrackerStruct *> res_new = sqlite_try_new<TrackerStruct>(42);
  assert(res_new.is_err());
  assert(res_new.err_code() == SQLITE_NOMEM);
  assert(res_new.err_message() != nullptr);
  assert(strcmp(res_new.err_message(),
                "Memory allocation failed in sqlite_try_new") == 0);
  assert(strcmp(res_new.msg(), "Memory allocation failed in sqlite_try_new") ==
         0);

  // 3. sqlite_try_new_array should return SqliteResult with SQLITE_NOMEM
  SqliteResult<TrackerStruct *> res_arr =
      sqlite_try_new_array<TrackerStruct>(20);
  assert(res_arr.is_err());
  assert(res_arr.err_code() == SQLITE_NOMEM);
  assert(res_arr.err_message() != nullptr);
  assert(strcmp(res_arr.err_message(),
                "Memory allocation failed in sqlite_try_new_array") == 0);

  // 4. sqlite_try_new_zeroed should return SqliteResult with SQLITE_NOMEM
  SqliteResult<TrackerStruct *> res_new_z =
      sqlite_try_new_zeroed<TrackerStruct>();
  assert(res_new_z.is_err());
  assert(res_new_z.err_code() == SQLITE_NOMEM);

  // 5. sqlite_try_new_array_zeroed should return SqliteResult with SQLITE_NOMEM
  SqliteResult<TrackerStruct *> res_arr_z =
      sqlite_try_new_array_zeroed<TrackerStruct>(20);
  assert(res_arr_z.is_err());
  assert(res_arr_z.err_code() == SQLITE_NOMEM);

  // 6. sqlite_try_reallocate_array_zeroed should return SqliteResult with
  // SQLITE_NOMEM
  SqliteResult<TrackerStruct *> res_realloc_arr_z =
      sqlite_try_reallocate_array_zeroed<TrackerStruct>(nullptr, 0, 20);
  assert(res_realloc_arr_z.is_err());
  assert(res_realloc_arr_z.err_code() == SQLITE_NOMEM);

  // 7. Remove limit
  sqlite3_hard_heap_limit64(0);

  // 8. Success paths after removing limit
  SqliteResult<TrackerStruct *> ok_new = sqlite_try_new<TrackerStruct>(99);
  assert(ok_new.is_ok());
  assert(ok_new.unwrap() != nullptr);
  assert(ok_new.unwrap()->id == 99);
  sqlite_delete(ok_new.unwrap());

  SqliteResult<TrackerStruct *> ok_arr = sqlite_try_new_array<TrackerStruct>(5);
  assert(ok_arr.is_ok());
  assert(ok_arr.unwrap() != nullptr);
  sqlite_delete_array(ok_arr.unwrap());

  SqliteResult<TrackerStruct *> ok_new_z =
      sqlite_try_new_zeroed<TrackerStruct>();
  assert(ok_new_z.is_ok());
  assert(ok_new_z.unwrap() != nullptr);
  assert(ok_new_z.unwrap()->id == 0);
  sqlite3_free(ok_new_z.unwrap());

  SqliteResult<TrackerStruct *> ok_arr_z =
      sqlite_try_new_array_zeroed<TrackerStruct>(4);
  assert(ok_arr_z.is_ok());
  assert(ok_arr_z.unwrap() != nullptr);
  for (size_t i = 0; i < 4; ++i)
    assert(ok_arr_z.unwrap()[i].id == 0);
  sqlite_delete_array(ok_arr_z.unwrap());

  assert(sqlite3_memory_used() == mem_baseline);
  printf("   [PASS] sqlite_try_new and sqlite_try_new_array under OOM "
         "verified.\n");
}

// ============================================================================
// 7. SQLITE_VALUE_VEC TRY_ METHODS UNDER OOM
// ============================================================================

void test_sqlite_value_vec_try_methods_oom() {
  printf("7. Testing SqliteValueVec try_ methods under hard heap limit...\n");
  sqlite3_initialize();
  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. Stack SBO (N=4): try_push_back for <= 4 items succeeds with 0 heap
  // allocation
  {
    SqliteValueVec<4> vec;
    for (int i = 0; i < 4; ++i) {
      SqliteStatus stat =
          vec.try_push_back(static_cast<sqlite3_int64>(i * 100));
      assert(stat.is_ok());
    }
    assert(vec.size() == 4);
    assert(!vec.is_heap_allocated());

    // Now set hard heap limit
    sqlite3_hard_heap_limit64(mem_baseline + 1);

    // 5th item requires heap allocation -> try_push_back must fail cleanly with
    // nomem
    SqliteValueOwned val5(500LL);
    SqliteStatus fail_stat = vec.try_push_back(val5);
    assert(fail_stat.is_err());
    assert(fail_stat.err_code() == SQLITE_NOMEM);

    // Size and existing stack items remain uncorrupted (strong guarantee)
    assert(vec.size() == 4);
    assert(!vec.is_heap_allocated());
    for (int i = 0; i < 4; ++i) {
      assert(vec[i].as_int64() == i * 100);
    }

    // try_reserve to 10 must fail
    SqliteStatus res_stat = vec.try_reserve(10);
    assert(res_stat.is_err());
    assert(res_stat.err_code() == SQLITE_NOMEM);

    // try_resize to 10 must fail
    SqliteStatus resize_stat = vec.try_resize(10);
    assert(resize_stat.is_err());
    assert(resize_stat.err_code() == SQLITE_NOMEM);

    // Remove limit
    sqlite3_hard_heap_limit64(0);

    // After removing limit, try_push_back succeeds and moves to heap
    SqliteStatus ok_stat = vec.try_push_back(500LL);
    assert(ok_stat.is_ok());
    assert(vec.size() == 5);
    assert(vec.is_heap_allocated());

    // try_clone succeeds
    auto clone_res = vec.try_clone();
    assert(clone_res.is_ok());
    assert(clone_res.unwrap().size() == 5);
  }

  // 2. Pure heap vector (N=0)
  {
    SqliteValueVec<0> heap_vec;
    sqlite3_hard_heap_limit64(mem_baseline + 1);

    SqliteStatus fail_push = heap_vec.try_push_back(42LL);
    assert(fail_push.is_err());
    assert(fail_push.err_code() == SQLITE_NOMEM);
    assert(heap_vec.size() == 0);

    sqlite3_hard_heap_limit64(0);

    SqliteStatus ok_push = heap_vec.try_push_back(42LL);
    assert(ok_push.is_ok());
    assert(heap_vec.size() == 1);
    assert(heap_vec[0].as_int64() == 42);

    auto clone_res = heap_vec.try_clone();
    assert(clone_res.is_ok());
    assert(clone_res.unwrap().size() == 1);
  }

  assert(sqlite3_memory_used() == mem_baseline);
  printf("   [PASS] SqliteValueVec try_ methods under OOM verified.\n");
}

// ============================================================================
// 8. SQLITE_VALUE_OWNED TRY_ FACTORIES & METHODS UNDER OOM
// ============================================================================

void test_sqlite_value_owned_try_methods_oom() {
  printf("8. Testing SqliteValueOwned try_ factory methods & mutations under "
         "OOM...\n");
  sqlite3_initialize();
  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. SBO text (<= 21 chars) and SBO blob (<= 22 bytes) succeed even under
  // hard heap limit
  sqlite3_hard_heap_limit64(mem_baseline + 1);

  auto sbo_text = SqliteValueOwned::try_from_text("Short inline str");
  assert(sbo_text.is_ok());
  assert(!sbo_text.unwrap().is_heap_allocated());
  assert(sbo_text.unwrap().as_text() == "Short inline str");

  uint8_t raw16[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  auto sbo_uuid =
      SqliteValueOwned::try_from_blob(raw16, 16, SQLITE_SUBTYPE_UUID);
  assert(sbo_uuid.is_ok());
  assert(!sbo_uuid.unwrap().is_heap_allocated());
  assert(sbo_uuid.unwrap().is_uuid());

  // 2. Large text (> 21 bytes) and large blob (> 22 bytes) fail cleanly with
  // nomem
  const char *long_text =
      "This text is well beyond twenty-one bytes in length!";
  auto fail_text = SqliteValueOwned::try_from_text(long_text);
  assert(fail_text.is_err());
  assert(fail_text.err_code() == SQLITE_NOMEM);

  uint8_t long_blob[32] = {0};
  auto fail_blob = SqliteValueOwned::try_from_blob(long_blob, 32);
  assert(fail_blob.is_err());
  assert(fail_blob.err_code() == SQLITE_NOMEM);

  auto fail_json = SqliteValueOwned::try_from_json(
      "{\"very_long_json_object_key\": 123456789}");
  assert(fail_json.is_err());
  assert(fail_json.err_code() == SQLITE_NOMEM);

  auto fail_vec = SqliteValueOwned::try_from_vector(long_blob, 32);
  assert(fail_vec.is_err());
  assert(fail_vec.err_code() == SQLITE_NOMEM);

  // 3. Mutation try_set_text & try_set_blob fail on heap spill without
  // corrupting existing value
  SqliteValueOwned val(12345LL);
  SqliteStatus mut_fail = val.try_set_text(long_text);
  assert(mut_fail.is_err());
  assert(mut_fail.err_code() == SQLITE_NOMEM);
  assert(val.as_int64() == 12345); // Unchanged

  // 4. SqliteBlobOwned try_create under limit
  auto blob_fail = SqliteBlobOwned::try_create(long_blob, 32);
  assert(blob_fail.is_err());
  assert(blob_fail.err_code() == SQLITE_NOMEM);

  // 5. Remove limit and verify success paths
  sqlite3_hard_heap_limit64(0);

  {
    auto ok_text = SqliteValueOwned::try_from_text(long_text);
    assert(ok_text.is_ok());
    assert(ok_text.unwrap().is_heap_allocated());
    assert(ok_text.unwrap().as_text() == long_text);

    auto ok_clone = ok_text.unwrap().try_clone();
    assert(ok_clone.is_ok());
    assert(ok_clone.unwrap().as_text() == long_text);

    auto ok_blob_owned = SqliteBlobOwned::try_create(long_blob, 32);
    assert(ok_blob_owned.is_ok());
    assert(ok_blob_owned.unwrap().size() == 32);
  }

  assert(sqlite3_memory_used() == mem_baseline);
  printf("   [PASS] SqliteValueOwned try_ factories and mutations under OOM "
         "verified.\n");
}

// ============================================================================
// 9. SQLITE_BUFFER & SQLITE_STRING TRY_ METHODS UNDER OOM
// ============================================================================

void test_sqlite_buffer_and_string_try_methods_oom() {
  printf(
      "9. Testing SqliteBuffer and SqliteString try_ methods under OOM...\n");
  sqlite3_initialize();
  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. Constrain limit
  sqlite3_hard_heap_limit64(mem_baseline + 1);

  // 2. SqliteBuffer try_reserve and try_append fail with nomem
  {
    SqliteBuffer buf;
    SqliteStatus stat_res = buf.try_reserve(128);
    assert(stat_res.is_err());
    assert(stat_res.err_code() == SQLITE_NOMEM);

    SqliteStatus stat_app = buf.try_append("Hello World", 11);
    assert(stat_app.is_err());
    assert(stat_app.err_code() == SQLITE_NOMEM);

    auto uninit_res = buf.try_append_uninitialized(64);
    assert(uninit_res.is_err());
    assert(uninit_res.err_code() == SQLITE_NOMEM);

    // 3. SqliteString try_create fails
    auto str_res = SqliteString::try_create("This is a test string");
    assert(str_res.is_err());
    assert(str_res.err_code() == SQLITE_NOMEM);
  }

  // 4. Remove limit and verify success
  sqlite3_hard_heap_limit64(0);

  {
    SqliteBuffer buf;
    assert(buf.try_append("Hello World", 11).is_ok());
    assert(buf.bytes() == 11);

    auto ok_str = SqliteString::try_create("Hello String");
    assert(ok_str.is_ok());
    assert(ok_str.unwrap() == "Hello String");
  }

  assert(sqlite3_memory_used() == mem_baseline);
  printf("   [PASS] SqliteBuffer and SqliteString try_ methods under OOM "
         "verified.\n");
}

// ============================================================================
// 10. SMART POINTER TRY_ FACTORIES & C MACRO OOM CLEANUP
// ============================================================================

struct DummyResource {
  int val;
  static int destroyed_count;
  DummyResource(int v = 0) : val(v) {}
  ~DummyResource() { destroyed_count++; }
};
int DummyResource::destroyed_count = 0;

static void free_dummy_resource(DummyResource *p) {
  if (p) {
    p->~DummyResource();
    sqlite3_free(p);
  }
}

// Generate C smart pointer for DummyResource
SQLITE_SHARED_PTR_DEFINE(DummyRes, DummyResource, free_dummy_resource)

void test_smart_ptr_try_methods_and_c_leak_fix_oom() {
  printf("10. Testing smart pointers try_ factories & C macro leak fix under "
         "OOM...\n");
  sqlite3_initialize();
  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. Test C++ sqlite_try_make_shared and sqlite_try_make_unique under limit
  sqlite3_hard_heap_limit64(mem_baseline + 1);

  auto sp_res = sqlite_try_make_shared<DummyResource>(100);
  assert(sp_res.is_err());
  assert(sp_res.err_code() == SQLITE_NOMEM);

  auto up_res = sqlite_try_make_unique<DummyResource>(200);
  assert(up_res.is_err());
  assert(up_res.err_code() == SQLITE_NOMEM);

  // 2. Test C macro DummyRes_make_shared leak fix when control block allocation
  // fails
  DummyResource::destroyed_count = 0;
  DummyResource *raw_obj =
      (DummyResource *)sqlite3_malloc64(sizeof(DummyResource));
  // When heap limit is active, allocating raw_obj failed
  assert(raw_obj == nullptr);

  // Temporarily allow raw_obj allocation, then restrict before make_shared
  sqlite3_hard_heap_limit64(0);
  raw_obj = (DummyResource *)sqlite3_malloc64(sizeof(DummyResource));
  assert(raw_obj != nullptr);
  new (static_cast<void *>(raw_obj), sqlite_new_tag()) DummyResource(777);

  // Now set limit so ControlBlock allocation inside DummyRes_make_shared fails
  sqlite3_hard_heap_limit64(sqlite3_memory_used() + 1);

  DummyRes_SharedPtr sp = DummyRes_make_shared(raw_obj);
  assert(sp.cb == nullptr);
  // Destructor MUST have been invoked on raw_obj to prevent leak!
  assert(DummyResource::destroyed_count == 1);

  // Remove limit
  sqlite3_hard_heap_limit64(0);

  // Verify successful creation & exercise C smart ptr macros
  {
    auto ok_sp = sqlite_try_make_shared<DummyResource>(888);
    assert(ok_sp.is_ok());
    assert(ok_sp.unwrap()->val == 888);

    auto ok_up = sqlite_try_make_unique<DummyResource>(999);
    assert(ok_up.is_ok());
    assert(ok_up.unwrap()->val == 999);

    // Exercise full C macro lifecycle
    DummyResource *c_res =
        (DummyResource *)sqlite3_malloc64(sizeof(DummyResource));
    new (static_cast<void *>(c_res), sqlite_new_tag()) DummyResource(111);
    DummyRes_SharedPtr c_sp = DummyRes_make_shared(c_res);
    assert(DummyRes_get(c_sp) != nullptr);
    assert(DummyRes_get(c_sp)->val == 111);

    DummyRes_SharedPtr c_sp2 = DummyRes_clone(c_sp);
    assert(DummyRes_get(c_sp2)->val == 111);

    DummyRes_SharedPtr c_sp3 = DummyRes_move(&c_sp2);
    assert(c_sp2.cb == nullptr);

    DummyRes_SharedPtr c_sp4;
    c_sp4.cb = nullptr;
    DummyRes_assign_move(&c_sp4, &c_sp3);

    DummyRes_WeakPtr c_wp = DummyRes_weak_create(c_sp);
    assert(!DummyRes_weak_expired(c_wp));
    DummyRes_WeakPtr c_wp2 = DummyRes_weak_clone(c_wp);
    DummyRes_WeakPtr c_wp3 = DummyRes_weak_move(&c_wp2);
    assert(c_wp2.cb == nullptr);

    DummyRes_SharedPtr c_locked = DummyRes_weak_lock(c_wp3);
    assert(DummyRes_get(c_locked) != nullptr);

    DummyRes_reset(&c_locked);
    DummyRes_reset(&c_sp4);
    DummyRes_reset(&c_sp);
    assert(DummyRes_weak_expired(c_wp));

    DummyRes_weak_reset(&c_wp);
    DummyRes_weak_reset(&c_wp3);
  }

  assert(sqlite3_memory_used() == mem_baseline);
  printf("   [PASS] Smart pointers try_ factories and C leak fix under OOM "
         "verified.\n");
}

// ============================================================================
// 11. SQLITE_EXT_STATE TRY_ METHODS UNDER OOM
// ============================================================================

struct StatePayload {
  int counter;
};

void test_ext_state_try_methods_oom() {
  printf("11. Testing SqliteExtState try_ methods under OOM...\n");
  sqlite3_initialize();
  sqlite3 *db = nullptr;
  int rc = sqlite3_open(":memory:", &db);
  assert(rc == SQLITE_OK && db != nullptr);

  sqlite3_int64 mem_baseline = sqlite3_memory_used();

  // 1. Set limit
  sqlite3_hard_heap_limit64(mem_baseline + 1);

  auto res = SqliteExtState<StatePayload>::try_get_or_create(db);
  assert(res.is_err());
  assert(res.err_code() == SQLITE_NOMEM);

  auto init_res = SqliteExtState<StatePayload>::try_init(db);
  assert(init_res.is_err());
  assert(init_res.err_code() == SQLITE_NOMEM);

  // 2. Remove limit and verify success
  sqlite3_hard_heap_limit64(0);

  auto ok_res = SqliteExtState<StatePayload>::try_get_or_create(db);
  assert(ok_res.is_ok());
  assert(ok_res.unwrap() != nullptr);
  ok_res.unwrap()->counter = 42;

  sqlite3_close(db);
  printf("   [PASS] SqliteExtState try_ methods under OOM verified.\n");
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

int main() {
  printf("=================================================================\n");
  printf("Running SqliteAllocator Memory Limits & OOM Safety Test Suite\n");
  printf("=================================================================\n");

  test_allocator_hard_heap_limit();
  test_allocator_overflow_safety();
  test_container_sbo_resilience_under_oom();
  test_sqlite_string_owned_oom();
  test_allocator_class_oom_methods();
  test_try_new_under_oom();
  test_sqlite_value_vec_try_methods_oom();
  test_sqlite_value_owned_try_methods_oom();
  test_sqlite_buffer_and_string_try_methods_oom();
  test_smart_ptr_try_methods_and_c_leak_fix_oom();
  test_ext_state_try_methods_oom();

  printf("\nAll 11 Memory Limit & OOM Safety Suites Passed Successfully "
         "(100%%)!\n");
  return 0;
}
