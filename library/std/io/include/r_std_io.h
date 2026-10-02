#ifndef R_STD_IO_H
#define R_STD_IO_H

#include "r_runtime_arc.h"
#include "r_runtime_array.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"
#include "r_std_async.h"
#include "r_std_error_types.h"
#include "r_std_time.h"

#include <stddef.h>
#include <stdint.h>

typedef enum RStdIoErrorCode {
    R_STD_IO_ERROR_CLOSED = 0,
    R_STD_IO_ERROR_BROKEN_PIPE = 1,
    R_STD_IO_ERROR_PERMISSION_DENIED = 2,
    R_STD_IO_ERROR_RESOURCE_EXHAUSTED = 3,
    R_STD_IO_ERROR_INVALID_OPERATION = 4,
    R_STD_IO_ERROR_CANCELLED = 5,
    R_STD_IO_ERROR_TIMED_OUT = 6,
    R_STD_IO_ERROR_OTHER = 7
} RStdIoErrorCode;

typedef struct RStdIoError {
    RStdIoErrorCode code;
    int64_t native_code;
} RStdIoError;

/*
 * Move-only Send+Sync runtime views. Generated C17 Move/drop glue uses the inline ABI below;
 * values shall never be byte-copied. Each initialized value owns exactly one runtime view.
 */
typedef struct RStdIoInput {
    RRuntimeDarwinIoHandle *handle;
} RStdIoInput;

typedef struct RStdIoOutput {
    RRuntimeDarwinIoHandle *handle;
} RStdIoOutput;

static inline void r_std_io_input_move_initialize(RStdIoInput *destination, RStdIoInput *source) {
    destination->handle = source->handle;
    source->handle = NULL;
}

static inline void r_std_io_input_destroy(RStdIoInput *value) {
    r_runtime_darwin_io_handle_release(value->handle);
    value->handle = NULL;
}

static inline void r_std_io_output_move_initialize(RStdIoOutput *destination,
                                                   RStdIoOutput *source) {
    destination->handle = source->handle;
    source->handle = NULL;
}

static inline void r_std_io_output_destroy(RStdIoOutput *value) {
    r_runtime_darwin_io_handle_release(value->handle);
    value->handle = NULL;
}

typedef enum RStdIoReadResultKind {
    R_STD_IO_READ_RESULT_READ = 0,
    R_STD_IO_READ_RESULT_END = 1,
    R_STD_IO_READ_RESULT_FAILED = 2
} RStdIoReadResultKind;

typedef struct RStdIoReadResult {
    RStdIoReadResultKind kind;
    RRuntimeArray buffer;
    size_t count;
    RStdIoError error;
} RStdIoReadResult;

typedef enum RStdIoWriteResultKind {
    R_STD_IO_WRITE_RESULT_WRITTEN = 0,
    R_STD_IO_WRITE_RESULT_FAILED = 1
} RStdIoWriteResultKind;

typedef struct RStdIoWriteResult {
    RStdIoWriteResultKind kind;
    RRuntimeArray buffer;
    size_t count;
    RStdIoError error;
} RStdIoWriteResult;

typedef enum RStdIoWriteAllResultKind {
    R_STD_IO_WRITE_ALL_RESULT_WRITTEN = 0,
    R_STD_IO_WRITE_ALL_RESULT_FAILED = 1
} RStdIoWriteAllResultKind;

typedef struct RStdIoWriteAllResult {
    RStdIoWriteAllResultKind kind;
    RRuntimeArray buffer;
    size_t written;
    RStdIoError error;
} RStdIoWriteAllResult;

typedef enum RStdIoSharedWriteResultKind {
    R_STD_IO_SHARED_WRITE_RESULT_WRITTEN = 0,
    R_STD_IO_SHARED_WRITE_RESULT_FAILED = 1
} RStdIoSharedWriteResultKind;

typedef struct RStdIoSharedWriteResult {
    RStdIoSharedWriteResultKind kind;
    RRuntimeArc buffer;
    size_t written;
    RStdIoError error;
} RStdIoSharedWriteResult;

typedef struct RStdIoDeadline {
    _Bool has_value;
    RStdTimeInstant value;
} RStdIoDeadline;

/* Canonical hidden checked carrier: r_tag is 0 for success and 1 for std.io::io_error. */
typedef struct RStdIoVoidResult {
    uint32_t r_tag;
    union {
        RStdIoError r_err;
    } r_payload;
} RStdIoVoidResult;

typedef struct RStdIoTaskStartResult {
    _Bool is_ok;
    RRuntimeTask *task;
    RStdAsyncStartError error;
} RStdIoTaskStartResult;

/*
 * Borrowed byte views for the scoped operations. The caller keeps the storage alive and
 * unmoved until the completion result is published; the library never frees or retains it.
 */
typedef struct RStdIoMutableBytes {
    uint8_t *data;
    size_t length;
} RStdIoMutableBytes;

typedef struct RStdIoConstBytes {
    const uint8_t *data;
    size_t length;
} RStdIoConstBytes;

/* Canonical hidden checked carrier: r_tag 0 carries a byte count, 1 carries std.io::io_error. */
typedef struct RStdIoCountResult {
    uint32_t r_tag;
    union {
        size_t r_value;
        RStdIoError r_error_00000001;
    } r_payload;
} RStdIoCountResult;

/*
 * Allocation-free process-console constructors. Calling outside an active hosted runtime
 * lifecycle is an internal contract violation because the normative operations have no failure
 * result. Every successful call returns one independent Move-only view.
 */
RStdIoInput r_std_io_stdin(void);
RStdIoOutput r_std_io_stdout(void);
RStdIoOutput r_std_io_stderr(void);

/*
 * Ownership: success consumes buffer into an eager task. Start failure leaves the complete array
 * descriptor and every byte unchanged. The borrowed stream is retained during native reservation
 * and need not outlive a successful call.
 */
RStdIoTaskStartResult
r_std_io_read(const RStdIoInput *stream, RRuntimeArray *buffer, RStdIoDeadline deadline);
RStdIoTaskStartResult
r_std_io_write(const RStdIoOutput *stream, RRuntimeArray *buffer, RStdIoDeadline deadline);
RStdIoTaskStartResult
r_std_io_write_all(const RStdIoOutput *stream, RRuntimeArray *buffer, RStdIoDeadline deadline);

/*
 * Scoped forms borrow caller storage instead of consuming an owner. target/source stay borrowed,
 * unmoved and unfreed until the completion result is published (after native acknowledgement);
 * start failure leaves them untouched. read_into completes with RStdIoCountResult carrying the
 * bytes placed at the start of target (0 = end of stream or empty target). write_from carries one
 * positive native prefix count; write_all_from completes with RStdIoVoidResult only after every
 * byte or a terminal error. Zero-length views complete successfully without native submission.
 * Failed outcomes carry only the error and never report partial progress.
 */
RStdIoTaskStartResult
r_std_io_read_into(const RStdIoInput *stream, RStdIoMutableBytes target, RStdIoDeadline deadline);
RStdIoTaskStartResult
r_std_io_write_from(const RStdIoOutput *stream, RStdIoConstBytes source, RStdIoDeadline deadline);
RStdIoTaskStartResult r_std_io_write_all_from(const RStdIoOutput *stream,
                                              RStdIoConstBytes source,
                                              RStdIoDeadline deadline);

/*
 * Ownership: success consumes the strong array owner into the eager task. Start failure leaves
 * the owner unchanged. offset and length select an immutable range with write-all semantics.
 */
RStdIoTaskStartResult r_std_io_write_shared(const RStdIoOutput *stream,
                                            RRuntimeArc *buffer,
                                            size_t offset,
                                            size_t length,
                                            RStdIoDeadline deadline);

/* The borrowed output identity is independently retained before successful task publication. */
RStdIoTaskStartResult r_std_io_flush(const RStdIoOutput *stream, RStdIoDeadline deadline);

/*
 * Ownership: successful task start consumes the named view; start failure leaves it byte-for-byte
 * unchanged. Every terminal task path cancels earlier identity operations, waits for native
 * acknowledgement, and releases the consumed view exactly once. Cleanup is never skipped by an
 * expired deadline or by task cancellation.
 */
RStdIoTaskStartResult r_std_io_close_input(RStdIoInput *stream, RStdIoDeadline deadline);
RStdIoTaskStartResult r_std_io_close_output(RStdIoOutput *stream, RStdIoDeadline deadline);

/* Exact, allocation-free conversion preserving the captured native status. */
RStdError r_std_io_as_error(RStdIoError value);

/* Private compiler drop/move ABI used by the inline typed glue below. */
void r_library_internal_io_read_result_move(RStdIoReadResult *destination,
                                            RStdIoReadResult *source);
void r_library_internal_io_read_result_drop(RStdIoReadResult *result);
void r_library_internal_io_write_result_move(RStdIoWriteResult *destination,
                                             RStdIoWriteResult *source);
void r_library_internal_io_write_result_drop(RStdIoWriteResult *result);
void r_library_internal_io_write_all_result_move(RStdIoWriteAllResult *destination,
                                                 RStdIoWriteAllResult *source);
void r_library_internal_io_write_all_result_drop(RStdIoWriteAllResult *result);
void r_library_internal_io_shared_write_result_move(RStdIoSharedWriteResult *destination,
                                                    RStdIoSharedWriteResult *source);
void r_library_internal_io_shared_write_result_drop(RStdIoSharedWriteResult *result);

static inline void r_std_io_read_result_destroy(RStdIoReadResult *result) {
    r_library_internal_io_read_result_drop(result);
}

static inline void r_std_io_read_result_move_initialize(RStdIoReadResult *destination,
                                                        RStdIoReadResult *source) {
    r_library_internal_io_read_result_move(destination, source);
}

static inline void r_std_io_write_result_destroy(RStdIoWriteResult *result) {
    r_library_internal_io_write_result_drop(result);
}

static inline void r_std_io_write_result_move_initialize(RStdIoWriteResult *destination,
                                                         RStdIoWriteResult *source) {
    r_library_internal_io_write_result_move(destination, source);
}

static inline void r_std_io_write_all_result_destroy(RStdIoWriteAllResult *result) {
    r_library_internal_io_write_all_result_drop(result);
}

static inline void r_std_io_write_all_result_move_initialize(RStdIoWriteAllResult *destination,
                                                             RStdIoWriteAllResult *source) {
    r_library_internal_io_write_all_result_move(destination, source);
}

static inline void r_std_io_shared_write_result_destroy(RStdIoSharedWriteResult *result) {
    r_library_internal_io_shared_write_result_drop(result);
}

static inline void
r_std_io_shared_write_result_move_initialize(RStdIoSharedWriteResult *destination,
                                             RStdIoSharedWriteResult *source) {
    r_library_internal_io_shared_write_result_move(destination, source);
}

#endif
