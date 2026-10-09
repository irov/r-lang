#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_std_thread.h"

#include <stddef.h>
#include <stdint.h>

/*
 * R-LIB-0010: the program spawns three running checked threads and detaches each through a
 * handle it moves: into a synchronous function, into an async function whose start the wrapper
 * refuses (main then detaches the handle the refused start left with it, R-SLIB-ASYNC-0003), and
 * into the same async function again. Each spawned handle shall reach the library detach exactly
 * once, live, and be consumed by it, so no handle reaches its destructor live.
 */
RStdThreadSpawnResult r_test_thread_spawn_checked(RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo payload_type,
                                                  RStdThreadCompletionTypeInfo completion_type,
                                                  RStdThreadEntryFn entry,
                                                  void *staged_payload);
void r_test_thread_detach(RStdThreadJoinHandle *handle);
void r_test_thread_handle_destroy(RStdThreadJoinHandle *handle);
RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step);

#define r_library_internal_thread_handle_destroy r_test_thread_handle_destroy
#define r_library_internal_thread_spawn_checked r_test_thread_spawn_checked
#define r_runtime_task_resumable_start_prepare r_test_prepare
#define r_std_thread_detach r_test_thread_detach
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_thread_detach
#undef r_runtime_task_resumable_start_prepare
#undef r_library_internal_thread_spawn_checked
#undef r_library_internal_thread_handle_destroy

enum {
    R_TEST_THREADS = 3
};

/* Spawns, starts and detaches run one after another on the tasks of main, which await each
   other, so plain counters suffice. */
static RStdThreadDescriptor *r_test_spawned[R_TEST_THREADS];
static size_t r_test_detached[R_TEST_THREADS];
static size_t r_test_spawn_count;
static size_t r_test_start_count;
static size_t r_test_refused_starts;
static size_t r_test_live_handle_destroys;
static _Bool r_test_valid = 1;

RStdThreadSpawnResult r_test_thread_spawn_checked(RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo payload_type,
                                                  RStdThreadCompletionTypeInfo completion_type,
                                                  RStdThreadEntryFn entry,
                                                  void *staged_payload) {
    const RStdThreadSpawnResult result = r_library_internal_thread_spawn_checked(
        allocator, payload_type, completion_type, entry, staged_payload);

    if (!result.is_ok || (r_test_spawn_count == (size_t)R_TEST_THREADS)) {
        r_test_valid = 0;
        return result;
    }
    r_test_spawned[r_test_spawn_count] = result.value.descriptor;
    r_test_spawn_count += 1U;
    return result;
}

/* Start 1 is main; start 2, the first z_detach_async, fails to allocate its frame. */
RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step) {
    RRuntimeAllocator *const allocator = r_runtime_hosted_allocator();
    RRuntimeTaskPrepareResult result;

    r_test_start_count += 1U;
    if (r_test_start_count != 2U) {
        return r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    result = r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    if (result.status == R_RUNTIME_TASK_START_ALLOCATION_FAILED) {
        r_test_refused_starts += 1U;
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    return result;
}

void r_test_thread_detach(RStdThreadJoinHandle *handle) {
    size_t index;
    _Bool known = 0;

    if ((handle == NULL) || (handle->descriptor == NULL)) {
        r_test_valid = 0;
        return;
    }
    /* A finished detached thread frees its descriptor, so a later spawn may reuse the address:
       the handle belongs to the earliest spawn with that address not detached yet. */
    for (index = 0U; (index < r_test_spawn_count) && !known; ++index) {
        if ((r_test_spawned[index] == handle->descriptor) && (r_test_detached[index] == 0U)) {
            r_test_detached[index] = 1U;
            known = 1;
        }
    }
    r_std_thread_detach(handle);
    if (!known || (handle->descriptor != NULL)) {
        r_test_valid = 0;
    }
}

void r_test_thread_handle_destroy(RStdThreadJoinHandle *handle) {
    if ((handle != NULL) && (handle->descriptor != NULL)) {
        r_test_live_handle_destroys += 1U;
    }
    r_library_internal_thread_handle_destroy(handle);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    size_t index;

    if (status != 0) {
        return status;
    }
    if (!r_test_valid || (r_test_spawn_count != (size_t)R_TEST_THREADS) ||
        (r_test_refused_starts != 1U) || (r_test_start_count != 3U)) {
        return 71;
    }
    for (index = 0U; index < (size_t)R_TEST_THREADS; ++index) {
        if (r_test_detached[index] != 1U) {
            return 72;
        }
    }
    if (r_test_live_handle_destroys != 0U) {
        return 73;
    }
    return 0;
}
