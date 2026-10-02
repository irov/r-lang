#include "io_internal.h"

#include <dispatch/dispatch.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

static void deadline_fired(RRuntimeDarwinIoRequest *request) {
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
        (void)r_runtime_darwin_io_request_deadline_expired(request);
        dispatch_source_cancel(timer);
        dispatch_release(timer);
    }
}

_Bool r_runtime_darwin_io_internal_prepare_deadline(RRuntimeDarwinIoRequest *request,
                                                    uint64_t timeout_nanoseconds) {
    dispatch_source_t timer;

    if (timeout_nanoseconds == 0U) {
        return 1;
    }
    if (timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return 0;
    }
    timer =
        dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, 0U, request->handle->callback_queue);
    if (timer == NULL) {
        return 0;
    }
    r_runtime_darwin_io_internal_request_retain(request);
    dispatch_source_set_event_handler(timer, ^{
      deadline_fired(request);
    });
    dispatch_source_set_cancel_handler(timer, ^{
      r_runtime_darwin_io_internal_request_release(request);
    });
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

void r_runtime_darwin_io_internal_activate_deadline(RRuntimeDarwinIoRequest *request) {
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

void r_runtime_darwin_io_internal_disarm_deadline(RRuntimeDarwinIoRequest *request) {
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
