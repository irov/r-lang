#include "r_runtime_darwin_fs_lane.h"

#include "fs_lane_internal.h"
#include <dispatch/dispatch.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

void r_runtime_darwin_fs_internal_request_retain(RRuntimeDarwinFsRequest *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->references += 1U;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_fs_request_retain(RRuntimeDarwinFsRequest *request) {
    r_runtime_darwin_fs_internal_request_retain(request);
}

_Bool r_runtime_darwin_fs_internal_has_pending_event(RRuntimeDarwinFsRequest *request) {
    _Bool pending;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    pending = request->pending_event != 0;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return pending;
}

static void deadline_cancelled(void *context) {
    RRuntimeDarwinFsRequest *request = context;

    r_runtime_darwin_fs_request_release(request);
}

static void deadline_fired(void *context) {
    RRuntimeDarwinFsRequest *request = context;
    dispatch_source_t timer = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->deadline_timer != NULL) {
        timer = request->deadline_timer;
        request->deadline_timer = NULL;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (timer != NULL) {
        (void)r_runtime_darwin_fs_request_deadline_expired(request);
        dispatch_source_cancel(timer);
        dispatch_release(timer);
    }
}

_Bool r_runtime_darwin_fs_internal_prepare_deadline(RRuntimeDarwinFsRequest *request,
                                                    uint64_t timeout_nanoseconds) {
    dispatch_source_t timer;

    if (timeout_nanoseconds == 0U) {
        return 1;
    }
    if (timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return 0;
    }
    timer = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, request->lane->completion_queue);
    if (timer == NULL) {
        return 0;
    }
    r_runtime_darwin_fs_internal_request_retain(request);
    dispatch_set_context(timer, request);
    dispatch_source_set_event_handler_f(timer, deadline_fired);
    dispatch_source_set_cancel_handler_f(timer, deadline_cancelled);
    dispatch_source_set_timer(timer,
                              dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_nanoseconds),
                              DISPATCH_TIME_FOREVER,
                              0U);
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    request->deadline_timer = timer;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return 1;
}

void r_runtime_darwin_fs_internal_activate_deadline(RRuntimeDarwinFsRequest *request) {
    dispatch_source_t timer = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->deadline_timer != NULL && !request->deadline_activated) {
        request->deadline_activated = 1;
        timer = request->deadline_timer;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (timer != NULL) {
        dispatch_activate(timer);
    }
}

void r_runtime_darwin_fs_internal_disarm_deadline(RRuntimeDarwinFsRequest *request) {
    dispatch_source_t timer = NULL;
    _Bool activated = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->deadline_timer != NULL) {
        timer = request->deadline_timer;
        request->deadline_timer = NULL;
        activated = request->deadline_activated;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (timer != NULL) {
        if (!activated) {
            dispatch_activate(timer);
        }
        dispatch_source_cancel(timer);
        dispatch_release(timer);
    }
}

static void completion_worker(void *context) {
    RRuntimeDarwinFsRequest *request = context;
    RRuntimeDarwinFsCompletionFn completion;
    void *completion_context;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    completion = request->completion;
    completion_context = request->completion_context;
    if (completion == NULL || !request->completion_scheduled ||
        request->state != R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    completion(request, completion_context);
    r_runtime_darwin_fs_request_release(request);
}

void r_runtime_darwin_fs_internal_schedule_completion(RRuntimeDarwinFsRequest *request) {
    _Bool schedule = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state == R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL && request->completion != NULL &&
        !request->completion_scheduled) {
        if (request->references == SIZE_MAX) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        request->references += 1U;
        request->completion_scheduled = 1;
        schedule = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (schedule) {
        dispatch_async_f(request->lane->completion_queue, request, completion_worker);
    }
}

_Bool r_runtime_darwin_fs_request_set_completion(RRuntimeDarwinFsRequest *request,
                                                 RRuntimeDarwinFsCompletionFn completion,
                                                 void *context) {
    _Bool accepted = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state != R_RUNTIME_DARWIN_FS_REQUEST_PREPARED && request->completion == NULL) {
        /* The request can reach its terminal state and run the completion on another thread as
           soon as the mutex is released; this reference keeps it alive until scheduling ends. */
        if (request->references == SIZE_MAX) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        request->references += 1U;
        request->completion = completion;
        request->completion_context = context;
        accepted = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (accepted) {
        r_runtime_darwin_fs_internal_schedule_completion(request);
        r_runtime_darwin_fs_request_release(request);
    }
    return accepted;
}
