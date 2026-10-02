#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_runtime_thread_attachment.h"
#include "r_runtime_utf8.h"

#if defined(__APPLE__)
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_darwin_process.h"
#endif

#include <errno.h>
#include <pthread.h>
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

static void *r_runtime_argument_snapshot_allocation;
static unsigned char *r_runtime_argument_snapshot;
static size_t r_runtime_argument_snapshot_length;
static size_t r_runtime_argument_snapshot_count;
static RRuntimeStringView *r_runtime_argument_views;
static _Atomic _Bool r_runtime_argument_snapshot_available;
static _Thread_local _Bool r_runtime_initial_thread_draining;
static RRuntimeAllocator r_runtime_hosted_program_allocator;

#if defined(R_RUNTIME_TESTING)
static _Bool r_runtime_testing_snapshot_allocation_failure;
static _Bool r_runtime_testing_executor_allocation_failure;

void r_runtime_testing_set_snapshot_allocation_failure(_Bool enabled) {
    r_runtime_testing_snapshot_allocation_failure = enabled;
}

void r_runtime_testing_set_executor_allocation_failure(_Bool enabled) {
    r_runtime_testing_executor_allocation_failure = enabled;
}
#endif

void r_runtime_emergency_write(const char *bytes, size_t length) {
    while (length != 0U) {
#if defined(_WIN32)
        const unsigned int requested = length > (size_t)UINT_MAX ? UINT_MAX : (unsigned int)length;
        const int written = _write(2, bytes, requested);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (written == 0) {
            return;
        }
        bytes += (size_t)written;
        length -= (size_t)written;
#else
        const ssize_t written = write(STDERR_FILENO, bytes, length);
        if (written < (ssize_t)0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (written == (ssize_t)0) {
            return;
        }
        bytes += (size_t)written;
        length -= (size_t)written;
#endif
    }
}

static void r_runtime_emergency_unsigned(uint64_t value) {
    char digits[20];
    size_t first = sizeof(digits);
    do {
        digits[--first] = (char)('0' + value % UINT64_C(10));
        value /= UINT64_C(10);
    } while (value != UINT64_C(0));
    r_runtime_emergency_write(digits + first, sizeof(digits) - first);
}

void r_runtime_report_main_error(const char *domain,
                                 size_t domain_length,
                                 const uint8_t *name,
                                 size_t name_length,
                                 uint32_t code,
                                 int64_t native_code) {
    static const char prefix[] = "R main error: domain=";
    r_runtime_emergency_write(prefix, sizeof(prefix) - 1U);
    r_runtime_emergency_write(domain, domain_length);
    r_runtime_emergency_write(" name=", 6U);
    r_runtime_emergency_write((const char *)name, name_length);
    r_runtime_emergency_write(" code=", 6U);
    r_runtime_emergency_unsigned(code);
    r_runtime_emergency_write(" native_code=", 13U);
    if (native_code < INT64_C(0)) {
        r_runtime_emergency_write("-", 1U);
        r_runtime_emergency_unsigned((uint64_t)(-(native_code + INT64_C(1))) + UINT64_C(1));
    } else {
        r_runtime_emergency_unsigned((uint64_t)native_code);
    }
    r_runtime_emergency_write("\n", 1U);
}

static void r_runtime_report_start_failure(int process_status) {
    static const char argument_encoding[] = "R startup failure: argument_encoding_failure\n";
    static const char allocation[] = "R startup failure: allocation_failure\n";
    static const char async_root[] = "R startup failure: async_root_start_failure\n";

    if (process_status == R_RUNTIME_ARGUMENT_ENCODING_STATUS) {
        r_runtime_emergency_write(argument_encoding, sizeof(argument_encoding) - 1U);
    } else if (process_status == R_RUNTIME_ALLOCATION_FAILURE_STATUS) {
        r_runtime_emergency_write(allocation, sizeof(allocation) - 1U);
    } else if (process_status == R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS) {
        r_runtime_emergency_write(async_root, sizeof(async_root) - 1U);
    }
}

static void *r_runtime_allocate_snapshot(size_t size) {
    void *allocation = NULL;
    RRuntimeAllocationStatus status;

    r_runtime_allocator_initialize(&r_runtime_hosted_program_allocator);
#if defined(R_RUNTIME_TESTING)
    if (r_runtime_testing_snapshot_allocation_failure) {
        r_runtime_allocator_set_failure(&r_runtime_hosted_program_allocator, UINT64_C(1));
    } else if (r_runtime_testing_executor_allocation_failure) {
        r_runtime_allocator_set_failure(&r_runtime_hosted_program_allocator, UINT64_C(2));
    }
#endif
    status = r_runtime_allocator_allocate(
        &r_runtime_hosted_program_allocator, size, _Alignof(RRuntimeStringView), &allocation);
    return status == R_RUNTIME_ALLOCATION_OK ? allocation : NULL;
}

static RRuntimeStartResult r_runtime_start_failure(int process_status) {
    RRuntimeStartResult result;
    r_runtime_report_start_failure(process_status);
    result.started = 0;
    result.process_status = process_status;
    return result;
}

static void r_runtime_release_argument_snapshot(void) {
    r_runtime_allocator_deallocate(r_runtime_argument_snapshot_allocation,
                                   _Alignof(RRuntimeStringView));
    r_runtime_argument_snapshot_allocation = NULL;
    r_runtime_argument_snapshot = NULL;
    r_runtime_argument_snapshot_length = 0U;
    r_runtime_argument_snapshot_count = 0U;
    r_runtime_argument_views = NULL;
}

RRuntimeStartResult r_runtime_hosted_start(int argc, char *argv[]) {
    static const unsigned char synthesized[] = "<program>";
    size_t total = 0U;
    size_t allocation_size;
    size_t argument_count;
    size_t views_size;
    int index;
    unsigned char *cursor;
    RRuntimeExecutorStartStatus executor_status;
#if defined(__APPLE__)
    RRuntimeDarwinFsServiceStartResult filesystem_status;
    RRuntimeDarwinIoConsoleStartResult console_status;
    RRuntimeDarwinProcessStartResult process_status;
#endif
    RRuntimeStartResult result;

#if defined(__APPLE__)
    if (!r_runtime_stack_initialize_current_thread()) {
        r_runtime_panic(R_RUNTIME_PANIC_STACK_EXHAUSTION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
#endif
    if ((argc < 0) || ((argc > 0) && (argv == NULL)) ||
        (r_runtime_argument_snapshot_allocation != NULL)) {
        return r_runtime_start_failure(R_RUNTIME_ARGUMENT_ENCODING_STATUS);
    }
    argument_count = argc == 0 ? 1U : (size_t)argc;
    if (argument_count > (SIZE_MAX / sizeof(*r_runtime_argument_views))) {
        return r_runtime_start_failure(R_RUNTIME_ALLOCATION_FAILURE_STATUS);
    }
    views_size = argument_count * sizeof(*r_runtime_argument_views);
    if (argc == 0) {
        total = sizeof(synthesized);
    } else {
        for (index = 0; index < argc; ++index) {
            if (argv[index] == NULL) {
                return r_runtime_start_failure(R_RUNTIME_ARGUMENT_ENCODING_STATUS);
            }
            if (!r_runtime_utf8_validate((const uint8_t *)argv[index], strlen(argv[index]), NULL)) {
                return r_runtime_start_failure(R_RUNTIME_ARGUMENT_ENCODING_STATUS);
            }
        }
        for (index = 0; index < argc; ++index) {
            const size_t length = strlen(argv[index]);
            if ((length == SIZE_MAX) || (total > (SIZE_MAX - length - 1U))) {
                return r_runtime_start_failure(R_RUNTIME_ALLOCATION_FAILURE_STATUS);
            }
            total += length + 1U;
        }
    }
    if (views_size > (SIZE_MAX - total)) {
        return r_runtime_start_failure(R_RUNTIME_ALLOCATION_FAILURE_STATUS);
    }
    allocation_size = views_size + total;
    r_runtime_argument_snapshot_allocation = r_runtime_allocate_snapshot(allocation_size);
    if (r_runtime_argument_snapshot_allocation == NULL) {
        return r_runtime_start_failure(R_RUNTIME_ALLOCATION_FAILURE_STATUS);
    }
    r_runtime_argument_views = r_runtime_argument_snapshot_allocation;
    r_runtime_argument_snapshot =
        (unsigned char *)r_runtime_argument_snapshot_allocation + views_size;
    cursor = r_runtime_argument_snapshot;
    if (argc == 0) {
        (void)memcpy(cursor, synthesized, sizeof(synthesized));
        r_runtime_argument_views[0].data = cursor;
        r_runtime_argument_views[0].length = sizeof(synthesized) - 1U;
    } else {
        for (index = 0; index < argc; ++index) {
            const size_t length = strlen(argv[index]) + 1U;
            (void)memcpy(cursor, argv[index], length);
            r_runtime_argument_views[(size_t)index].data = cursor;
            r_runtime_argument_views[(size_t)index].length = length - 1U;
            cursor += length;
        }
    }
    r_runtime_argument_snapshot_length = total;
    r_runtime_argument_snapshot_count = argument_count;
    if (!r_runtime_thread_lifecycle_start()) {
        r_runtime_release_argument_snapshot();
        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    executor_status = r_runtime_executor_lifecycle_start(&r_runtime_hosted_program_allocator);
    if (executor_status != R_RUNTIME_EXECUTOR_START_OK) {
        r_runtime_thread_lifecycle_stop();
        r_runtime_release_argument_snapshot();
        if (executor_status == R_RUNTIME_EXECUTOR_START_ALLOCATION_FAILED) {
            return r_runtime_start_failure(R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS);
        }
        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
#if defined(__APPLE__)
    /* The blocking call pool (Core R-TERM-0015) starts no thread before its first call. */
    if (!r_runtime_blocking_start(R_RUNTIME_BLOCKING_MAX_PENDING_CALLS)) {
        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    filesystem_status = r_runtime_darwin_fs_service_start(&r_runtime_hosted_program_allocator,
                                                          R_RUNTIME_DARWIN_FS_MAX_PENDING_REQUESTS);
    if (filesystem_status.status != R_RUNTIME_DARWIN_FS_START_OK) {
        if (!r_runtime_executor_lifecycle_stop()) {
            r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                            (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
        }
        r_runtime_blocking_stop();
        r_runtime_thread_lifecycle_stop();
        r_runtime_release_argument_snapshot();
        return r_runtime_start_failure(R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS);
    }
    console_status = r_runtime_darwin_io_process_console_start(&r_runtime_hosted_program_allocator);
    if (console_status.status != R_RUNTIME_DARWIN_IO_START_OK) {
        r_runtime_darwin_fs_service_stop();
        if (!r_runtime_executor_lifecycle_stop()) {
            r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                            (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
        }
        r_runtime_blocking_stop();
        r_runtime_thread_lifecycle_stop();
        r_runtime_release_argument_snapshot();
        return r_runtime_start_failure(R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS);
    }
    process_status = r_runtime_darwin_process_lifecycle_start(&r_runtime_hosted_program_allocator);
    if (process_status.status != R_RUNTIME_DARWIN_PROCESS_START_OK) {
        r_runtime_darwin_io_process_console_stop();
        r_runtime_darwin_fs_service_stop();
        if (!r_runtime_executor_lifecycle_stop()) {
            r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                            (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
        }
        r_runtime_blocking_stop();
        r_runtime_thread_lifecycle_stop();
        r_runtime_release_argument_snapshot();
        return r_runtime_start_failure(R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS);
    }
#endif
    atomic_store_explicit(&r_runtime_argument_snapshot_available, 1, memory_order_release);
    r_runtime_initial_thread_draining = 0;
    result.started = 1;
    result.process_status = 0;
    return result;
}

int r_runtime_hosted_finish(int32_t result) {
    _Static_assert(INT_MAX >= INT32_MAX, "hosted scalar runtime requires 32-bit int");
    _Static_assert(INT_MIN <= INT32_MIN, "hosted scalar runtime requires 32-bit int");
    if (!r_runtime_executor_lifecycle_stop()) {
        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
#if defined(__APPLE__)
    r_runtime_darwin_process_lifecycle_stop();
    r_runtime_darwin_io_process_console_stop();
    r_runtime_darwin_fs_service_stop();
    r_runtime_blocking_stop();
#endif
    r_runtime_thread_lifecycle_stop();
    r_runtime_thread_local_cleanup_install(NULL);
    atomic_store_explicit(&r_runtime_argument_snapshot_available, 0, memory_order_release);
    r_runtime_release_argument_snapshot();
    return (int)result;
}

int r_runtime_hosted_exit(int32_t result) {
    /* The initial thread outside any task finishes as for a returning entry point. Elsewhere a
       task or a joining task may wait on this thread, so the other tasks are drained instead. */
    if (!r_runtime_executor_on_worker() && (pthread_main_np() != 0)) {
        return r_runtime_hosted_finish(result);
    }
    if (!r_runtime_executor_quiesce_for_exit()) {
        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    /* As r_runtime_hosted_finish after the executor stop; the drained executor keeps its
       queue, since tasks suspended on the calling chain still reference it. */
#if defined(__APPLE__)
    r_runtime_darwin_process_lifecycle_stop();
    r_runtime_darwin_io_process_console_stop();
    r_runtime_darwin_fs_service_stop();
    r_runtime_blocking_stop();
#endif
    r_runtime_thread_lifecycle_stop();
    r_runtime_thread_local_cleanup_install(NULL);
    atomic_store_explicit(&r_runtime_argument_snapshot_available, 0, memory_order_release);
    r_runtime_release_argument_snapshot();
    return (int)result;
}

RRuntimeAllocator *r_runtime_hosted_allocator(void) {
    if (!atomic_load_explicit(&r_runtime_argument_snapshot_available, memory_order_acquire)) {
        return NULL;
    }
    return &r_runtime_hosted_program_allocator;
}

int r_runtime_hosted_async_root_start_failure(void) {
    r_runtime_report_start_failure(R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS);
    return r_runtime_hosted_finish(INT32_C(R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS));
}

_Bool r_runtime_hosted_argument_snapshot(RRuntimeArgumentSnapshotView *view) {
    if (!atomic_load_explicit(&r_runtime_argument_snapshot_available, memory_order_acquire)) {
        return 0;
    }
    view->data = r_runtime_argument_snapshot;
    view->byte_length = r_runtime_argument_snapshot_length;
    view->count = r_runtime_argument_snapshot_count;
    view->arguments = r_runtime_argument_views;
    return 1;
}

void r_runtime_hosted_drain(void) {
    r_runtime_initial_thread_draining = 1;
    r_runtime_executor_cancel_pending();
    r_runtime_hosted_work_drain();
}

void r_runtime_hosted_after_object_drop(void) {
    if (r_runtime_initial_thread_draining)
        r_runtime_hosted_drain();
}
