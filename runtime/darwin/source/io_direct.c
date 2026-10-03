/* Direct payload engines: requests of FILE and SOCKET handles reach the descriptor through system
   calls instead of Dispatch I/O channels.

   FILE handles (regular files of std.fs) are the file payload adapter of Core R-TERM-0016 and
   R-SLIB-ASYNC-0019. An activated read or write is admitted while fewer than
   R_RUNTIME_DARWIN_IO_FILE_MAX_ENTERED_TRANSFERS transfers are admitted and otherwise waits in a
   process-wide FIFO. An admitted transfer runs as a work item of a private concurrent Dispatch
   queue, never on an executor worker (R-SLIB-ASYNC-0010): pread or pwrite at the request's
   stream position or, on a RANDOM handle, at its offset from the handle's base, or write for an
   append descriptor without a position, directly in the request buffer; the work item then takes
   the next waiting transfer on the same thread. A cancellation or deadline finishes a waiting
   transfer on the handle's callback queue and an admitted one before its system call. An entered
   call is not preempted, and no further call follows it once an event is recorded.

   SOCKET handles (nonblocking stream sockets of std.net) try the nonblocking system call when
   the request is activated. A transfer that completes there is finished by the scheduler after
   it releases the handle mutex, so an adapter that registers its completion with
   r_runtime_darwin_io_request_set_completion_inline publishes the result before its task start
   returns. Otherwise the request waits for readiness on a Dispatch read or write source of the
   handle, which runs on the handle's callback queue and continues the transfer there; a
   cancellation or deadline of a waiting request is applied on the same queue.

   Both engines count every activity that may still use the descriptor in direct_inflight. The
   release of a direct handle's root cancels its sources and runs the root cleanup, which closes
   the descriptor and reports pending closes, only after that count reaches zero: the guarantee
   that Dispatch I/O gives a channel's cleanup handler. */

#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/* R-SLIB-ASYNC-0019: the bound of admitted file transfers; the target manifest records the same
   number as file_payload_adapter.maximum_entered_transfers. */
#define R_RUNTIME_DARWIN_IO_FILE_MAX_ENTERED_TRANSFERS 4U

_Static_assert(sizeof(off_t) == sizeof(int64_t), "file offsets are 64-bit");

typedef enum RRuntimeDarwinIoDirectOutcome {
    R_RUNTIME_DARWIN_IO_DIRECT_DONE = 1,
    R_RUNTIME_DARWIN_IO_DIRECT_WAIT
} RRuntimeDarwinIoDirectOutcome;

static dispatch_once_t file_queue_once;
static dispatch_queue_t file_queue_value;

/* Admission of the file payload adapter: the number of admitted transfers and the FIFO of
   activated transfers that wait for a slot. */
static pthread_mutex_t file_admission_mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t file_admitted;
static RRuntimeDarwinIoRequest *file_waiting_head;
static RRuntimeDarwinIoRequest *file_waiting_tail;

static void file_queue_create(void *context) {
    (void)context;
    file_queue_value = dispatch_queue_create("r.io.file", DISPATCH_QUEUE_CONCURRENT);
    if (file_queue_value == NULL) {
        abort();
    }
    dispatch_set_target_queue(file_queue_value,
                              dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U));
}

static dispatch_queue_t file_queue(void) {
    dispatch_once_f(&file_queue_once, NULL, file_queue_create);
    return file_queue_value;
}

static size_t direction_index(const RRuntimeDarwinIoRequest *request) {
    return request->operation == R_RUNTIME_DARWIN_IO_READ ? 0U : 1U;
}

static void direct_root_cleanup(void *context) {
    r_runtime_darwin_io_internal_handle_root_cleanup(context, 0);
}

static void inflight_release(RRuntimeDarwinIoHandle *handle) {
    _Bool schedule = 0;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (handle->direct_inflight == 0U) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    handle->direct_inflight -= 1U;
    if (handle->direct_inflight == 0U && handle->direct_cleanup_pending &&
        !handle->direct_cleanup_scheduled) {
        handle->direct_cleanup_scheduled = 1;
        schedule = 1;
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    if (schedule) {
        dispatch_async_f(handle->callback_queue, handle, direct_root_cleanup);
    }
}

/* Records the native outcome of a transfer; the event sequence is taken now, after the system
   call returned (and after any testing pause placed between the call and its completion). */
static void record_native(RRuntimeDarwinIoRequest *request, int native_error, _Bool eof) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    request->operation_error = native_error;
    request->operation_native_error = native_error;
    request->eof = eof;
    if (request->direct_native_sequence == UINT64_C(0)) {
        request->direct_native_sequence = r_runtime_darwin_event_sequence_next();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static void record_progress(RRuntimeDarwinIoRequest *request, size_t count) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (count > request->requested_size - request->bytes_transferred) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->bytes_transferred += count;
    request->bytes_remaining = request->requested_size - request->bytes_transferred;
    if (request->operation == R_RUNTIME_DARWIN_IO_READ) {
        request->buffer.size = request->bytes_transferred;
    }
    (void)pthread_cond_broadcast(&request->condition);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

/* Finishes a direct request whose native work is over. native_context marks the worker, readiness
   and cancellation contexts, where the next same-direction request is scheduled here and the
   native-completion testing pause applies; an activation context leaves both to the scheduler. */
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
static RRuntimeDarwinIoTestPause direct_worker_exit_test_pause =
    R_RUNTIME_DARWIN_IO_TEST_PAUSE_INITIALIZER;

void r_runtime_darwin_io_testing_pause_next_direct_worker_exit(void) {
    r_runtime_darwin_io_internal_test_pause_arm(&direct_worker_exit_test_pause);
}

void r_runtime_darwin_io_testing_wait_for_direct_worker_exit(void) {
    r_runtime_darwin_io_internal_test_pause_wait(&direct_worker_exit_test_pause);
}

void r_runtime_darwin_io_testing_release_direct_worker_exit(void) {
    r_runtime_darwin_io_internal_test_pause_release(&direct_worker_exit_test_pause);
}

static void testing_pause_direct_worker_exit(void) {
    r_runtime_darwin_io_internal_test_pause_point(&direct_worker_exit_test_pause);
}
#else
static void testing_pause_direct_worker_exit(void) {
}
#endif

static void direct_finish(RRuntimeDarwinIoRequest *request, _Bool native_context) {
    RRuntimeDarwinIoHandle *handle = request->handle;
    _Bool finalized;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->direct_native_sequence == UINT64_C(0)) {
        request->direct_native_sequence = r_runtime_darwin_event_sequence_next();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (native_context) {
        r_runtime_darwin_io_internal_testing_pause_native_completion();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation_done || !request->native_scheduled) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->native_event_sequence = request->direct_native_sequence;
    request->operation_done = 1;
    request->cleanup_done = 1;
    finalized = r_runtime_darwin_io_internal_request_finalize_locked(request);
    (void)pthread_cond_broadcast(&request->condition);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (!finalized) {
        abort();
    }
    r_runtime_darwin_io_internal_handle_notify_request_terminal(handle);
    r_runtime_darwin_io_internal_disarm_deadline(request);
    (void)r_runtime_darwin_io_internal_deliver_completion(request);
    if (native_context) {
        r_runtime_darwin_io_internal_schedule_ready_requests(handle);
    }
    inflight_release(handle);
    r_runtime_darwin_io_internal_request_release(request);
    if (native_context) {
        testing_pause_direct_worker_exit();
    }
}

void r_runtime_darwin_io_internal_direct_finish_activated(RRuntimeDarwinIoRequest *request) {
    direct_finish(request, 0);
}

/* ---- FILE ---- */

static const unsigned char *write_bytes(const RRuntimeDarwinIoRequest *request) {
    return request->buffer_transfer_required ? request->buffer.data : request->prepared_buffer_data;
}

static _Bool event_pending(RRuntimeDarwinIoRequest *request) {
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

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
static pthread_mutex_t file_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t file_gate_condition = PTHREAD_COND_INITIALIZER;
static _Atomic _Bool file_gate_hint;
static _Bool file_gate_closed;
static size_t file_gate_held;

void r_runtime_darwin_io_testing_hold_file_transfers(void) {
    if (pthread_mutex_lock(&file_gate_mutex) != 0) {
        abort();
    }
    file_gate_closed = 1;
    atomic_store_explicit(&file_gate_hint, 1, memory_order_release);
    if (pthread_mutex_unlock(&file_gate_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_wait_for_held_file_transfers(size_t count) {
    if (pthread_mutex_lock(&file_gate_mutex) != 0) {
        abort();
    }
    while (file_gate_held < count) {
        if (pthread_cond_wait(&file_gate_condition, &file_gate_mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&file_gate_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_release_file_transfers(void) {
    if (pthread_mutex_lock(&file_gate_mutex) != 0) {
        abort();
    }
    file_gate_closed = 0;
    atomic_store_explicit(&file_gate_hint, 0, memory_order_release);
    if (pthread_cond_broadcast(&file_gate_condition) != 0) {
        abort();
    }
    if (pthread_mutex_unlock(&file_gate_mutex) != 0) {
        abort();
    }
}

size_t r_runtime_darwin_io_testing_file_transfers_admitted(void) {
    size_t admitted;

    if (pthread_mutex_lock(&file_admission_mutex) != 0) {
        abort();
    }
    admitted = file_admitted;
    if (pthread_mutex_unlock(&file_admission_mutex) != 0) {
        abort();
    }
    return admitted;
}

size_t r_runtime_darwin_io_testing_file_transfers_waiting(void) {
    const RRuntimeDarwinIoRequest *request;
    size_t waiting = 0U;

    if (pthread_mutex_lock(&file_admission_mutex) != 0) {
        abort();
    }
    for (request = file_waiting_head; request != NULL; request = request->direct_admission_next) {
        waiting += 1U;
    }
    if (pthread_mutex_unlock(&file_admission_mutex) != 0) {
        abort();
    }
    return waiting;
}

static void testing_hold_file_transfer(void) {
    if (!atomic_load_explicit(&file_gate_hint, memory_order_acquire)) {
        return;
    }
    if (pthread_mutex_lock(&file_gate_mutex) != 0) {
        abort();
    }
    file_gate_held += 1U;
    if (pthread_cond_broadcast(&file_gate_condition) != 0) {
        abort();
    }
    while (file_gate_closed) {
        if (pthread_cond_wait(&file_gate_condition, &file_gate_mutex) != 0) {
            abort();
        }
    }
    file_gate_held -= 1U;
    if (pthread_mutex_unlock(&file_gate_mutex) != 0) {
        abort();
    }
}
#else
static void testing_hold_file_transfer(void) {
}
#endif

/* The file offset of the next call, or -1 when base + offset + transferred is not representable
   (the sum is formed without signed overflow). */
static off_t file_offset(off_t base, off_t offset, size_t transferred) {
    const uint64_t limit = (uint64_t)INT64_MAX;
    uint64_t value;

    if (base < 0 || offset < 0) {
        return (off_t)-1;
    }
    value = (uint64_t)base;
    if ((uint64_t)offset > limit - value) {
        return (off_t)-1;
    }
    value += (uint64_t)offset;
    if ((uint64_t)transferred > limit - value) {
        return (off_t)-1;
    }
    return (off_t)(value + (uint64_t)transferred);
}

static void file_transfer(RRuntimeDarwinIoRequest *request) {
    const RRuntimeDarwinIoHandle *handle = request->handle;
    const int descriptor = handle->retained_descriptor;
    RRuntimeDarwinIoOperation operation;
    unsigned char *read_data;
    const unsigned char *written_data;
    size_t size;
    size_t transferred = 0U;
    off_t base = 0;
    off_t offset;
    _Bool positioned;
    _Bool some;
    _Bool pending;
    _Bool eof = 0;
    int native_error = 0;

    testing_hold_file_transfer();
    r_runtime_darwin_io_internal_testing_pause_position_entry();
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    pending = request->pending_event != 0;
    operation = request->operation;
    read_data = request->buffer.data;
    written_data = write_bytes(request);
    size = request->requested_size;
    some = request->complete_after_progress;
    if (handle->type == R_RUNTIME_DARWIN_IO_RANDOM) {
        positioned = 1;
        base = handle->direct_random_base;
        offset = request->prepared_offset;
    } else {
        positioned = request->stream_position_enabled;
        offset = request->prepared_stream_position;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (!pending) {
        while (transferred < size) {
            const size_t remaining = size - transferred;
            const off_t at = positioned ? file_offset(base, offset, transferred) : (off_t)0;
            ssize_t count;

            if (at < 0) {
                native_error = EOVERFLOW;
                break;
            }
            if (operation == R_RUNTIME_DARWIN_IO_READ) {
                count = positioned ? pread(descriptor, read_data + transferred, remaining, at)
                                   : read(descriptor, read_data + transferred, remaining);
            } else {
                count = positioned ? pwrite(descriptor, written_data + transferred, remaining, at)
                                   : write(descriptor, written_data + transferred, remaining);
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count < 0) {
                native_error = errno;
                break;
            }
            if (count == 0) {
                if (operation == R_RUNTIME_DARWIN_IO_READ) {
                    eof = 1;
                } else {
                    native_error = EIO;
                }
                break;
            }
            transferred += (size_t)count;
            record_progress(request, (size_t)count);
            /* R-SLIB-ASYNC-0019: no further call once a cancellation or deadline is recorded. */
            if (some || (transferred < size && event_pending(request))) {
                break;
            }
        }
        record_native(request, native_error, eof);
    }
    direct_finish(request, 1);
}

/* The next waiting transfer for a finishing work item, or NULL after its admission slot is
   returned. */
static RRuntimeDarwinIoRequest *admission_next(void) {
    RRuntimeDarwinIoRequest *request;

    if (pthread_mutex_lock(&file_admission_mutex) != 0) {
        abort();
    }
    request = file_waiting_head;
    if (request != NULL) {
        file_waiting_head = request->direct_admission_next;
        if (file_waiting_head == NULL) {
            file_waiting_tail = NULL;
        }
        request->direct_admission_next = NULL;
        request->direct_admission_waiting = 0;
    } else {
        if (file_admitted == 0U) {
            abort();
        }
        file_admitted -= 1U;
    }
    if (pthread_mutex_unlock(&file_admission_mutex) != 0) {
        abort();
    }
    return request;
}

static void file_worker(void *context) {
    RRuntimeDarwinIoRequest *request = context;

    while (request != NULL) {
        file_transfer(request);
        request = admission_next();
    }
}

/* Called with the handle mutex held; the admission mutex nests inside it and nowhere else. */
static void file_submit(RRuntimeDarwinIoRequest *request) {
    _Bool admitted;

    if (pthread_mutex_lock(&file_admission_mutex) != 0) {
        abort();
    }
    admitted = file_admitted < R_RUNTIME_DARWIN_IO_FILE_MAX_ENTERED_TRANSFERS;
    if (admitted) {
        file_admitted += 1U;
    } else {
        request->direct_admission_next = NULL;
        request->direct_admission_waiting = 1;
        if (file_waiting_tail == NULL) {
            file_waiting_head = request;
        } else {
            file_waiting_tail->direct_admission_next = request;
        }
        file_waiting_tail = request;
    }
    if (pthread_mutex_unlock(&file_admission_mutex) != 0) {
        abort();
    }
    if (admitted) {
        dispatch_async_f(file_queue(), request, file_worker);
    }
}

/* A transfer still waiting for admission is taken out of the FIFO and finished here without a
   system call; an admitted one sees the event before its call. */
static void file_cancel_worker(void *context) {
    RRuntimeDarwinIoRequest *request = context;
    _Bool claimed = 0;

    if (pthread_mutex_lock(&file_admission_mutex) != 0) {
        abort();
    }
    if (request->direct_admission_waiting) {
        RRuntimeDarwinIoRequest **link = &file_waiting_head;
        RRuntimeDarwinIoRequest *previous = NULL;

        while (*link != request) {
            if (*link == NULL) {
                abort();
            }
            previous = *link;
            link = &previous->direct_admission_next;
        }
        *link = request->direct_admission_next;
        if (file_waiting_tail == request) {
            file_waiting_tail = previous;
        }
        request->direct_admission_next = NULL;
        request->direct_admission_waiting = 0;
        claimed = 1;
    }
    if (pthread_mutex_unlock(&file_admission_mutex) != 0) {
        abort();
    }
    if (claimed) {
        direct_finish(request, 1);
    }
    r_runtime_darwin_io_internal_request_release(request);
}

/* ---- SOCKET ---- */

/* After the root release the sources belong to their cancellation; a late suspend is a no-op. */
static void suspend_source_locked(RRuntimeDarwinIoHandle *handle, size_t index) {
    if (!handle->socket_sources_cancelled &&
        handle->socket_source_states[index] == R_RUNTIME_DARWIN_IO_SOURCE_RESUMED) {
        dispatch_suspend(handle->socket_sources[index]);
        handle->socket_source_states[index] = R_RUNTIME_DARWIN_IO_SOURCE_SUSPENDED;
    }
}

static void resume_source_locked(RRuntimeDarwinIoHandle *handle, size_t index) {
    if (handle->socket_sources_cancelled || handle->socket_sources[index] == NULL) {
        abort();
    }
    switch (handle->socket_source_states[index]) {
    case R_RUNTIME_DARWIN_IO_SOURCE_INACTIVE:
        dispatch_activate(handle->socket_sources[index]);
        break;
    case R_RUNTIME_DARWIN_IO_SOURCE_SUSPENDED:
        dispatch_resume(handle->socket_sources[index]);
        break;
    case R_RUNTIME_DARWIN_IO_SOURCE_RESUMED:
        break;
    }
    handle->socket_source_states[index] = R_RUNTIME_DARWIN_IO_SOURCE_RESUMED;
}

/* Moves bytes with nonblocking system calls until the request is complete, the descriptor would
   block, end of stream or an error. Testing pauses apply only in native contexts, between the
   progress and the native completion event. */
static RRuntimeDarwinIoDirectOutcome socket_transfer(RRuntimeDarwinIoRequest *request,
                                                     _Bool native_context) {
    const int descriptor = request->handle->retained_descriptor;

    for (;;) {
        RRuntimeDarwinIoOperation operation;
        unsigned char *read_data;
        const unsigned char *written_data;
        size_t size;
        size_t transferred;
        ssize_t count;
        _Bool some;
        _Bool pending;
        _Bool complete;

        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        operation = request->operation;
        read_data = request->buffer.data;
        written_data = write_bytes(request);
        size = request->requested_size;
        transferred = request->bytes_transferred;
        some = request->complete_after_progress;
        pending = request->pending_event != 0;
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        if (pending || transferred == size) {
            return R_RUNTIME_DARWIN_IO_DIRECT_DONE;
        }
        count = operation == R_RUNTIME_DARWIN_IO_READ
                    ? read(descriptor, read_data + transferred, size - transferred)
                    : write(descriptor, written_data + transferred, size - transferred);
        if (count > 0) {
            record_progress(request, (size_t)count);
            complete = some || transferred + (size_t)count == size;
            if (native_context) {
                if (operation == R_RUNTIME_DARWIN_IO_READ && some) {
                    r_runtime_darwin_io_internal_testing_pause_read_after_progress(request);
                } else if (operation == R_RUNTIME_DARWIN_IO_WRITE && (some || !complete)) {
                    r_runtime_darwin_io_internal_testing_pause_write_after_progress(request);
                }
            }
            if (complete) {
                record_native(request, 0, 0);
                return R_RUNTIME_DARWIN_IO_DIRECT_DONE;
            }
            continue;
        }
        if (count == 0 && operation == R_RUNTIME_DARWIN_IO_READ) {
            record_native(request, 0, 1);
            return R_RUNTIME_DARWIN_IO_DIRECT_DONE;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            return R_RUNTIME_DARWIN_IO_DIRECT_WAIT;
        }
        record_native(request, errno, 0);
        return R_RUNTIME_DARWIN_IO_DIRECT_DONE;
    }
}

static void socket_ready(RRuntimeDarwinIoHandle *handle, size_t index) {
    RRuntimeDarwinIoRequest *request;
    _Bool claimed;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (handle->socket_sources_cancelled) {
        if (pthread_mutex_unlock(&handle->mutex) != 0) {
            abort();
        }
        return;
    }
    request = handle->socket_waiting[index];
    if (request == NULL) {
        suspend_source_locked(handle, index);
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    if (request == NULL || socket_transfer(request, 1) == R_RUNTIME_DARWIN_IO_DIRECT_WAIT) {
        return;
    }
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    claimed = handle->socket_waiting[index] == request;
    if (claimed) {
        handle->socket_waiting[index] = NULL;
        suspend_source_locked(handle, index);
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    if (claimed) {
        direct_finish(request, 1);
    }
}

static void socket_read_ready(void *context) {
    socket_ready(context, 0U);
}

static void socket_write_ready(void *context) {
    socket_ready(context, 1U);
}

static void socket_source_cancelled(void *context) {
    inflight_release(context);
}

static void socket_cancel_worker(void *context) {
    RRuntimeDarwinIoRequest *request = context;
    RRuntimeDarwinIoHandle *handle = request->handle;
    const size_t index = direction_index(request);
    _Bool claimed;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    claimed = handle->socket_waiting[index] == request;
    if (claimed) {
        handle->socket_waiting[index] = NULL;
        suspend_source_locked(handle, index);
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    if (claimed) {
        direct_finish(request, 1);
    }
    r_runtime_darwin_io_internal_request_release(request);
}

static _Bool socket_immediate_allowed(const RRuntimeDarwinIoRequest *request) {
    if (r_runtime_darwin_io_internal_testing_native_completion_pause_armed()) {
        return 0;
    }
    return request->operation == R_RUNTIME_DARWIN_IO_READ
               ? !r_runtime_darwin_io_internal_testing_read_pause_armed()
               : !r_runtime_darwin_io_internal_testing_write_pause_armed();
}

_Bool r_runtime_darwin_io_internal_direct_create_sources(RRuntimeDarwinIoHandle *handle) {
    dispatch_source_t input = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,
                                                     (uintptr_t)handle->retained_descriptor,
                                                     0U,
                                                     handle->callback_queue);
    dispatch_source_t output = NULL;

    if (input != NULL) {
        output = dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE,
                                        (uintptr_t)handle->retained_descriptor,
                                        0U,
                                        handle->callback_queue);
    }
    if (input == NULL || output == NULL) {
        /* Neither source has handlers yet; activation lets the release complete. */
        if (input != NULL) {
            dispatch_activate(input);
            dispatch_source_cancel(input);
            dispatch_release(input);
        }
        return 0;
    }
    dispatch_set_context(input, handle);
    dispatch_set_context(output, handle);
    dispatch_source_set_event_handler_f(input, socket_read_ready);
    dispatch_source_set_event_handler_f(output, socket_write_ready);
    dispatch_source_set_cancel_handler_f(input, socket_source_cancelled);
    dispatch_source_set_cancel_handler_f(output, socket_source_cancelled);
    handle->socket_sources[0] = input;
    handle->socket_sources[1] = output;
    handle->socket_source_states[0] = R_RUNTIME_DARWIN_IO_SOURCE_INACTIVE;
    handle->socket_source_states[1] = R_RUNTIME_DARWIN_IO_SOURCE_INACTIVE;
    handle->direct_inflight = 2U;
    return 1;
}

/* ---- Shared entry points ---- */

_Bool r_runtime_darwin_io_internal_direct_activate(RRuntimeDarwinIoRequest *request,
                                                   RRuntimeDarwinIoRequest **finished) {
    RRuntimeDarwinIoHandle *handle = request->handle;
    _Bool pending;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state != R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE || request->native_scheduled) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return 0;
    }
    if (request->references == SIZE_MAX || handle->direct_inflight == SIZE_MAX ||
        (request->operation != R_RUNTIME_DARWIN_IO_READ &&
         request->operation != R_RUNTIME_DARWIN_IO_WRITE)) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->native_scheduled = 1;
    /* The native retain keeps request and handle alive through direct_finish. */
    request->references += 1U;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    handle->direct_inflight += 1U;
    if (handle->engine == R_RUNTIME_DARWIN_IO_ENGINE_FILE) {
        file_submit(request);
        return 1;
    }
    if (socket_immediate_allowed(request) &&
        socket_transfer(request, 0) == R_RUNTIME_DARWIN_IO_DIRECT_DONE) {
        *finished = request;
        return 1;
    }
    /* A cancellation recorded before the wait is registered finishes the request now; one
       recorded later finds it waiting (both sides decide under the handle mutex). */
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    pending = request->pending_event != 0;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (pending) {
        *finished = request;
        return 1;
    }
    handle->socket_waiting[direction_index(request)] = request;
    resume_source_locked(handle, direction_index(request));
    return 1;
}

void r_runtime_darwin_io_internal_direct_signal(RRuntimeDarwinIoRequest *request) {
    const RRuntimeDarwinIoEngine engine = request->handle->engine;

    if (engine == R_RUNTIME_DARWIN_IO_ENGINE_DISPATCH) {
        return;
    }
    r_runtime_darwin_io_internal_request_retain(request);
    dispatch_async_f(request->handle->callback_queue,
                     request,
                     engine == R_RUNTIME_DARWIN_IO_ENGINE_SOCKET ? socket_cancel_worker
                                                                 : file_cancel_worker);
}

static void cancel_source(dispatch_source_t source, RRuntimeDarwinIoSourceState state) {
    switch (state) {
    case R_RUNTIME_DARWIN_IO_SOURCE_INACTIVE:
        dispatch_activate(source);
        dispatch_source_cancel(source);
        break;
    case R_RUNTIME_DARWIN_IO_SOURCE_RESUMED:
        dispatch_source_cancel(source);
        break;
    case R_RUNTIME_DARWIN_IO_SOURCE_SUSPENDED:
        /* A suspended source is resumed after the cancel so that its cancel handler runs and
           its release is balanced. */
        dispatch_source_cancel(source);
        dispatch_resume(source);
        break;
    }
    dispatch_release(source);
}

/* Like a Dispatch I/O root closed with DISPATCH_IO_STOP, the release stops every request still
   waiting for readiness: each is cancelled and finished by its cancellation worker. */
void r_runtime_darwin_io_internal_direct_release_root(RRuntimeDarwinIoHandle *handle) {
    dispatch_source_t sources[2] = {NULL, NULL};
    RRuntimeDarwinIoSourceState states[2] = {R_RUNTIME_DARWIN_IO_SOURCE_INACTIVE,
                                             R_RUNTIME_DARWIN_IO_SOURCE_INACTIVE};
    RRuntimeDarwinIoRequest *waiting[2] = {NULL, NULL};
    _Bool schedule = 0;
    size_t index;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (handle->engine == R_RUNTIME_DARWIN_IO_ENGINE_DISPATCH || handle->direct_cleanup_pending) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    handle->direct_cleanup_pending = 1;
    if (handle->engine == R_RUNTIME_DARWIN_IO_ENGINE_SOCKET && !handle->socket_sources_cancelled) {
        handle->socket_sources_cancelled = 1;
        for (index = 0U; index != 2U; ++index) {
            waiting[index] = handle->socket_waiting[index];
            if (waiting[index] != NULL) {
                r_runtime_darwin_io_internal_request_retain(waiting[index]);
            }
            sources[index] = handle->socket_sources[index];
            states[index] = handle->socket_source_states[index];
            handle->socket_sources[index] = NULL;
        }
    }
    if (handle->direct_inflight == 0U && !handle->direct_cleanup_scheduled) {
        handle->direct_cleanup_scheduled = 1;
        schedule = 1;
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    for (index = 0U; index != 2U; ++index) {
        if (waiting[index] != NULL) {
            (void)r_runtime_darwin_io_internal_request_cancel_for_close(waiting[index]);
            r_runtime_darwin_io_internal_request_release(waiting[index]);
        }
        if (sources[index] != NULL) {
            cancel_source(sources[index], states[index]);
        }
    }
    if (schedule) {
        dispatch_async_f(handle->callback_queue, handle, direct_root_cleanup);
    }
}
