/*
 * An owner held by a suspended async frame (a fn once capture, an async closure capture or a
 * temporary argument evaluated before an await) stays alive while the frame waits, is consumed
 * exactly once when the frame completes and is destroyed exactly once when the frame is
 * cancelled while it waits (Core R-FUNC-0017, R-AM-0014). The fixture runs two rounds: the
 * first completes the waiting frame, the second cancels it. Each round creates two notify
 * handles, `gate`, which the awaited child waits on, and `ready`, which main waits on before it
 * either opens the gate or cancels the frame. The wrapper opens `ready` from the await of the
 * frame once that await has registered its wait, so the frame is suspended in both rounds
 * without any timing assumption. The owner's destructor adds 1000 to the owned value and the
 * consumer stores 7 there, so the released value tells a consumed owner (1007) from one the
 * cancellation destroyed (1022).
 */
#include "r_runtime_own.h"
#include "r_runtime_task.h"
#include "r_std_async.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result);
RStdAsyncNotifyNewResult r_test_notify_new(RRuntimeAllocator *allocator);
void r_test_release(RRuntimeOwn *owner);

#define r_runtime_task_execution_await r_test_await
#define r_std_async_notify_new r_test_notify_new
#define r_runtime_own_release r_test_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_own_release
#undef r_std_async_notify_new
#undef r_runtime_task_execution_await

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "suspension check failed at line %d\n", __LINE__);               \
            return __LINE__;                                                                       \
        }                                                                                          \
    } while (0)

/* A reference of its own to the notify handle created last, so that opening it cannot race
   with main dropping its handle once it wakes. */
static _Atomic(RLibraryAsyncNotifyState *) r_test_last_notify;
/* The awaited-task slot of the suspended frame and the task it held, or null outside a
   suspension. */
static _Atomic(RRuntimeTask **) r_test_waiting_slot;
static _Atomic(RRuntimeTask *) r_test_waiting_task;
static atomic_bool r_test_slot_changed;
static atomic_uint r_test_suspensions;
static atomic_uint r_test_resumptions;
static atomic_uint r_test_resumed_outcomes;
static atomic_uint r_test_releases;
static atomic_uint r_test_releases_at_suspension;
static atomic_uint r_test_released_values;
static atomic_bool r_test_released_while_suspended;

static void r_test_drop_notify(RLibraryAsyncNotifyState *state) {
    RStdAsyncNotify handle = {state};
    r_std_async_notify_destroy(&handle);
}

RStdAsyncNotifyNewResult r_test_notify_new(RRuntimeAllocator *allocator) {
    RStdAsyncNotifyNewResult created = r_std_async_notify_new(allocator);
    if (created.status == R_STD_ASYNC_CALL_SUCCESS) {
        const RStdAsyncNotify own = r_std_async_clone_notify(&created.value);
        r_test_drop_notify(atomic_exchange(&r_test_last_notify, own.state));
    }
    return created;
}

void r_test_release(RRuntimeOwn *owner) {
    const int32_t *value = r_runtime_own_get(owner);
    unsigned seen = 1U << 31U;
    if (value != NULL && *value == 1007) {
        seen = 1U;
    } else if (value != NULL && *value == 1022) {
        seen = 2U;
    }
    (void)atomic_fetch_or(&r_test_released_values, seen);
    (void)atomic_fetch_add(&r_test_releases, 1U);
    r_runtime_own_release(owner);
}

RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result) {
    RRuntimeTask *const awaited = *task;
    const RRuntimeTaskExecutionAwaitStatus status =
        r_runtime_task_execution_await(execution, task, result);

    /* The resumed frame awaits the same slot again. A cancelled frame is first resumed to pass
       the cancellation on to the awaited task and waits again (SUSPENDED) until that task has
       finished; the wait ends with OK or CANCELLED, which consumes the awaited task. Only the
       frame itself reads its slot here, so the steps of the frame order these accesses. */
    if (atomic_load(&r_test_waiting_slot) == task) {
        if (status != R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
            if (atomic_load(&r_test_releases) != atomic_load(&r_test_releases_at_suspension)) {
                atomic_store(&r_test_released_while_suspended, 1);
            }
            (void)atomic_fetch_or(&r_test_resumed_outcomes,
                                  status == R_RUNTIME_TASK_EXECUTION_AWAIT_OK          ? 1U
                                  : status == R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED ? 2U
                                                                                       : 4U);
            /* The frame kept the task it awaited across the suspension, and its final await
               consumed it. */
            if (awaited != atomic_load(&r_test_waiting_task) || *task != NULL) {
                atomic_store(&r_test_slot_changed, 1);
            }
            (void)atomic_fetch_add(&r_test_resumptions, 1U);
            atomic_store(&r_test_waiting_slot, NULL);
        }
        return status;
    }
    /* Only the frame awaits a task with a result before `ready` is open: main and the child wait
       for notifications, which have none. */
    if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED && result != NULL) {
        RStdAsyncNotify ready = {atomic_exchange(&r_test_last_notify, NULL)};
        if (ready.state != NULL) {
            /* Recorded before the frame can resume: opening `ready` lets main complete or
               cancel it on another worker while this step is still returning. */
            atomic_store(&r_test_releases_at_suspension, atomic_load(&r_test_releases));
            atomic_store(&r_test_waiting_task, awaited);
            atomic_store(&r_test_waiting_slot, task);
            (void)atomic_fetch_add(&r_test_suspensions, 1U);
            r_std_async_notify_one(&ready);
            r_std_async_notify_destroy(&ready);
        }
    }
    return status;
}

int main(int argc, char *argv[]) {
    CHECK(r_generated_main(argc, argv) == 0);
    CHECK(atomic_load(&r_test_last_notify) == NULL);
    CHECK(atomic_load(&r_test_waiting_slot) == NULL);
    CHECK(atomic_load(&r_test_suspensions) == 2U && atomic_load(&r_test_resumptions) == 2U);
    CHECK(atomic_load(&r_test_resumed_outcomes) == 3U && !atomic_load(&r_test_slot_changed));
    CHECK(!atomic_load(&r_test_released_while_suspended));
    CHECK(atomic_load(&r_test_releases) == 2U && atomic_load(&r_test_released_values) == 3U);
    return 0;
}
