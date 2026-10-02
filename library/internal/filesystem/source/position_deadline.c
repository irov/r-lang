#include "r_library_fs_internal.h"

#include "r_library_clock_internal.h"

#include <dispatch/dispatch.h>
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct RLibraryFsPositionDeadlineToken {
    _Atomic size_t references;
    pthread_mutex_t mutex;
    dispatch_source_t source;
    RLibraryFsPositionDeadlineExpiredFn expired;
    RLibraryFsOperationRetainFn retain_context;
    RLibraryFsOperationReleaseFn release_context;
    void *context;
    RStdTimeInstant continuous_deadline;
    _Bool activated;
    _Bool cancelled;
    _Bool expired_delivered;
};

static const uint64_t R_LIBRARY_FS_NANOSECONDS_PER_SECOND = UINT64_C(1000000000);
static const uint64_t R_LIBRARY_FS_CLOCK_RETRY_NANOSECONDS = UINT64_C(1000000000);

static void token_retain(RLibraryFsPositionDeadlineToken *token) {
    size_t references = atomic_load_explicit(&token->references, memory_order_relaxed);

    for (;;) {
        if (references == 0U || references == SIZE_MAX) {
            abort();
        }
        if (atomic_compare_exchange_weak_explicit(&token->references,
                                                  &references,
                                                  references + 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static void token_release(RLibraryFsPositionDeadlineToken *token) {
    size_t previous;

    previous = atomic_fetch_sub_explicit(&token->references, 1U, memory_order_acq_rel);
    if (previous == 0U) {
        abort();
    }
    if (previous != 1U) {
        return;
    }
    if (token->source != NULL || pthread_mutex_destroy(&token->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(token, _Alignof(RLibraryFsPositionDeadlineToken));
}

static int compare_instant(RStdTimeInstant left, RStdTimeInstant right) {
    if (left.storage_seconds != right.storage_seconds) {
        return left.storage_seconds < right.storage_seconds ? -1 : 1;
    }
    if (left.storage_nanoseconds != right.storage_nanoseconds) {
        return left.storage_nanoseconds < right.storage_nanoseconds ? -1 : 1;
    }
    return 0;
}

static uint64_t clamped_remaining_nanoseconds(RStdTimeInstant deadline, RStdTimeInstant now) {
    uint64_t seconds;
    uint64_t nanoseconds;

    if (compare_instant(deadline, now) <= 0) {
        return 0U;
    }
    seconds = (uint64_t)(deadline.storage_seconds - now.storage_seconds);
    if (deadline.storage_nanoseconds < now.storage_nanoseconds) {
        --seconds;
        nanoseconds = R_LIBRARY_FS_NANOSECONDS_PER_SECOND + (uint64_t)deadline.storage_nanoseconds -
                      (uint64_t)now.storage_nanoseconds;
    } else {
        nanoseconds = (uint64_t)deadline.storage_nanoseconds - (uint64_t)now.storage_nanoseconds;
    }
    if (seconds > (UINT64_MAX - nanoseconds) / R_LIBRARY_FS_NANOSECONDS_PER_SECOND) {
        return UINT64_MAX;
    }
    return seconds * R_LIBRARY_FS_NANOSECONDS_PER_SECOND + nanoseconds;
}

static dispatch_time_t dispatch_target(uint64_t remaining_nanoseconds) {
    uint64_t chunk = remaining_nanoseconds;
    dispatch_time_t target = DISPATCH_TIME_FOREVER;

    if (chunk > (uint64_t)INT64_MAX) {
        chunk = (uint64_t)INT64_MAX;
    }
    while (target == DISPATCH_TIME_FOREVER && chunk != 0U) {
        target = dispatch_time(DISPATCH_TIME_NOW, (int64_t)chunk);
        chunk /= 2U;
    }
    return target == DISPATCH_TIME_FOREVER ? DISPATCH_TIME_NOW : target;
}

static void arm_timer(dispatch_source_t source, uint64_t remaining_nanoseconds) {
    dispatch_source_set_timer(
        source, dispatch_target(remaining_nanoseconds), DISPATCH_TIME_FOREVER, 0U);
}

static void deadline_timer_fired(void *context) {
    RLibraryFsPositionDeadlineToken *token = context;
    RLibraryFsPositionDeadlineExpiredFn expired = NULL;
    void *expired_context = NULL;
    RStdTimeInstantResult now;

    token_retain(token);
    token->retain_context(token->context);
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (pthread_mutex_lock(&token->mutex) != 0) {
        abort();
    }
    if (!token->cancelled && !token->expired_delivered && token->source != NULL) {
        if (!now.is_ok || now.value.storage_seconds < 0 ||
            now.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
            arm_timer(token->source, R_LIBRARY_FS_CLOCK_RETRY_NANOSECONDS);
        } else if (compare_instant(now.value, token->continuous_deadline) >= 0) {
            token->expired_delivered = 1;
            expired = token->expired;
            expired_context = token->context;
        } else {
            arm_timer(token->source,
                      clamped_remaining_nanoseconds(token->continuous_deadline, now.value));
        }
    }
    if (pthread_mutex_unlock(&token->mutex) != 0) {
        abort();
    }
    if (expired != NULL) {
        expired(expired_context);
    }
    token->release_context(token->context);
    token_release(token);
}

static void deadline_cancelled(void *context) {
    RLibraryFsPositionDeadlineToken *token = context;
    RLibraryFsOperationReleaseFn release_context = token->release_context;
    void *release_value = token->context;

    release_context(release_value);
    token_release(token);
}

static void token_cancel_source(RLibraryFsPositionDeadlineToken *token) {
    dispatch_source_t source;
    _Bool activated;

    if (pthread_mutex_lock(&token->mutex) != 0) {
        abort();
    }
    source = token->source;
    activated = token->activated;
    token->source = NULL;
    token->cancelled = 1;
    if (pthread_mutex_unlock(&token->mutex) != 0) {
        abort();
    }
    if (source != NULL) {
        if (!activated) {
            dispatch_activate(source);
        }
        dispatch_source_cancel(source);
        dispatch_release(source);
    }
}

_Bool r_library_internal_fs_position_deadline_initialize(
    RLibraryFsPositionDeadline *deadline,
    RRuntimeAllocator *allocator,
    RStdTimeInstant continuous_deadline,
    RLibraryFsPositionDeadlineExpiredFn expired,
    RLibraryFsOperationRetainFn retain_context,
    RLibraryFsOperationReleaseFn release_context,
    void *context) {
    RLibraryFsPositionDeadlineToken *token = NULL;
    dispatch_queue_t queue;
    dispatch_source_t source;

    if (deadline == NULL || deadline->token != NULL || allocator == NULL || expired == NULL ||
        retain_context == NULL || release_context == NULL || context == NULL ||
        continuous_deadline.storage_seconds < 0 ||
        continuous_deadline.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        return 0;
    }
    if (r_runtime_allocator_allocate(allocator,
                                     sizeof(*token),
                                     _Alignof(RLibraryFsPositionDeadlineToken),
                                     (void **)&token) != R_RUNTIME_ALLOCATION_OK) {
        return 0;
    }
    (void)memset(token, 0, sizeof(*token));
    if (pthread_mutex_init(&token->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(token, _Alignof(RLibraryFsPositionDeadlineToken));
        return 0;
    }
    atomic_init(&token->references, 1U);
    token->expired = expired;
    token->retain_context = retain_context;
    token->release_context = release_context;
    token->context = context;
    token->continuous_deadline = continuous_deadline;
    deadline->token = token;
    queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
    if (queue == NULL) {
        r_library_internal_fs_position_deadline_destroy(deadline);
        return 0;
    }
    source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, queue);
    if (source == NULL) {
        r_library_internal_fs_position_deadline_destroy(deadline);
        return 0;
    }
    retain_context(context);
    atomic_store_explicit(&token->references, 2U, memory_order_relaxed);
    token->source = source;
    dispatch_set_context(source, token);
    dispatch_source_set_event_handler_f(source, deadline_timer_fired);
    dispatch_source_set_cancel_handler_f(source, deadline_cancelled);
    return 1;
}

void r_library_internal_fs_position_deadline_activate(RLibraryFsPositionDeadline *deadline) {
    RLibraryFsPositionDeadlineToken *token;
    RStdTimeInstantResult now;

    if (deadline == NULL || deadline->token == NULL) {
        abort();
    }
    token = deadline->token;
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (pthread_mutex_lock(&token->mutex) != 0) {
        abort();
    }
    if (token->source != NULL && !token->activated) {
        if (!now.is_ok || now.value.storage_seconds < 0 ||
            now.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
            arm_timer(token->source, R_LIBRARY_FS_CLOCK_RETRY_NANOSECONDS);
        } else {
            arm_timer(token->source,
                      clamped_remaining_nanoseconds(token->continuous_deadline, now.value));
        }
        token->activated = 1;
        dispatch_activate(token->source);
    }
    if (pthread_mutex_unlock(&token->mutex) != 0) {
        abort();
    }
}

void r_library_internal_fs_position_deadline_cancel(RLibraryFsPositionDeadline *deadline) {
    if (deadline == NULL || deadline->token == NULL) {
        return;
    }
    token_cancel_source(deadline->token);
}

void r_library_internal_fs_position_deadline_destroy(RLibraryFsPositionDeadline *deadline) {
    RLibraryFsPositionDeadlineToken *token;

    if (deadline == NULL) {
        return;
    }
    token = deadline->token;
    deadline->token = NULL;
    if (token == NULL) {
        return;
    }
    token_cancel_source(token);
    token_release(token);
}
