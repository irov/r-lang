#include "r_runtime_0_1.h"
#include "r_runtime_array.h"
#include "r_runtime_task.h"
#include "r_std_format.h"
#include "r_std_string.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RRuntimeAllocator r_test_allocator;
static void *r_test_owned[1024];
static size_t r_test_owned_count;
static atomic_flag r_test_owned_lock = ATOMIC_FLAG_INIT;
static atomic_size_t r_test_suspensions;
static atomic_size_t r_test_finalizations;
static atomic_bool r_test_cancel_enabled;
static atomic_bool r_test_cancel_observed;
static atomic_bool r_test_cancel_armed;

static void r_test_lock_owned(void) {
    while (atomic_flag_test_and_set_explicit(&r_test_owned_lock, memory_order_acquire)) {
    }
}
static void r_test_unlock_owned(void) {
    atomic_flag_clear_explicit(&r_test_owned_lock, memory_order_release);
}
static size_t r_test_live_count(void) {
    size_t count;
    r_test_lock_owned();
    count = r_test_owned_count;
    r_test_unlock_owned();
    return count;
}

static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *storage) {
    RRuntimeTaskExecutionAwaitStatus status =
        r_runtime_task_execution_await(execution, task, storage);
    if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
        (void)atomic_fetch_add_explicit(&r_test_suspensions, 1U, memory_order_relaxed);
    }
    if (atomic_load_explicit(&r_test_cancel_enabled, memory_order_acquire) &&
        r_test_live_count() != 0U) {
        atomic_store_explicit(&r_test_cancel_armed, 1, memory_order_release);
    }
    return status;
}
static _Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution) {
    return (atomic_load_explicit(&r_test_cancel_enabled, memory_order_acquire) &&
            atomic_load_explicit(&r_test_cancel_armed, memory_order_acquire)) ||
           r_runtime_task_execution_cancel_requested(execution);
}
static RRuntimeTaskAwaitStatus r_test_root_await(RRuntimeTask **task, void *storage) {
    RRuntimeTaskAwaitStatus status = r_runtime_task_await(task, storage);
    if (atomic_load_explicit(&r_test_cancel_enabled, memory_order_acquire) &&
        status == R_RUNTIME_TASK_AWAIT_CANCELLED) {
        atomic_store_explicit(&r_test_cancel_observed, 1, memory_order_release);
        *(int32_t *)storage = 0;
        return R_RUNTIME_TASK_AWAIT_OK;
    }
    return status;
}

static RRuntimeAllocator *r_test_hosted_allocator(void) {
    return &r_test_allocator;
}
static void r_test_record(void *data) {
    if (data == NULL) {
        return;
    }
    r_test_lock_owned();
    if (r_test_owned_count == sizeof(r_test_owned) / sizeof(r_test_owned[0])) {
        abort();
    }
    for (size_t index = 0U; index < r_test_owned_count; ++index) {
        if (r_test_owned[index] == data) {
            abort();
        }
    }
    r_test_owned[r_test_owned_count++] = data;
    r_test_unlock_owned();
}
static void r_test_release(void *data) {
    r_test_lock_owned();
    for (size_t index = 0U; index < r_test_owned_count; ++index) {
        if (r_test_owned[index] == data) {
            r_test_owned[index] = r_test_owned[--r_test_owned_count];
            r_test_unlock_owned();
            return;
        }
    }
    r_test_unlock_owned();
}
static RStdStringAllocValueResult r_test_from_str(RRuntimeAllocator *allocator,
                                                  RStdStringView view) {
    RStdStringAllocValueResult result = r_std_string_from_str(allocator, view);
    if (result.status == R_STD_STRING_CALL_SUCCESS) {
        r_test_record(result.value.bytes.data);
    }
    return result;
}
static RStdString r_test_finish(RStdFormatBuilder *builder) {
    RStdString result = r_std_format_finish(builder);
    r_test_record(result.bytes.data);
    if (result.bytes.length == 9U && memcmp(result.bytes.data, "finalized", 9U) == 0) {
        (void)atomic_fetch_add_explicit(&r_test_finalizations, 1U, memory_order_relaxed);
    }
    return result;
}
static void r_test_string_destroy(RStdString *source) {
    r_test_release(source->bytes.data);
    r_std_string_destroy(source);
}
static void r_test_array_destroy(RRuntimeArray *source) {
    r_test_release(source->data);
    r_runtime_array_destroy(source);
}

#define r_runtime_hosted_allocator r_test_hosted_allocator
#define r_std_string_from_str r_test_from_str
#define r_std_format_finish r_test_finish
#define r_std_string_destroy r_test_string_destroy
#define r_runtime_array_destroy r_test_array_destroy
#define r_runtime_task_execution_await r_test_await
#define r_runtime_task_execution_cancel_requested r_test_cancel_requested
#define r_runtime_task_await r_test_root_await
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_task_await
#undef r_runtime_task_execution_cancel_requested
#undef r_runtime_task_execution_await
#undef r_runtime_array_destroy
#undef r_std_string_destroy
#undef r_std_format_finish
#undef r_std_string_from_str
#undef r_runtime_hosted_allocator

int main(int argc, char *argv[]) {
    (void)r_test_await;
    (void)r_test_cancel_requested;
    (void)r_test_root_await;
    r_runtime_allocator_initialize(&r_test_allocator);
    int status = r_generated_main(argc, argv);
    uint64_t attempts = r_runtime_allocator_attempt_count(&r_test_allocator);
    size_t live_count = r_test_live_count();
    if (status != 0 || attempts == 0U || live_count != 0U) {
        (void)fprintf(stderr,
                      "format baseline status=%d attempts=%llu live=%zu\n",
                      status,
                      (unsigned long long)attempts,
                      live_count);
        return 101;
    }
    for (uint64_t failure = 1U; failure <= attempts + 1U; ++failure) {
        r_runtime_allocator_set_failure(&r_test_allocator, failure);
        status = r_generated_main(argc, argv);
        int expected = failure <= attempts ? 99 : 0;
        live_count = r_test_live_count();
        if ((status != expected && !(failure <= attempts && (status == 98 || status == 0))) ||
            live_count != 0U) {
            (void)fprintf(stderr,
                          "format failure %llu/%llu: status=%d expected=%d live=%zu\n",
                          (unsigned long long)failure,
                          (unsigned long long)attempts,
                          status,
                          expected,
                          live_count);
            return 102;
        }
    }
    if (atomic_load_explicit(&r_test_suspensions, memory_order_relaxed) != 0U) {
        r_runtime_allocator_set_failure(&r_test_allocator, 0U);
        atomic_store_explicit(&r_test_cancel_enabled, 1, memory_order_release);
        atomic_store_explicit(&r_test_cancel_armed, 0, memory_order_release);
        atomic_store_explicit(&r_test_finalizations, 0U, memory_order_relaxed);
        status = r_generated_main(argc, argv);
        live_count = r_test_live_count();
        if (status != 0 || !atomic_load_explicit(&r_test_cancel_observed, memory_order_acquire) ||
            live_count != 0U ||
            atomic_load_explicit(&r_test_finalizations, memory_order_relaxed) != 1U) {
            (void)fprintf(stderr,
                          "format cancellation: status=%d observed=%d live=%zu finally=%zu\n",
                          status,
                          (int)atomic_load_explicit(&r_test_cancel_observed, memory_order_acquire),
                          live_count,
                          atomic_load_explicit(&r_test_finalizations, memory_order_relaxed));
            return 103;
        }
    }
    return 0;
}
