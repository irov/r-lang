#include "r_library_json_internal.h"
#include "r_std_json_reader.h"

#include <stdalign.h>
#include <stdatomic.h>
#include <string.h>

struct RJsonReaderState {
    atomic_uint references;
    atomic_bool busy;
    RRuntimeAllocator *allocator;
    RStdJsonTransport transport;
    void *handle;
    void *read_storage;
    RRuntimeArray buffer;
    RStdJsonDecoder decoder;
    size_t offset;
    size_t length;
    bool handle_live;
    bool buffer_live;
    bool eof;
    bool poisoned;
    bool detached;
    bool only_whitespace;
};

typedef struct RJsonReadFrame {
    struct RJsonReaderState *reader;
    RRuntimeTask *child;
    RRuntimeTypeInfo value_type;
    RStdJsonDecodeCreateFn create;
    RStdJsonReadCompleteFn complete;
    RStdJsonDeadline deadline;
    bool accepted;
    bool started;
    bool done;
} RJsonReadFrame;

static void r_json_reader_release(struct RJsonReaderState *state) {
    if (state == NULL ||
        atomic_fetch_sub_explicit(&state->references, 1U, memory_order_acq_rel) != 1U)
        return;
    if (state->handle_live && state->transport.handle_type.drop != NULL)
        state->transport.handle_type.drop(state->handle);
    if (state->buffer_live)
        r_runtime_array_destroy(&state->buffer);
    r_json_decoder_destroy(&state->decoder);
    r_runtime_allocator_deallocate(state->handle, state->transport.handle_type.alignment);
    r_runtime_allocator_deallocate(state->read_storage, state->transport.read_type.alignment);
    r_runtime_allocator_deallocate(state, alignof(struct RJsonReaderState));
}

RStdJsonResult r_json_reader_initialize(RStdJsonReader *reader,
                                        RRuntimeAllocator *allocator,
                                        RStdJsonOptions options,
                                        RStdJsonTransport transport,
                                        void *handle) {
    *reader = (RStdJsonReader){0};
    if (allocator == NULL || handle == NULL || transport.handle_type.size == 0U ||
        transport.handle_type.move_initialize == NULL || transport.read_type.size == 0U ||
        transport.start == NULL || transport.take == NULL)
        return r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
    void *allocation = NULL;
    RStdJsonResult outcome = r_json_allocation_result(r_runtime_allocator_allocate(
        allocator, sizeof(struct RJsonReaderState), alignof(struct RJsonReaderState), &allocation));
    if (outcome.status != R_STD_JSON_CALL_SUCCESS)
        return outcome;
    struct RJsonReaderState *state = allocation;
    *state = (struct RJsonReaderState){
        .allocator = allocator, .transport = transport, .only_whitespace = true};
    atomic_init(&state->references, 1U);
    atomic_init(&state->busy, false);
    outcome = r_json_allocation_result(r_runtime_allocator_allocate(
        allocator, transport.handle_type.size, transport.handle_type.alignment, &state->handle));
    if (outcome.status == R_STD_JSON_CALL_SUCCESS)
        outcome =
            r_json_allocation_result(r_runtime_allocator_allocate(allocator,
                                                                  transport.read_type.size,
                                                                  transport.read_type.alignment,
                                                                  &state->read_storage));
    if (outcome.status == R_STD_JSON_CALL_SUCCESS) {
        outcome = r_json_array_result(r_runtime_array_with_capacity(
            &state->buffer,
            allocator,
            (RRuntimeTypeInfo){sizeof(uint8_t), alignof(uint8_t), NULL, NULL},
            4096U));
        state->buffer_live = true;
    }
    if (outcome.status == R_STD_JSON_CALL_SUCCESS)
        outcome = r_json_decoder_initialize(
            &state->decoder,
            allocator,
            options,
            (RRuntimeTypeInfo){sizeof(RStdJsonValue), alignof(RStdJsonValue), NULL, NULL},
            r_json_decode_value_create);
    if (outcome.status != R_STD_JSON_CALL_SUCCESS) {
        r_json_reader_release(state);
        return outcome;
    }
    (void)memset(state->buffer.data, 0, state->buffer.capacity);
    state->buffer.length = state->buffer.capacity;
    transport.handle_type.move_initialize(state->handle, handle);
    state->handle_live = true;
    reader->state = state;
    return outcome;
}

static void r_json_read_abandon(RJsonReadFrame *frame) {
    if (!frame->accepted || frame->done)
        return;
    frame->reader->poisoned = true;
    r_json_decoder_abort(&frame->reader->decoder);
    frame->done = true;
    atomic_store_explicit(&frame->reader->busy, false, memory_order_release);
}

static void r_json_read_drop(void *payload) {
    RJsonReadFrame *frame = payload;
    r_runtime_task_destroy(&frame->child);
    r_json_read_abandon(frame);
    r_json_reader_release(frame->reader);
}

static void r_json_read_initialize(void *payload, const void *context) {
    RJsonReadFrame *frame = payload;
    *frame = *(const RJsonReadFrame *)context;
    if (frame->reader != NULL) {
        (void)atomic_fetch_add_explicit(&frame->reader->references, 1U, memory_order_relaxed);
        bool expected = false;
        frame->accepted = atomic_compare_exchange_strong_explicit(
            &frame->reader->busy, &expected, true, memory_order_acquire, memory_order_relaxed);
    }
}

static RRuntimeTaskStepStatus
r_json_read_finish(RJsonReadFrame *frame, void *result, RStdJsonReadOutcome outcome) {
    frame->complete(result, frame->reader == NULL ? NULL : &frame->reader->decoder, outcome);
    if (frame->accepted) {
        if (outcome.status != R_STD_JSON_READ_VALUE && outcome.status != R_STD_JSON_READ_END) {
            frame->reader->poisoned = true;
            r_json_decoder_abort(&frame->reader->decoder);
        }
        frame->done = true;
        atomic_store_explicit(&frame->reader->busy, false, memory_order_release);
    }
    return R_RUNTIME_TASK_STEP_COMPLETED;
}

static RRuntimeTaskStepStatus
r_json_read_json_failure(RJsonReadFrame *frame, void *result, RStdJsonResult failure) {
    return r_json_read_finish(
        frame,
        result,
        (RStdJsonReadOutcome){.status = failure.status == R_STD_JSON_CALL_ALLOCATION_ERROR
                                            ? R_STD_JSON_READ_ALLOCATION_ERROR
                                            : R_STD_JSON_READ_JSON_ERROR,
                              .json = failure});
}

static RRuntimeTaskStepStatus
r_json_read_step(RRuntimeTaskExecution *execution, void *payload, void *result) {
    RJsonReadFrame *frame = payload;
    struct RJsonReaderState *state = frame->reader;
    if (!frame->accepted || state->poisoned || state->detached)
        return r_json_read_json_failure(
            frame, result, r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U));
    if (!frame->started) {
        RStdJsonResult rebound =
            r_json_decoder_rebind(&state->decoder, frame->value_type, frame->create);
        if (rebound.status != R_STD_JSON_CALL_SUCCESS)
            return r_json_read_json_failure(frame, result, rebound);
        frame->started = true;
    }
    for (;;) {
        if (frame->child != NULL) {
            RRuntimeTaskExecutionAwaitStatus awaited =
                r_runtime_task_execution_await(execution, &frame->child, state->read_storage);
            if (awaited == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED)
                return R_RUNTIME_TASK_STEP_SUSPENDED;
            if (awaited != R_RUNTIME_TASK_EXECUTION_AWAIT_OK) {
                r_json_read_abandon(frame);
                return R_RUNTIME_TASK_STEP_CANCELLED;
            }
            RStdJsonTransportRead read = {0};
            state->transport.take(state->read_storage, &read);
            state->buffer = read.buffer;
            state->buffer_live = true;
            state->offset = 0U;
            state->length = read.count;
            state->eof = read.end;
            if (read.failed)
                return r_json_read_finish(
                    frame,
                    result,
                    (RStdJsonReadOutcome){.status = R_STD_JSON_READ_TRANSPORT_ERROR,
                                          .transport_code = read.code,
                                          .native_code = read.native_code});
            if (state->length > state->buffer.length || (!state->eof && state->length == 0U))
                return r_json_read_json_failure(
                    frame, result, r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U));
        }
        if (r_runtime_task_execution_cancel_requested(execution)) {
            r_json_read_abandon(frame);
            return R_RUNTIME_TASK_STEP_CANCELLED;
        }
        RStdJsonReadOutcome timeout = {0};
        if (state->transport.deadline != NULL &&
            !state->transport.deadline(frame->deadline, &timeout))
            return r_json_read_finish(frame, result, timeout);
        for (size_t i = state->offset; state->only_whitespace && i < state->length; ++i) {
            uint8_t byte = ((const uint8_t *)state->buffer.data)[i];
            state->only_whitespace = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
        }
        if (state->eof && state->only_whitespace)
            return r_json_read_finish(
                frame, result, (RStdJsonReadOutcome){.status = R_STD_JSON_READ_END});
        RStdJsonFeedResult fed = r_json_decoder_feed(
            &state->decoder,
            (RStdJsonByteView){(const uint8_t *)state->buffer.data + state->offset,
                               state->length - state->offset},
            state->eof);
        state->offset += fed.consumed;
        if (fed.outcome.status != R_STD_JSON_CALL_SUCCESS)
            return r_json_read_json_failure(frame, result, fed.outcome);
        if (fed.state == R_STD_JSON_FEED_VALUE_READY || fed.state == R_STD_JSON_FEED_END)
            return r_json_read_finish(
                frame,
                result,
                (RStdJsonReadOutcome){.status = fed.state == R_STD_JSON_FEED_VALUE_READY
                                                    ? R_STD_JSON_READ_VALUE
                                                    : R_STD_JSON_READ_END});
        if (state->eof || state->offset != state->length)
            return r_json_read_json_failure(
                frame, result, r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U));
        state->buffer.length = state->buffer.capacity;
        RStdJsonTaskStartResult start =
            state->transport.start(state->handle, &state->buffer, frame->deadline);
        if (!start.is_ok) {
            if (start.error == R_STD_ASYNC_START_RUNTIME_STOPPING) {
                r_json_read_abandon(frame);
                return R_RUNTIME_TASK_STEP_CANCELLED;
            }
            return r_json_read_json_failure(
                frame,
                result,
                (RStdJsonResult){.status = R_STD_JSON_CALL_ALLOCATION_ERROR,
                                 .allocation_error = R_STD_ALLOC_REFUSAL()});
        }
        frame->child = start.task;
        state->buffer_live = false;
    }
}

RStdJsonTaskStartResult r_json_reader_read_next(RStdJsonReader *reader,
                                                RStdJsonDeadline deadline,
                                                RRuntimeTypeInfo value_type,
                                                RStdJsonDecodeCreateFn create,
                                                RRuntimeTypeInfo result_type,
                                                RStdJsonReadCompleteFn complete) {
    RRuntimeTaskPrepareResult prepared = r_runtime_task_resumable_start_prepare(
        (RRuntimeTypeInfo){sizeof(RJsonReadFrame), alignof(RJsonReadFrame), NULL, r_json_read_drop},
        result_type,
        r_json_read_step);
    RRuntimeTaskStartStatus status = prepared.status;
    RRuntimeTask *task = NULL;
    if (status == R_RUNTIME_TASK_START_OK) {
        RJsonReadFrame staged = {.reader = reader->state,
                                 .value_type = value_type,
                                 .create = create,
                                 .complete = complete,
                                 .deadline = deadline};
        RRuntimeTaskStartResult committed = r_runtime_task_start_commit_initialize(
            &prepared.transaction, r_json_read_initialize, &staged);
        status = committed.status;
        task = committed.task;
    }
    return (RStdJsonTaskStartResult){.is_ok = status == R_RUNTIME_TASK_START_OK,
                                     .task = task,
                                     .error = status == R_RUNTIME_TASK_START_ALLOCATION_FAILED
                                                  ? R_STD_ASYNC_START_REFUSAL()
                                                  : R_STD_ASYNC_START_RUNTIME_STOPPING};
}

RStdJsonResult r_json_reader_detach(RStdJsonReader *reader, RStdJsonDetached *detached) {
    struct RJsonReaderState *state = reader->state;
    bool expected = false;
    if (state == NULL ||
        !atomic_compare_exchange_strong_explicit(
            &state->busy, &expected, true, memory_order_acquire, memory_order_relaxed))
        return r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
    state->detached = true;
    r_json_decoder_destroy(&state->decoder);
    const size_t remaining = state->length - state->offset;
    if (remaining != 0U)
        (void)memmove(
            state->buffer.data, (const uint8_t *)state->buffer.data + state->offset, remaining);
    state->buffer.length = remaining;
    state->offset = 0U;
    state->length = remaining;
    *detached = (RStdJsonDetached){state};
    *reader = (RStdJsonReader){0};
    atomic_store_explicit(&state->busy, false, memory_order_release);
    return (RStdJsonResult){0};
}

RStdJsonResult r_json_detached_take_handle(RStdJsonDetached *detached, void *output) {
    struct RJsonReaderState *state = detached->state;
    if (state == NULL || !state->handle_live || output == NULL)
        return r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
    state->transport.handle_type.move_initialize(output, state->handle);
    state->handle_live = false;
    return (RStdJsonResult){0};
}

RStdJsonResult r_json_detached_take_bytes(RStdJsonDetached *detached, RRuntimeArray *output) {
    struct RJsonReaderState *state = detached->state;
    if (state == NULL || !state->buffer_live || output == NULL)
        return r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
    *output = state->buffer;
    state->buffer = (RRuntimeArray){0};
    state->buffer_live = false;
    return (RStdJsonResult){0};
}

void r_json_reader_destroy(RStdJsonReader *reader) {
    r_json_reader_release(reader->state);
    *reader = (RStdJsonReader){0};
}
void r_json_detached_destroy(RStdJsonDetached *detached) {
    r_json_reader_release(detached->state);
    *detached = (RStdJsonDetached){0};
}
