#include "r_runtime_thread_attachment.h"

#include "r_runtime_0_1.h"
#include <fenv.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#pragma STDC FENV_ACCESS ON

typedef enum RRuntimeThreadLifecycleState {
    R_RUNTIME_THREAD_LIFECYCLE_STOPPED = 0,
    R_RUNTIME_THREAD_LIFECYCLE_RUNNING = 1,
    R_RUNTIME_THREAD_LIFECYCLE_STOPPING = 2
} RRuntimeThreadLifecycleState;

static pthread_mutex_t r_runtime_thread_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t r_runtime_thread_condition = PTHREAD_COND_INITIALIZER;
static RRuntimeThreadLifecycleState r_runtime_thread_state = R_RUNTIME_THREAD_LIFECYCLE_STOPPED;
static uint64_t r_runtime_thread_generation = UINT64_C(1);
static size_t r_runtime_thread_attachment_count;
static size_t r_runtime_hosted_work_count;
static _Bool r_runtime_external_entry_closed;
static _Thread_local size_t r_runtime_c_call_depth;
static _Thread_local _Bool r_runtime_thread_current_attached;
static _Thread_local size_t r_runtime_c_entry_depth;
static _Thread_local RRuntimeCEntryGuard *r_runtime_c_entry_top;
static _Atomic(RRuntimeThreadLocalCleanupFn) r_runtime_thread_local_cleanup;

static _Noreturn void r_runtime_thread_contract_violation(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static void r_runtime_thread_lock(void) {
    if (pthread_mutex_lock(&r_runtime_thread_mutex) != 0) {
        r_runtime_thread_contract_violation();
    }
}

static void r_runtime_thread_unlock(void) {
    if (pthread_mutex_unlock(&r_runtime_thread_mutex) != 0) {
        r_runtime_thread_contract_violation();
    }
}

_Bool r_runtime_thread_lifecycle_start(void) {
    _Bool started = 0;

    r_runtime_thread_lock();
    if ((r_runtime_thread_state == R_RUNTIME_THREAD_LIFECYCLE_STOPPED) &&
        (r_runtime_thread_attachment_count == 0U)) {
        r_runtime_thread_state = R_RUNTIME_THREAD_LIFECYCLE_RUNNING;
        r_runtime_external_entry_closed = 0;
        started = 1;
    }
    r_runtime_thread_unlock();
    return started;
}

void r_runtime_thread_lifecycle_stop(void) {
    int wait_status;

    if (r_runtime_thread_current_attached) {
        r_runtime_thread_contract_violation();
    }
    r_runtime_thread_lock();
    if (r_runtime_thread_state != R_RUNTIME_THREAD_LIFECYCLE_RUNNING) {
        r_runtime_thread_unlock();
        r_runtime_thread_contract_violation();
    }
    r_runtime_thread_state = R_RUNTIME_THREAD_LIFECYCLE_STOPPING;
    while (r_runtime_thread_attachment_count != 0U) {
        wait_status = pthread_cond_wait(&r_runtime_thread_condition, &r_runtime_thread_mutex);
        if (wait_status != 0) {
            r_runtime_thread_unlock();
            r_runtime_thread_contract_violation();
        }
    }
    r_runtime_thread_state = R_RUNTIME_THREAD_LIFECYCLE_STOPPED;
    if (r_runtime_thread_generation == UINT64_MAX) {
        r_runtime_thread_unlock();
        r_runtime_thread_contract_violation();
    }
    r_runtime_thread_generation += UINT64_C(1);
    r_runtime_thread_unlock();
}

static void r_runtime_thread_release_reservation(void) {
    r_runtime_thread_lock();
    if (r_runtime_thread_attachment_count == 0U) {
        r_runtime_thread_unlock();
        r_runtime_thread_contract_violation();
    }
    r_runtime_thread_attachment_count -= 1U;
    if (pthread_cond_broadcast(&r_runtime_thread_condition) != 0) {
        r_runtime_thread_contract_violation();
    }
    r_runtime_thread_unlock();
}

RRuntimeThreadAttachResult r_runtime_thread_attach(void) {
    RRuntimeThreadAttachResult result = {0};

    r_runtime_thread_lock();
    if (r_runtime_thread_state != R_RUNTIME_THREAD_LIFECYCLE_RUNNING ||
        (r_runtime_external_entry_closed && r_runtime_c_call_depth == 0U)) {
        result.status = R_RUNTIME_THREAD_ATTACH_RUNTIME_STOPPING;
        r_runtime_thread_unlock();
        return result;
    }
    if (r_runtime_thread_current_attached || (r_runtime_thread_attachment_count == SIZE_MAX)) {
        result.status = R_RUNTIME_THREAD_ATTACH_RESOURCE_EXHAUSTED;
        r_runtime_thread_unlock();
        return result;
    }
    r_runtime_thread_attachment_count += 1U;
    result.attachment.generation = r_runtime_thread_generation;
    r_runtime_thread_unlock();

    if (fegetenv(&result.attachment.caller_environment) != 0) {
        r_runtime_thread_release_reservation();
        result.status = R_RUNTIME_THREAD_ATTACH_FLOATING_ENVIRONMENT_UNAVAILABLE;
        return result;
    }
    if (fesetenv(FE_DFL_ENV) != 0) {
        if (fesetenv(&result.attachment.caller_environment) != 0) {
            abort();
        }
        r_runtime_thread_release_reservation();
        result.status = R_RUNTIME_THREAD_ATTACH_FLOATING_ENVIRONMENT_UNAVAILABLE;
        return result;
    }
#if defined(__APPLE__)
    if (!r_runtime_stack_initialize_current_thread()) {
        if (fesetenv(&result.attachment.caller_environment) != 0) {
            abort();
        }
        r_runtime_thread_release_reservation();
        result.status = R_RUNTIME_THREAD_ATTACH_RESOURCE_EXHAUSTED;
        return result;
    }
#endif
    r_runtime_thread_current_attached = 1;
    result.attachment.active = 1;
    result.status = R_RUNTIME_THREAD_ATTACH_OK;
    return result;
}

void r_runtime_thread_detach(RRuntimeThreadAttachment *attachment) {
    if ((attachment == NULL) || !attachment->active || !r_runtime_thread_current_attached ||
        (r_runtime_c_entry_depth != 0U)) {
        r_runtime_thread_contract_violation();
    }
    r_runtime_thread_lock();
    if ((attachment->generation != r_runtime_thread_generation) ||
        ((r_runtime_thread_state != R_RUNTIME_THREAD_LIFECYCLE_RUNNING) &&
         (r_runtime_thread_state != R_RUNTIME_THREAD_LIFECYCLE_STOPPING))) {
        r_runtime_thread_unlock();
        r_runtime_thread_contract_violation();
    }
    r_runtime_thread_unlock();
    /* A synchronous R -> C -> R callback returns to its still-live R thread.
     * Its TLS belongs to that thread, including during initial-thread destruction. */
    if (r_runtime_c_call_depth == 0U) {
        r_runtime_thread_local_cleanup_current();
    }
    if (fesetenv(&attachment->caller_environment) != 0) {
        r_runtime_thread_contract_violation();
    }
    attachment->active = 0;
    r_runtime_thread_current_attached = 0;
    r_runtime_thread_release_reservation();
}

void r_runtime_thread_local_cleanup_install(RRuntimeThreadLocalCleanupFn cleanup) {
    atomic_store_explicit(&r_runtime_thread_local_cleanup, cleanup, memory_order_release);
}

void r_runtime_thread_local_cleanup_current(void) {
    RRuntimeThreadLocalCleanupFn cleanup =
        atomic_load_explicit(&r_runtime_thread_local_cleanup, memory_order_acquire);

    if (cleanup != NULL) {
        cleanup();
    }
}

void r_runtime_c_call_begin(RRuntimeCEnvironment *environment) {
    if (r_runtime_c_call_depth == SIZE_MAX)
        r_runtime_thread_contract_violation();
    r_runtime_c_call_depth += 1U;
    if ((environment == NULL) || (fegetenv(&environment->environment) != 0)) {
        r_runtime_thread_contract_violation();
    }
    if (fesetenv(FE_DFL_ENV) != 0) {
        if (fesetenv(&environment->environment) != 0) {
            abort();
        }
        r_runtime_thread_contract_violation();
    }
}

void r_runtime_c_call_end(RRuntimeCEnvironment *environment) {
    if (r_runtime_c_call_depth == 0U)
        r_runtime_thread_contract_violation();
    r_runtime_c_call_depth -= 1U;
    if (environment == NULL) {
        r_runtime_thread_contract_violation();
    }
    if (fesetenv(&environment->environment) != 0) {
        abort();
    }
}

void r_runtime_c_entry_begin(RRuntimeCEntryGuard *guard) {
    const RRuntimeCEntryGuard *entry;

    if ((guard == NULL) || (r_runtime_c_entry_depth == SIZE_MAX)) {
        r_runtime_thread_contract_violation();
    }
    for (entry = r_runtime_c_entry_top; entry != NULL; entry = entry->previous) {
        if (entry == guard) {
            r_runtime_thread_contract_violation();
        }
    }
    *guard = (RRuntimeCEntryGuard){0};
    if (!r_runtime_thread_current_attached) {
        const RRuntimeThreadAttachResult attached = r_runtime_thread_attach();

        if (attached.status != R_RUNTIME_THREAD_ATTACH_OK) {
            abort();
        }
        guard->attachment = attached.attachment;
        guard->environment = attached.attachment.caller_environment;
        guard->owns_attachment = 1;
    } else if ((fegetenv(&guard->environment) != 0) || (fesetenv(FE_DFL_ENV) != 0)) {
        abort();
    }
    guard->previous = r_runtime_c_entry_top;
    guard->active = 1;
    r_runtime_c_entry_top = guard;
    r_runtime_c_entry_depth += 1U;
}

void r_runtime_c_entry_end(RRuntimeCEntryGuard *guard) {
    if ((guard == NULL) || (guard != r_runtime_c_entry_top) || !guard->active ||
        (r_runtime_c_entry_depth == 0U) || !r_runtime_thread_current_attached ||
        (guard->owns_attachment && (r_runtime_c_entry_depth != 1U))) {
        r_runtime_thread_contract_violation();
    }
    r_runtime_c_entry_top = guard->previous;
    r_runtime_c_entry_depth -= 1U;
    guard->active = 0;
    guard->previous = NULL;
    if (guard->owns_attachment) {
        r_runtime_thread_detach(&guard->attachment);
        guard->owns_attachment = 0;
    } else if (fesetenv(&guard->environment) != 0) {
        abort();
    }
}

void r_runtime_hosted_work_begin(void) {
    r_runtime_thread_lock();
    if (r_runtime_hosted_work_count == SIZE_MAX)
        r_runtime_thread_contract_violation();
    r_runtime_hosted_work_count += 1U;
    r_runtime_thread_unlock();
}

void r_runtime_hosted_work_end(void) {
    r_runtime_thread_lock();
    if (r_runtime_hosted_work_count == 0U)
        r_runtime_thread_contract_violation();
    r_runtime_hosted_work_count -= 1U;
    if (pthread_cond_broadcast(&r_runtime_thread_condition) != 0)
        r_runtime_thread_contract_violation();
    r_runtime_thread_unlock();
}

void r_runtime_hosted_work_drain(void) {
    r_runtime_thread_lock();
    while (r_runtime_hosted_work_count != 0U || r_runtime_thread_attachment_count != 0U) {
        if (pthread_cond_wait(&r_runtime_thread_condition, &r_runtime_thread_mutex) != 0)
            r_runtime_thread_contract_violation();
    }
    r_runtime_external_entry_closed = 1;
    r_runtime_thread_unlock();
}
