#include "r_runtime_own.h"
#include "r_runtime_task.h"
#include "r_std_async.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifndef R_TEST_EXPECTED_OWN_DROPS
#define R_TEST_EXPECTED_OWN_DROPS 1U
#endif

/*
 * Parent cancellation of a task group (Core R-STMT-0017): `scenario` waits in group.all() while
 * its scoped child borrows the storage of an owner and is held inside its await. The program
 * cancels `scenario` (R-SLIB-ASYNC-0006); the cancel is held here until the child has entered its
 * await and `scenario` is suspended, as an external cancel of a suspended task. The child is held
 * until the group has begun its mandatory drain, so no owner may be released before the child's
 * finally has run.
 */
static _Atomic unsigned entered, draining, released, cancelled;
static _Atomic _Bool proceed, valid = 1;
RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result);
void r_test_cancel(RRuntimeTaskScope *scope);
void r_test_release(RRuntimeOwn *owner);
void r_test_async_cancel(RRuntimeTask **operation);
_Bool r_test_direct_begin(size_t stack_bytes);
#define r_runtime_task_execution_await r_test_await
#define r_runtime_task_direct_begin r_test_direct_begin
#define r_runtime_task_scope_cancel r_test_cancel
#define r_runtime_own_release r_test_release
#define r_std_async_cancel r_test_async_cancel
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_task_direct_begin
#undef r_std_async_cancel
#undef r_runtime_own_release
#undef r_runtime_task_scope_cancel
#undef r_runtime_task_execution_await

enum {
    R_TEST_ATTEMPTS = 5000
};

/* B7: the empty `pause` would run as a direct call; the test holds its await instead. */
_Bool r_test_direct_begin(size_t stack_bytes) {
    (void)stack_bytes;
    return 0;
}

static void r_test_pause(void) {
    const struct timespec interval = {0, 1000000L};
    (void)nanosleep(&interval, NULL);
}

RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result) {
    unsigned attempts = 0U;
    atomic_fetch_add(&entered, 1U);
    /* A native operation retains the borrowed frame until its explicit acknowledgement. */
    while (atomic_load(&draining) == 0U && attempts++ < R_TEST_ATTEMPTS) {
        r_test_pause();
    }
    if (attempts >= R_TEST_ATTEMPTS || atomic_load(&released) != 0U) {
        atomic_store(&valid, 0);
    }
    atomic_store(&proceed, 1);
    return r_runtime_task_execution_await(execution, task, result);
}

void r_test_cancel(RRuntimeTaskScope *scope) {
    r_runtime_task_scope_cancel(scope);
    atomic_fetch_add(&draining, 1U);
}

void r_test_release(RRuntimeOwn *owner) {
    if (!atomic_load(&proceed) || owner->allocation == NULL ||
        *(const int32_t *)owner->allocation != INT32_C(9)) {
        atomic_store(&valid, 0);
    }
    atomic_fetch_add(&released, 1U);
    r_runtime_own_release(owner);
}

void r_test_async_cancel(RRuntimeTask **operation) {
    unsigned attempts = 0U;
    while ((atomic_load(&entered) == 0U ||
            r_runtime_task_state(*operation) != R_RUNTIME_TASK_SUSPENDED) &&
           attempts++ < R_TEST_ATTEMPTS) {
        r_test_pause();
    }
    if (attempts >= R_TEST_ATTEMPTS || atomic_load(&released) != 0U ||
        atomic_load(&draining) != 0U) {
        atomic_store(&valid, 0);
    }
    r_std_async_cancel(operation);
    if (*operation != NULL) {
        atomic_store(&valid, 0);
    }
    atomic_fetch_add(&cancelled, 1U);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    if (status != 0 || !atomic_load(&valid) || atomic_load(&cancelled) != 1U ||
        atomic_load(&released) != R_TEST_EXPECTED_OWN_DROPS || atomic_load(&entered) != 1U) {
        (void)fprintf(stderr,
                      "scoped cancellation check failed: status %d, valid %d, cancelled %u, "
                      "released %u, entered %u\n",
                      status,
                      (int)atomic_load(&valid),
                      atomic_load(&cancelled),
                      atomic_load(&released),
                      atomic_load(&entered));
        return 1;
    }
    return 0;
}
