/*
 * Owners transferred into tasks and threads (Core R-FUNC-0010, R-FUNC-0012, R-AM-0014); the
 * fixture's scenarios check what R observes (lengths, destructions, results, start errors) and
 * this wrapper drives the failures and checks what only the runtime entry points observe:
 *
 * - The frame allocation of the 3rd, 6th, 9th, 12th and 16th task start of the program (the
 *   hosted root is the 1st) is refused: the starts marked "refused" in the fixture. Each refused
 *   start makes exactly that one allocation attempt.
 * - std.thread::spawn is refused at its 1st, 2nd, ... allocation attempt in turn until a spawn
 *   succeeds; a refused spawn reports resource exhaustion and stops at the refused attempt.
 * - Every list or dict that is destroyed with an element still holds it at the address it was
 *   stored at, so a container moved through a refused start, a task, a checked error or a thread
 *   was never copied element by element.
 * - parameter_resume creates the notify handles `gate` and `ready` and then starts `gated`, the
 *   task wait_for awaits. Once wait_for has suspended on it, the wrapper opens `ready`; the
 *   suspended frame keeps the task it awaits and its final await consumes it, by completion
 *   after main opens the gate, or by cancellation while it waits.
 */
#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_dict.h"
#include "r_runtime_list.h"
#include "r_runtime_task.h"
#include "r_std_async.h"
#include "r_std_dict.h"
#include "r_std_list.h"
#include "r_std_thread.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step);
RRuntimeTaskStartResult r_test_commit(RRuntimeTask **transaction,
                                      RRuntimeTaskPayloadInitializeFn initialize,
                                      const void *context);
RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result);
RStdAsyncNotifyNewResult r_test_notify_new(RRuntimeAllocator *allocator);
RStdThreadSpawnResult r_test_spawn(RRuntimeAllocator *allocator,
                                   RRuntimeTypeInfo payload_type,
                                   RStdThreadCompletionTypeInfo completion_type,
                                   RStdThreadEntryFn entry,
                                   void *staged_payload);
RStdListInsertResult r_test_push_back(RStdList *target, void *staged_value);
void r_test_list_destroy(RRuntimeList *list);
RStdDictInsertResult
r_test_dict_insert(RStdDict *target, void *staged_key, void *staged_value, void *replaced_storage);
void r_test_dict_destroy(RRuntimeDict *dict);

#define r_runtime_task_resumable_start_prepare r_test_prepare
#define r_runtime_task_start_commit_initialize r_test_commit
#define r_runtime_task_execution_await r_test_await
#define r_std_async_notify_new r_test_notify_new
#define r_library_internal_thread_spawn_checked r_test_spawn
#define r_std_list_push_back r_test_push_back
#define r_runtime_list_destroy r_test_list_destroy
#define r_std_dict_insert r_test_dict_insert
#define r_runtime_dict_destroy r_test_dict_destroy
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_dict_destroy
#undef r_std_dict_insert
#undef r_runtime_list_destroy
#undef r_std_list_push_back
#undef r_library_internal_thread_spawn_checked
#undef r_std_async_notify_new
#undef r_runtime_task_execution_await
#undef r_runtime_task_start_commit_initialize
#undef r_runtime_task_resumable_start_prepare

/* The hooks run on executor workers and on the spawned threads; the scenarios run one after
   another, so the runtime orders the events of one scenario, and atomics keep the unrelated
   concurrent reads well defined. */
static atomic_int r_test_failed_line;

static void r_test_fail(int line) {
    int expected = 0;
    (void)atomic_compare_exchange_strong(&r_test_failed_line, &expected, line);
}

#define R_TEST_REQUIRE(condition)                                                                  \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            r_test_fail(__LINE__);                                                                 \
        }                                                                                          \
    } while (0)

/* Refused task starts. */
static const unsigned r_test_refused_starts[] = {3U, 6U, 9U, 12U, 16U};
static atomic_uint r_test_starts;
static atomic_uint r_test_start_refusals;

static _Bool r_test_start_refused(unsigned ordinal) {
    for (size_t index = 0U; index < sizeof(r_test_refused_starts) / sizeof(unsigned); ++index) {
        if (r_test_refused_starts[index] == ordinal) {
            return 1;
        }
    }
    return 0;
}

RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step) {
    const unsigned ordinal = atomic_fetch_add(&r_test_starts, 1U) + 1U;
    RRuntimeAllocator *allocator;
    RRuntimeTaskPrepareResult prepared;

    if (!r_test_start_refused(ordinal)) {
        return r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    }
    /* No other task allocates while a scenario starts its task: the earlier scenarios have
       ended, and the only task started just before (number) allocates nothing. */
    allocator = r_runtime_hosted_allocator();
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    prepared = r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    R_TEST_REQUIRE(prepared.transaction == NULL &&
                   prepared.status == R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    R_TEST_REQUIRE(r_runtime_allocator_attempt_count(allocator) == UINT64_C(1));
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    (void)atomic_fetch_add(&r_test_start_refusals, 1U);
    return prepared;
}

/* Refused thread spawns. */
#define R_TEST_SPAWN_FAILURE_POINTS 32U
static atomic_uint r_test_spawn_refusals;
static atomic_bool r_test_spawned;

RStdThreadSpawnResult r_test_spawn(RRuntimeAllocator *allocator,
                                   RRuntimeTypeInfo payload_type,
                                   RStdThreadCompletionTypeInfo completion_type,
                                   RStdThreadEntryFn entry,
                                   void *staged_payload) {
    const unsigned failure = atomic_load(&r_test_spawn_refusals) + 1U;
    RStdThreadSpawnResult started;

    if (atomic_load(&r_test_spawned)) {
        return r_library_internal_thread_spawn_checked(
            allocator, payload_type, completion_type, entry, staged_payload);
    }
    if (failure > R_TEST_SPAWN_FAILURE_POINTS) {
        /* A spawn that never stops allocating would loop the fixture forever. */
        r_test_fail(__LINE__);
        atomic_store(&r_test_spawned, 1);
        return r_library_internal_thread_spawn_checked(
            allocator, payload_type, completion_type, entry, staged_payload);
    }
    r_runtime_allocator_set_failure(allocator, (uint64_t)failure);
    started = r_library_internal_thread_spawn_checked(
        allocator, payload_type, completion_type, entry, staged_payload);
    if (started.is_ok) {
        atomic_store(&r_test_spawned, 1);
    } else {
        R_TEST_REQUIRE(started.error == R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED);
        R_TEST_REQUIRE(r_runtime_allocator_attempt_count(allocator) == (uint64_t)failure);
        (void)atomic_fetch_add(&r_test_spawn_refusals, 1U);
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    return started;
}

/* Element addresses. Every list and dict of the fixture holds one element, and each is
   destroyed before the next one is filled. */
static _Atomic(void *) r_test_list_element;
static _Atomic(const void *) r_test_dict_entry;
static atomic_uint r_test_list_destroys;
static atomic_uint r_test_dict_destroys;

RStdListInsertResult r_test_push_back(RStdList *target, void *staged_value) {
    const RStdListInsertResult inserted = r_std_list_push_back(target, staged_value);
    if (inserted.status == R_STD_LIST_CALL_SUCCESS) {
        atomic_store(&r_test_list_element, inserted.value);
    }
    return inserted;
}

void r_test_list_destroy(RRuntimeList *list) {
    if (list->length != 0U) {
        R_TEST_REQUIRE(list->length == 1U);
        R_TEST_REQUIRE(r_runtime_list_front(list) == atomic_load(&r_test_list_element));
        (void)atomic_fetch_add(&r_test_list_destroys, 1U);
    }
    r_runtime_list_destroy(list);
}

RStdDictInsertResult
r_test_dict_insert(RStdDict *target, void *staged_key, void *staged_value, void *replaced_storage) {
    int32_t key;
    RStdDictInsertResult inserted;

    /* Every dict of the fixture has i32 keys; the insert consumes the staged key. */
    (void)memcpy(&key, staged_key, sizeof(key));
    inserted = r_std_dict_insert(target, staged_key, staged_value, replaced_storage);
    if (inserted.status == R_STD_DICT_CALL_SUCCESS) {
        atomic_store(&r_test_dict_entry, r_runtime_dict_get(target, &key));
    }
    return inserted;
}

void r_test_dict_destroy(RRuntimeDict *dict) {
    const int32_t key = INT32_C(11);
    if (dict->length != 0U) {
        R_TEST_REQUIRE(dict->length == 1U);
        R_TEST_REQUIRE(r_runtime_dict_get(dict, &key) == atomic_load(&r_test_dict_entry));
        (void)atomic_fetch_add(&r_test_dict_destroys, 1U);
    }
    r_runtime_dict_destroy(dict);
}

/* The suspension of wait_for in parameter_resume. A reference of its own to the notify handle
   created last (`ready`) lets the wrapper open it although main may drop its handle as soon as
   it wakes. */
static _Atomic(RLibraryAsyncNotifyState *) r_test_last_notify;
static atomic_bool r_test_capture_commit;
static _Atomic(RRuntimeTask *) r_test_gated_task;
static _Atomic(RRuntimeTask **) r_test_waiting_slot;
static atomic_uint r_test_suspensions;
static atomic_uint r_test_resumptions;
static atomic_uint r_test_resumed_outcomes;

RStdAsyncNotifyNewResult r_test_notify_new(RRuntimeAllocator *allocator) {
    RStdAsyncNotifyNewResult created = r_std_async_notify_new(allocator);
    if (created.status == R_STD_ASYNC_CALL_SUCCESS) {
        RStdAsyncNotify previous = {NULL};
        const RStdAsyncNotify own = r_std_async_clone_notify(&created.value);
        previous.state = atomic_exchange(&r_test_last_notify, own.state);
        r_std_async_notify_destroy(&previous);
        atomic_store(&r_test_capture_commit, 1);
    }
    return created;
}

/* The first task committed after the notify handles is `gated`. */
RRuntimeTaskStartResult r_test_commit(RRuntimeTask **transaction,
                                      RRuntimeTaskPayloadInitializeFn initialize,
                                      const void *context) {
    const RRuntimeTaskStartResult started =
        r_runtime_task_start_commit_initialize(transaction, initialize, context);
    if (started.status == R_RUNTIME_TASK_START_OK && atomic_exchange(&r_test_capture_commit, 0)) {
        atomic_store(&r_test_gated_task, started.task);
    }
    return started;
}

RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result) {
    RRuntimeTask *const awaited = *task;
    const RRuntimeTaskExecutionAwaitStatus status =
        r_runtime_task_execution_await(execution, task, result);

    if (awaited == NULL || awaited != atomic_load(&r_test_gated_task)) {
        return status;
    }
    if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
        /* A cancelled frame is resumed to pass the cancellation on to the task it awaits and
           waits again until that task has finished. */
        if (atomic_load(&r_test_waiting_slot) == NULL) {
            RStdAsyncNotify ready = {atomic_exchange(&r_test_last_notify, NULL)};
            R_TEST_REQUIRE(ready.state != NULL);
            /* Recorded before main can complete or cancel the frame on another worker. */
            atomic_store(&r_test_waiting_slot, task);
            (void)atomic_fetch_add(&r_test_suspensions, 1U);
            r_std_async_notify_one(&ready);
            r_std_async_notify_destroy(&ready);
        } else {
            R_TEST_REQUIRE(atomic_load(&r_test_waiting_slot) == task);
        }
        return status;
    }
    /* The final await of the resumed frame names the same task in the same slot, and consumes
       it whether the task completed or the frame was cancelled. */
    R_TEST_REQUIRE(atomic_load(&r_test_waiting_slot) == task);
    R_TEST_REQUIRE(*task == NULL);
    (void)atomic_fetch_or(&r_test_resumed_outcomes,
                          status == R_RUNTIME_TASK_EXECUTION_AWAIT_OK          ? 1U
                          : status == R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED ? 2U
                                                                               : 4U);
    (void)atomic_fetch_add(&r_test_resumptions, 1U);
    atomic_store(&r_test_waiting_slot, NULL);
    atomic_store(&r_test_gated_task, NULL);
    return status;
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    const int failed_line = atomic_load(&r_test_failed_line);

    if (failed_line != 0) {
        (void)fprintf(stderr, "owner matrix check failed at line %d\n", failed_line);
        return failed_line;
    }
    if (status != 0) {
        (void)fprintf(stderr, "owner matrix fixture failed with status %d\n", status);
        return status;
    }
    R_TEST_REQUIRE(atomic_load(&r_test_start_refusals) ==
                   sizeof(r_test_refused_starts) / sizeof(unsigned));
    R_TEST_REQUIRE(atomic_load(&r_test_spawned) && atomic_load(&r_test_spawn_refusals) != 0U);
    R_TEST_REQUIRE(atomic_load(&r_test_list_destroys) == 4U);
    R_TEST_REQUIRE(atomic_load(&r_test_dict_destroys) == 3U);
    R_TEST_REQUIRE(atomic_load(&r_test_last_notify) == NULL);
    R_TEST_REQUIRE(atomic_load(&r_test_suspensions) == 2U);
    R_TEST_REQUIRE(atomic_load(&r_test_resumptions) == 2U);
    R_TEST_REQUIRE(atomic_load(&r_test_resumed_outcomes) == 3U);
    if (atomic_load(&r_test_failed_line) != 0) {
        (void)fprintf(
            stderr, "owner matrix check failed at line %d\n", atomic_load(&r_test_failed_line));
        return atomic_load(&r_test_failed_line);
    }
    return 0;
}
