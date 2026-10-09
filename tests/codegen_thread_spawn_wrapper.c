#include "r_runtime_array.h"
#include "r_std_thread.h"

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * R-LIB-0005, R-LIB-0010: the program spawns, joins and detaches threads whose payload is a
 * one-byte array marked with the value the program gives it. The wrapper refuses the first
 * thread creation and observes every array drop and every joined value, so each payload is shown
 * to be dropped exactly once on every path: by the catch after a refused start, by the entry,
 * with a thrown error by its catch, and by the library when it drops the result of a detached
 * thread.
 */
void r_test_array_destroy(RRuntimeArray *array);
void r_test_thread_join_result_move(RStdThreadJoinResult *result, void *destination);
RStdThreadSpawnResult r_test_thread_spawn_checked(RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo payload_type,
                                                  RStdThreadCompletionTypeInfo completion_type,
                                                  RStdThreadEntryFn entry,
                                                  void *staged_payload);
RStdThreadScopedSpawnResult
r_test_thread_spawn_scoped_checked(RRuntimeAllocator *allocator,
                                   RRuntimeTypeInfo payload_type,
                                   RStdThreadCompletionTypeInfo completion_type,
                                   RStdThreadEntryFn entry,
                                   void *staged_payload);
void r_test_thread_detach(RStdThreadJoinHandle *handle);

#define r_library_internal_thread_join_result_move r_test_thread_join_result_move
#define r_library_internal_thread_spawn_checked r_test_thread_spawn_checked
#define r_library_internal_thread_spawn_scoped_checked r_test_thread_spawn_scoped_checked
#define r_runtime_array_destroy r_test_array_destroy
#define r_std_thread_detach r_test_thread_detach
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_thread_detach
#undef r_runtime_array_destroy
#undef r_library_internal_thread_spawn_scoped_checked
#undef r_library_internal_thread_spawn_checked
#undef r_library_internal_thread_join_result_move

void r_library_internal_thread_testing_fail_create(int native_error_value);

static _Atomic unsigned int r_test_drop_11;
static _Atomic unsigned int r_test_drop_41;
static _Atomic unsigned int r_test_drop_42;
static _Atomic unsigned int r_test_drop_43;
static _Atomic unsigned int r_test_drop_73;
static _Atomic unsigned int r_test_drop_91;
static _Atomic unsigned int r_test_join_41;
static _Atomic unsigned int r_test_join_42;
static _Atomic unsigned int r_test_join_error_73;
static unsigned int r_test_spawns;
static unsigned int r_test_scoped_spawns;
static unsigned int r_test_refused_spawns;
static unsigned int r_test_detaches;

static void r_test_record_drop(uint8_t marker) {
    switch (marker) {
    case UINT8_C(11):
        (void)atomic_fetch_add_explicit(&r_test_drop_11, 1U, memory_order_relaxed);
        break;
    case UINT8_C(41):
        (void)atomic_fetch_add_explicit(&r_test_drop_41, 1U, memory_order_relaxed);
        break;
    case UINT8_C(42):
        (void)atomic_fetch_add_explicit(&r_test_drop_42, 1U, memory_order_relaxed);
        break;
    case UINT8_C(43):
        (void)atomic_fetch_add_explicit(&r_test_drop_43, 1U, memory_order_relaxed);
        break;
    case UINT8_C(73):
        (void)atomic_fetch_add_explicit(&r_test_drop_73, 1U, memory_order_relaxed);
        break;
    case UINT8_C(91):
        (void)atomic_fetch_add_explicit(&r_test_drop_91, 1U, memory_order_relaxed);
        break;
    default:
        break;
    }
}

void r_test_array_destroy(RRuntimeArray *array) {
    if ((array != NULL) && (array->data != NULL) && (array->length == 1U)) {
        r_test_record_drop(*(const uint8_t *)array->data);
    }
    r_runtime_array_destroy(array);
}

void r_test_thread_join_result_move(RStdThreadJoinResult *result, void *destination) {
    RStdThreadCompletionTypeInfo completion_type;

    if ((result == NULL) || (destination == NULL)) {
        return;
    }
    completion_type = result->completion_type;
    r_library_internal_thread_join_result_move(result, destination);
    if (completion_type.error_count == UINT32_C(0)) {
        int32_t value = INT32_C(0);

        (void)memcpy(&value, destination, sizeof(value));
        if (value == INT32_C(41)) {
            (void)atomic_fetch_add_explicit(&r_test_join_41, 1U, memory_order_relaxed);
        } else if (value == INT32_C(42)) {
            (void)atomic_fetch_add_explicit(&r_test_join_42, 1U, memory_order_relaxed);
        }
    } else {
        uint32_t tag = UINT32_C(0);
        int32_t code = INT32_C(0);
        const unsigned char *const bytes = destination;

        (void)memcpy(&tag, bytes + completion_type.tag_offset, sizeof(tag));
        if (tag == UINT32_C(1)) {
            (void)memcpy(&code, bytes + completion_type.payload_offset, sizeof(code));
            if (code == INT32_C(-73)) {
                (void)atomic_fetch_add_explicit(&r_test_join_error_73, 1U, memory_order_relaxed);
            }
        }
    }
}

/* The first creation fails in the native call, after the library has reserved its descriptor:
   the refused start shall release that state and leave the staged payload to the caller. */
RStdThreadSpawnResult r_test_thread_spawn_checked(RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo payload_type,
                                                  RStdThreadCompletionTypeInfo completion_type,
                                                  RStdThreadEntryFn entry,
                                                  void *staged_payload) {
    const _Bool refuse = r_test_spawns == 0U;
    RStdThreadSpawnResult result;

    r_test_spawns += 1U;
    if (refuse) {
        r_library_internal_thread_testing_fail_create(EAGAIN);
    }
    result = r_library_internal_thread_spawn_checked(
        allocator, payload_type, completion_type, entry, staged_payload);
    if (refuse && !result.is_ok && (result.error == R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED)) {
        r_test_refused_spawns += 1U;
    }
    return result;
}

RStdThreadScopedSpawnResult
r_test_thread_spawn_scoped_checked(RRuntimeAllocator *allocator,
                                   RRuntimeTypeInfo payload_type,
                                   RStdThreadCompletionTypeInfo completion_type,
                                   RStdThreadEntryFn entry,
                                   void *staged_payload) {
    r_test_scoped_spawns += 1U;
    return r_library_internal_thread_spawn_scoped_checked(
        allocator, payload_type, completion_type, entry, staged_payload);
}

void r_test_thread_detach(RStdThreadJoinHandle *handle) {
    r_test_detaches += 1U;
    r_std_thread_detach(handle);
}

static _Bool r_test_once(_Atomic unsigned int *counter) {
    return atomic_load_explicit(counter, memory_order_relaxed) == 1U;
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);

    if (status != 0) {
        return status;
    }
    /* The program spawns five unscoped threads (one refused) and one scoped thread, and
       detaches the two unscoped threads it does not join. */
    if ((r_test_spawns != 5U) || (r_test_refused_spawns != 1U) || (r_test_scoped_spawns != 1U) ||
        (r_test_detaches != 2U)) {
        return 71;
    }
    if (!r_test_once(&r_test_join_41) || !r_test_once(&r_test_join_42) ||
        !r_test_once(&r_test_join_error_73)) {
        return 72;
    }
    /* The program ends after the hosted drain, which waits for the detached threads, so every
       drop has happened by now. */
    if (!r_test_once(&r_test_drop_11) || !r_test_once(&r_test_drop_41) ||
        !r_test_once(&r_test_drop_42) || !r_test_once(&r_test_drop_43) ||
        !r_test_once(&r_test_drop_73) || !r_test_once(&r_test_drop_91)) {
        return 73;
    }
    return 0;
}
