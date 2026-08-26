#include <stdio.h>
#include <assert.h>
#include "sqlite3_thread.h"

typedef struct {
    sqlite3_thread_mutex_t mutex;
    sqlite3_cond_t cond;
    int ready;
    int counter;
} sync_context_t;

static void* worker_signal(void* arg) {
    sync_context_t* ctx = (sync_context_t*)arg;
    sqlite3_time_sleep_ms(25);

    sqlite3_thread_mutex_lock(&ctx->mutex);
    ctx->ready = 1;
    ctx->counter = 777;
    sqlite3_cond_signal(&ctx->cond);
    sqlite3_thread_mutex_unlock(&ctx->mutex);
    return NULL;
}

static void* worker_broadcast(void* arg) {
    sync_context_t* ctx = (sync_context_t*)arg;

    sqlite3_thread_mutex_lock(&ctx->mutex);
    while (!ctx->ready) {
        sqlite3_cond_wait(&ctx->cond, &ctx->mutex);
    }
    ctx->counter += 1;
    sqlite3_thread_mutex_unlock(&ctx->mutex);
    return NULL;
}

void test_cond_signal(void) {
    printf("1. Testing Pure C condition variable wait & signal...\n");
    sync_context_t ctx;
    sqlite3_thread_mutex_init(&ctx.mutex);
    sqlite3_cond_init(&ctx.cond);
    ctx.ready = 0;
    ctx.counter = 0;

    sqlite3_thread_t th;
#if defined(_WIN32) || defined(_WIN64)
    th.handle = NULL;
    th.func = NULL;
    th.arg = NULL;
    th.retval = NULL;
    th.is_detached = 0;
#else
    th = (pthread_t)0;
#endif
    int rc = sqlite3_thread_create(&th, worker_signal, &ctx);
    assert(rc == 0);

    sqlite3_thread_mutex_lock(&ctx.mutex);
    while (!ctx.ready) {
        sqlite3_cond_wait(&ctx.cond, &ctx.mutex);
    }
    assert(ctx.counter == 777);
    sqlite3_thread_mutex_unlock(&ctx.mutex);

    sqlite3_thread_join(&th, NULL);
    sqlite3_cond_destroy(&ctx.cond);
    sqlite3_thread_mutex_destroy(&ctx.mutex);
    printf("   [PASS] Condition variable successfully signaled.\n");
}

void test_cond_timedwait(void) {
    printf("2. Testing Pure C condition variable timedwait...\n");
    sqlite3_thread_mutex_t mutex;
    sqlite3_cond_t cond;
    sqlite3_thread_mutex_init(&mutex);
    sqlite3_cond_init(&cond);

    sqlite3_thread_mutex_lock(&mutex);
    uint64_t start_ms = sqlite3_time_ms();
    // Wait for 30ms on condition that will never be signaled
    int rc = sqlite3_cond_timedwait(&cond, &mutex, 30);
    uint64_t elapsed_ms = sqlite3_time_ms() - start_ms;

    assert(rc != 0); // Must report timeout
    assert(elapsed_ms >= 20); // At least ~20-30ms elapsed
    sqlite3_thread_mutex_unlock(&mutex);

    sqlite3_cond_destroy(&cond);
    sqlite3_thread_mutex_destroy(&mutex);
    printf("   [PASS] Timedwait timed out accurately after %llu ms.\n", (unsigned long long)elapsed_ms);
}

void test_cond_broadcast(void) {
    printf("3. Testing Pure C condition variable broadcast to multiple workers...\n");
    sync_context_t ctx;
    sqlite3_thread_mutex_init(&ctx.mutex);
    sqlite3_cond_init(&ctx.cond);
    ctx.ready = 0;
    ctx.counter = 0;

    const int NUM_WORKERS = 4;
    sqlite3_thread_t workers[4];

    for (int i = 0; i < NUM_WORKERS; i++) {
        assert(sqlite3_thread_create(&workers[i], worker_broadcast, &ctx) == 0);
    }

    sqlite3_time_sleep_ms(20);

    sqlite3_thread_mutex_lock(&ctx.mutex);
    ctx.ready = 1;
    sqlite3_cond_broadcast(&ctx.cond);
    sqlite3_thread_mutex_unlock(&ctx.mutex);

    for (int i = 0; i < NUM_WORKERS; i++) {
        assert(sqlite3_thread_join(&workers[i], NULL) == 0);
    }

    assert(ctx.counter == NUM_WORKERS);
    sqlite3_cond_destroy(&ctx.cond);
    sqlite3_thread_mutex_destroy(&ctx.mutex);
    printf("   [PASS] Broadcast woke up all %d worker threads.\n", NUM_WORKERS);
}

int main(void) {
    printf("=================================================================\n");
    printf("Running Pure C Condition Variable & Synchronization Test Suite\n");
    printf("=================================================================\n");

    test_cond_signal();
    test_cond_timedwait();
    test_cond_broadcast();

    printf("\nAll Pure C Condition Variable Tests Passed Cleanly!\n");
    return 0;
}
