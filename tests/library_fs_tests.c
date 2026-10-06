#include "r_std_fs.h"

#include "r_library_fs_internal.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_task.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestFsThreadContext {
    const RStdFsPath *path;
    _Atomic int failed;
} RTestFsThreadContext;

typedef struct RTestFsSeekStartContext {
    const RStdFsFile *file;
    RStdFsSeekOrigin origin;
    int64_t offset;
    RStdFsDeadline deadline;
    RStdFsTaskStartResult started;
} RTestFsSeekStartContext;

typedef struct RTestFsCloseStartContext {
    RStdFsFile *file;
    RStdFsDeadline deadline;
    RStdFsTaskStartResult started;
} RTestFsCloseStartContext;

typedef struct RTestFsExecutorStopContext {
    _Bool stopped;
} RTestFsExecutorStopContext;

typedef struct RTestFsPositionLedger {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    _Atomic int activation_count;
    _Atomic int cancellation_count;
    _Atomic int cancellation_active;
    _Atomic int cancellation_reason;
    _Atomic int native_submission_count;
    _Atomic int drained;
    _Atomic int destroyed;
    _Atomic uint64_t observed_position;
    _Bool activation_entered;
    _Bool activation_may_commit;
} RTestFsPositionLedger;

typedef struct RTestFsPositionControl {
    _Atomic size_t references;
    RLibraryFsHandleStorage *storage;
    RLibraryFsPositionNode node;
    RLibraryFsOperationRegistration registration;
    RTestFsPositionLedger *ledger;
    _Bool pause_before_commit;
    _Bool finish_after_commit;
    _Bool unregister_on_cancel;
} RTestFsPositionControl;

static int r_test_fs_move_initialize_overwrites_uninitialized_storage(void) {
    unsigned char directory_token;
    unsigned char file_token;
    unsigned char iterator_token;
    unsigned char entry_token;
    unsigned char next_iterator_token;
    unsigned char next_entry_token;
    RStdFsDirectory directory_destination;
    RStdFsDirectory directory_source = {
        (RStdFsDirectoryStorage *)(void *)&directory_token,
    };
    RStdFsFile file_destination;
    RStdFsFile file_source = {
        (RStdFsFileStorage *)(void *)&file_token,
    };
    RStdFsDirectoryIter iterator_destination;
    RStdFsDirectoryIter iterator_source = {
        (RStdFsDirectoryIterStorage *)(void *)&iterator_token,
    };
    RStdFsDirectoryEntry entry_destination;
    RStdFsDirectoryEntry entry_source = {
        {(RStdFsPathStorage *)(void *)&entry_token},
        R_STD_FS_FILE_KIND_REGULAR,
    };
    RStdFsDirectoryNextResult next_destination;
    RStdFsDirectoryNextResult next_source = {
        R_STD_FS_DIRECTORY_NEXT_ENTRY,
        {0},
        {(RStdFsDirectoryIterStorage *)(void *)&next_iterator_token},
        {{(RStdFsPathStorage *)(void *)&next_entry_token}, R_STD_FS_FILE_KIND_DIRECTORY},
    };

    (void)memset(&directory_destination, 0xbe, sizeof(directory_destination));
    r_library_internal_fs_directory_move(&directory_destination, &directory_source);
    R_TEST_CHECK(directory_destination.storage ==
                 (RStdFsDirectoryStorage *)(void *)&directory_token);
    R_TEST_CHECK(directory_source.storage == NULL);

    (void)memset(&file_destination, 0xbe, sizeof(file_destination));
    r_library_internal_fs_file_move(&file_destination, &file_source);
    R_TEST_CHECK(file_destination.storage == (RStdFsFileStorage *)(void *)&file_token);
    R_TEST_CHECK(file_source.storage == NULL);

    (void)memset(&iterator_destination, 0xbe, sizeof(iterator_destination));
    r_library_internal_fs_directory_iter_move(&iterator_destination, &iterator_source);
    R_TEST_CHECK(iterator_destination.storage ==
                 (RStdFsDirectoryIterStorage *)(void *)&iterator_token);
    R_TEST_CHECK(iterator_source.storage == NULL);

    (void)memset(&entry_destination, 0xbe, sizeof(entry_destination));
    r_library_internal_fs_directory_entry_move(&entry_destination, &entry_source);
    R_TEST_CHECK(entry_destination.name.storage == (RStdFsPathStorage *)(void *)&entry_token);
    R_TEST_CHECK(entry_destination.kind == R_STD_FS_FILE_KIND_REGULAR);
    R_TEST_CHECK(entry_source.name.storage == NULL);

    (void)memset(&next_destination, 0xbe, sizeof(next_destination));
    r_library_internal_fs_directory_next_result_move(&next_destination, &next_source);
    R_TEST_CHECK(next_destination.kind == R_STD_FS_DIRECTORY_NEXT_ENTRY);
    R_TEST_CHECK(next_destination.iterator.storage ==
                 (RStdFsDirectoryIterStorage *)(void *)&next_iterator_token);
    R_TEST_CHECK(next_destination.entry.name.storage ==
                 (RStdFsPathStorage *)(void *)&next_entry_token);
    R_TEST_CHECK(next_destination.entry.kind == R_STD_FS_FILE_KIND_DIRECTORY);
    R_TEST_CHECK(next_source.iterator.storage == NULL);
    R_TEST_CHECK(next_source.entry.name.storage == NULL);
    return 0;
}

static RStdStringView r_test_fs_view(const char *text) {
    return (RStdStringView){(const uint8_t *)text, strlen(text)};
}

static RRuntimeDarwinIoHandle *r_test_fs_publish_file(RRuntimeAllocator *allocator,
                                                      RStdFsFileStorage *storage,
                                                      int descriptor,
                                                      RStdFsOpenFileOptions options) {
    RRuntimeDarwinIoHandleCreateResult io_created =
        r_runtime_darwin_io_handle_create(allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);

    if (io_created.status != R_RUNTIME_DARWIN_IO_START_OK || io_created.handle == NULL) {
        return NULL;
    }
    r_library_internal_fs_file_publish(storage, descriptor, options, io_created.handle);
    return io_created.handle;
}

static _Bool r_test_fs_sleep_nanoseconds(long nanoseconds) {
    struct timespec requested = {0, nanoseconds};
    struct timespec remaining;

    while (nanosleep(&requested, &remaining) != 0) {
        if (errno != EINTR) {
            return 0;
        }
        requested = remaining;
    }
    return 1;
}

static void *r_test_fs_seek_start_thread(void *context) {
    RTestFsSeekStartContext *start = context;

    start->started = r_std_fs_seek(start->file, start->origin, start->offset, start->deadline);
    return NULL;
}

static void *r_test_fs_close_start_thread(void *context) {
    RTestFsCloseStartContext *start = context;

    start->started = r_std_fs_close_file(start->file, start->deadline);
    return NULL;
}

static void *r_test_fs_executor_stop_thread(void *context) {
    RTestFsExecutorStopContext *stop = context;

    stop->stopped = r_runtime_executor_lifecycle_stop();
    return NULL;
}

static void
r_test_fs_empty_task_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    (void)execution;
    (void)payload;
    (void)result;
}

static _Bool r_test_fs_wait_executor_stopping(void) {
    const RRuntimeTypeInfo empty_type = {0U, 1U, NULL, NULL};
    size_t spin;

    for (spin = 0U; spin < 1000000U; ++spin) {
        RRuntimeTaskPrepareResult preparation =
            r_runtime_task_start_prepare(empty_type, empty_type, r_test_fs_empty_task_body);

        if (preparation.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING) {
            return 1;
        }
        if (preparation.status != R_RUNTIME_TASK_START_OK || preparation.transaction == NULL) {
            return 0;
        }
        r_runtime_task_start_abort(&preparation.transaction);
        (void)sched_yield();
    }
    return 0;
}

static void r_test_fs_position_retain(void *context) {
    RTestFsPositionControl *control = context;
    size_t current = atomic_load_explicit(&control->references, memory_order_relaxed);

    for (;;) {
        if (current == SIZE_MAX) {
            abort();
        }
        if (atomic_compare_exchange_weak_explicit(&control->references,
                                                  &current,
                                                  current + 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static void r_test_fs_position_release(void *context) {
    RTestFsPositionControl *control = context;
    size_t previous = atomic_fetch_sub_explicit(&control->references, 1U, memory_order_acq_rel);

    if (previous == 0U) {
        abort();
    }
    if (previous == 1U) {
        atomic_store_explicit(&control->ledger->destroyed, 1, memory_order_release);
        free(control);
    }
}

static void r_test_fs_position_activate(void *context, uint64_t position) {
    RTestFsPositionControl *control = context;
    RTestFsPositionLedger *ledger = control->ledger;

    (void)atomic_fetch_add_explicit(&ledger->activation_count, 1, memory_order_relaxed);
    atomic_store_explicit(&ledger->observed_position, position, memory_order_relaxed);
    if (control->pause_before_commit) {
        if (pthread_mutex_lock(&ledger->mutex) != 0) {
            abort();
        }
        ledger->activation_entered = 1;
        if (pthread_cond_broadcast(&ledger->condition) != 0) {
            abort();
        }
        while (!ledger->activation_may_commit) {
            if (pthread_cond_wait(&ledger->condition, &ledger->mutex) != 0) {
                abort();
            }
        }
        if (pthread_mutex_unlock(&ledger->mutex) != 0) {
            abort();
        }
    }
    if (!r_library_internal_fs_position_activation_begin(&control->node)) {
        return;
    }
    (void)atomic_fetch_add_explicit(&ledger->native_submission_count, 1, memory_order_relaxed);
    r_library_internal_fs_position_activation_commit(&control->node);
    if (control->finish_after_commit) {
        r_library_internal_fs_position_finish(
            &control->node, R_LIBRARY_FS_POSITION_KEEP, UINT64_C(0));
        r_library_internal_fs_operation_unregister(control->storage, &control->registration);
    }
}

static void
r_test_fs_position_cancel(void *context, RLibraryFsPositionCancelReason reason, _Bool active) {
    RTestFsPositionControl *control = context;

    (void)atomic_fetch_add_explicit(&control->ledger->cancellation_count, 1, memory_order_relaxed);
    atomic_store_explicit(&control->ledger->cancellation_reason, (int)reason, memory_order_relaxed);
    atomic_store_explicit(&control->ledger->cancellation_active, active, memory_order_relaxed);
    if (active) {
        r_library_internal_fs_position_finish(
            &control->node, R_LIBRARY_FS_POSITION_KEEP, UINT64_C(0));
    }
    if (control->unregister_on_cancel) {
        r_library_internal_fs_operation_unregister(control->storage, &control->registration);
    }
}

static void r_test_fs_position_close_cancel(void *context) {
    RTestFsPositionControl *control = context;

    r_library_internal_fs_position_cancel(&control->node, R_LIBRARY_FS_POSITION_CANCEL_CLOSE);
}

static void r_test_fs_position_drained(void *context) {
    RTestFsPositionLedger *ledger = context;

    atomic_store_explicit(&ledger->drained, 1, memory_order_release);
}

static void *r_test_fs_position_publish_thread(void *context) {
    RTestFsPositionControl *control = context;

    r_library_internal_fs_position_publish(&control->node);
    return NULL;
}

static void r_test_fs_position_ledger_initialize(RTestFsPositionLedger *ledger) {
    (void)memset(ledger, 0, sizeof(*ledger));
    if (pthread_mutex_init(&ledger->mutex, NULL) != 0 ||
        pthread_cond_init(&ledger->condition, NULL) != 0) {
        abort();
    }
    atomic_init(&ledger->activation_count, 0);
    atomic_init(&ledger->cancellation_count, 0);
    atomic_init(&ledger->cancellation_active, 0);
    atomic_init(&ledger->cancellation_reason, -1);
    atomic_init(&ledger->native_submission_count, 0);
    atomic_init(&ledger->drained, 0);
    atomic_init(&ledger->destroyed, 0);
    atomic_init(&ledger->observed_position, UINT64_C(0));
}

static void r_test_fs_position_ledger_destroy(RTestFsPositionLedger *ledger) {
    if (pthread_cond_destroy(&ledger->condition) != 0 ||
        pthread_mutex_destroy(&ledger->mutex) != 0) {
        abort();
    }
}

static RTestFsPositionControl *r_test_fs_position_control_create(RTestFsPositionLedger *ledger,
                                                                 RLibraryFsHandleStorage *storage,
                                                                 _Bool pause_before_commit,
                                                                 _Bool finish_after_commit,
                                                                 _Bool unregister_on_cancel) {
    RTestFsPositionControl *control = malloc(sizeof(*control));

    if (control == NULL) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    atomic_init(&control->references, 1U);
    control->storage = storage;
    control->ledger = ledger;
    control->pause_before_commit = pause_before_commit;
    control->finish_after_commit = finish_after_commit;
    control->unregister_on_cancel = unregister_on_cancel;
    return control;
}

static _Bool r_test_fs_position_reserve(RTestFsPositionControl *control, int expected_descriptor) {
    RStdFsAccess access;
    int descriptor;
    _Bool append;

    if (!r_library_internal_fs_position_reserve(control->storage,
                                                &control->node,
                                                &control->registration,
                                                r_test_fs_position_activate,
                                                r_test_fs_position_cancel,
                                                control,
                                                r_test_fs_position_close_cancel,
                                                r_test_fs_position_retain,
                                                r_test_fs_position_release,
                                                &descriptor,
                                                &access,
                                                &append)) {
        return 0;
    }
    return descriptor == expected_descriptor && access == R_STD_FS_ACCESS_READ_WRITE && !append;
}

static int r_test_fs_position_activation_close_race(void) {
    RRuntimeAllocator allocator;
    RStdFsFileStorage *file_storage;
    RTestFsPositionLedger ledger;
    RTestFsPositionControl *control;
    RStdFsOpenFileOptions options = {
        R_STD_FS_ACCESS_READ_WRITE,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        0,
    };
    pthread_t thread;
    RRuntimeDarwinIoHandle *payload_io;
    int descriptor;
    int retained_descriptor;
    _Bool already_drained;

    r_test_fs_position_ledger_initialize(&ledger);
    r_runtime_allocator_initialize(&allocator);
    file_storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(file_storage != NULL);
    descriptor = open("/dev/null", O_RDWR | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    payload_io = r_test_fs_publish_file(&allocator, file_storage, descriptor, options);
    R_TEST_CHECK(payload_io != NULL);

    control = r_test_fs_position_control_create(&ledger, &file_storage->handle, 1, 0, 1);
    R_TEST_CHECK(control != NULL);
    R_TEST_CHECK(r_test_fs_position_reserve(control, descriptor));
    r_test_fs_position_release(control);
    R_TEST_CHECK(pthread_create(&thread, NULL, r_test_fs_position_publish_thread, control) == 0);
    R_TEST_CHECK(pthread_mutex_lock(&ledger.mutex) == 0);
    while (!ledger.activation_entered) {
        R_TEST_CHECK(pthread_cond_wait(&ledger.condition, &ledger.mutex) == 0);
    }
    R_TEST_CHECK(pthread_mutex_unlock(&ledger.mutex) == 0);

    R_TEST_CHECK(r_library_internal_fs_handle_reserve_close(
        &file_storage->handle, &retained_descriptor, &payload_io));
    R_TEST_CHECK(payload_io != NULL && retained_descriptor == descriptor);
    R_TEST_CHECK(r_library_internal_fs_handle_begin_close(&file_storage->handle,
                                                          r_test_fs_position_drained,
                                                          &ledger,
                                                          &retained_descriptor,
                                                          &payload_io,
                                                          &already_drained));
    R_TEST_CHECK(payload_io != NULL);
    R_TEST_CHECK(!already_drained && retained_descriptor == descriptor);
    R_TEST_CHECK(atomic_load_explicit(&ledger.cancellation_count, memory_order_acquire) == 0);
    R_TEST_CHECK(atomic_load_explicit(&ledger.native_submission_count, memory_order_acquire) == 0);
    R_TEST_CHECK(atomic_load_explicit(&ledger.drained, memory_order_acquire) == 0);
    R_TEST_CHECK(atomic_load_explicit(&ledger.destroyed, memory_order_acquire) == 0);

    R_TEST_CHECK(pthread_mutex_lock(&ledger.mutex) == 0);
    ledger.activation_may_commit = 1;
    R_TEST_CHECK(pthread_cond_broadcast(&ledger.condition) == 0);
    R_TEST_CHECK(pthread_mutex_unlock(&ledger.mutex) == 0);
    R_TEST_CHECK(pthread_join(thread, NULL) == 0);
    R_TEST_CHECK(atomic_load_explicit(&ledger.activation_count, memory_order_acquire) == 1);
    R_TEST_CHECK(atomic_load_explicit(&ledger.cancellation_count, memory_order_acquire) == 1);
    R_TEST_CHECK(atomic_load_explicit(&ledger.cancellation_active, memory_order_acquire) == 0);
    R_TEST_CHECK(atomic_load_explicit(&ledger.cancellation_reason, memory_order_acquire) ==
                 R_LIBRARY_FS_POSITION_CANCEL_CLOSE);
    R_TEST_CHECK(atomic_load_explicit(&ledger.native_submission_count, memory_order_acquire) == 0);
    R_TEST_CHECK(atomic_load_explicit(&ledger.drained, memory_order_acquire) == 1);
    R_TEST_CHECK(atomic_load_explicit(&ledger.destroyed, memory_order_acquire) == 1);
    R_TEST_CHECK(close(retained_descriptor) == 0);
    r_runtime_darwin_io_handle_release(payload_io);
    r_library_internal_fs_file_storage_release(file_storage);
    r_test_fs_position_ledger_destroy(&ledger);
    return 0;
}

static int r_test_fs_position_early_unregister_rejected(void) {
    pid_t child = fork();
    int status;

    R_TEST_CHECK(child >= (pid_t)0);
    if (child == (pid_t)0) {
        RRuntimeAllocator allocator;
        RStdFsFileStorage *file_storage;
        RTestFsPositionLedger ledger;
        RTestFsPositionControl *control;
        RStdFsOpenFileOptions options = {
            R_STD_FS_ACCESS_READ_WRITE,
            R_STD_FS_CREATE_EXISTING,
            0,
            0,
            0,
        };
        int descriptor;

        r_test_fs_position_ledger_initialize(&ledger);
        r_runtime_allocator_initialize(&allocator);
        file_storage = r_library_internal_fs_file_reserve(&allocator);
        descriptor = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (file_storage == NULL || descriptor < 0) {
            _exit(2);
        }
        if (r_test_fs_publish_file(&allocator, file_storage, descriptor, options) == NULL) {
            _exit(2);
        }
        control = r_test_fs_position_control_create(&ledger, &file_storage->handle, 0, 0, 0);
        if (control == NULL || !r_test_fs_position_reserve(control, descriptor)) {
            _exit(2);
        }
        r_library_internal_fs_operation_unregister(control->storage, &control->registration);
        _exit(0);
    }
    R_TEST_CHECK(waitpid(child, &status, 0) == child);
    R_TEST_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    return 0;
}

static int r_test_fs_position_repeated_cancel_and_queue_progress(void) {
    RRuntimeAllocator allocator;
    RStdFsFileStorage *file_storage;
    RTestFsPositionLedger first_ledger;
    RTestFsPositionLedger second_ledger;
    RTestFsPositionControl *first;
    RTestFsPositionControl *second;
    RStdFsOpenFileOptions options = {
        R_STD_FS_ACCESS_READ_WRITE,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        0,
    };
    int descriptor;

    r_test_fs_position_ledger_initialize(&first_ledger);
    r_test_fs_position_ledger_initialize(&second_ledger);
    r_runtime_allocator_initialize(&allocator);
    file_storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(file_storage != NULL);
    descriptor = open("/dev/null", O_RDWR | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(r_test_fs_publish_file(&allocator, file_storage, descriptor, options) != NULL);
    first = r_test_fs_position_control_create(&first_ledger, &file_storage->handle, 0, 0, 0);
    second = r_test_fs_position_control_create(&second_ledger, &file_storage->handle, 0, 1, 1);
    R_TEST_CHECK(first != NULL && second != NULL);
    R_TEST_CHECK(r_test_fs_position_reserve(first, descriptor));
    R_TEST_CHECK(r_test_fs_position_reserve(second, descriptor));
    r_library_internal_fs_position_publish(&second->node);
    R_TEST_CHECK(atomic_load_explicit(&second_ledger.activation_count, memory_order_acquire) == 0);

    r_library_internal_fs_position_cancel(&first->node, R_LIBRARY_FS_POSITION_CANCEL_TASK);
    r_library_internal_fs_position_cancel(&first->node, R_LIBRARY_FS_POSITION_CANCEL_DEADLINE);
    r_library_internal_fs_position_cancel(&first->node, R_LIBRARY_FS_POSITION_CANCEL_CLOSE);
    r_library_internal_fs_position_abort(&first->node);
    R_TEST_CHECK(atomic_load_explicit(&first_ledger.cancellation_count, memory_order_acquire) == 1);
    R_TEST_CHECK(atomic_load_explicit(&first_ledger.cancellation_active, memory_order_acquire) ==
                 0);
    R_TEST_CHECK(atomic_load_explicit(&first_ledger.cancellation_reason, memory_order_acquire) ==
                 R_LIBRARY_FS_POSITION_CANCEL_TASK);
    R_TEST_CHECK(atomic_load_explicit(&second_ledger.activation_count, memory_order_acquire) == 1);
    R_TEST_CHECK(
        atomic_load_explicit(&second_ledger.native_submission_count, memory_order_acquire) == 1);
    R_TEST_CHECK(atomic_load_explicit(&second_ledger.observed_position, memory_order_acquire) ==
                 UINT64_C(0));

    r_library_internal_fs_operation_unregister(first->storage, &first->registration);
    r_test_fs_position_release(first);
    r_test_fs_position_release(second);
    R_TEST_CHECK(atomic_load_explicit(&first_ledger.destroyed, memory_order_acquire) == 1);
    R_TEST_CHECK(atomic_load_explicit(&second_ledger.destroyed, memory_order_acquire) == 1);
    r_library_internal_fs_file_storage_release(file_storage);
    r_test_fs_position_ledger_destroy(&first_ledger);
    r_test_fs_position_ledger_destroy(&second_ledger);
    return 0;
}

static _Bool r_test_fs_instant_before(RStdTimeInstant left, RStdTimeInstant right) {
    return (left.storage_seconds < right.storage_seconds) ||
           ((left.storage_seconds == right.storage_seconds) &&
            (left.storage_nanoseconds < right.storage_nanoseconds));
}

static int r_test_fs_path_equals(const RStdFsPath *path, const char *expected) {
    RStdFsStringResult converted = r_std_fs_path_to_utf8(path);
    RStdStringView view;
    size_t expected_length = strlen(expected);
    int equal;

    if (converted.status != R_STD_FS_CALL_SUCCESS) {
        return 0;
    }
    view = r_std_string_as_str(&converted.value);
    equal = (view.length == expected_length) &&
            ((expected_length == 0U) || (memcmp(view.data, expected, expected_length) == 0));
    r_runtime_string_destroy(&converted.value);
    return equal;
}

static int r_test_fs_validation_precedence(void) {
    static const uint8_t valid_with_nul[] = {UINT8_C('a'), UINT8_C(0), UINT8_C('b')};
    static const uint8_t invalid_before_nul[] = {UINT8_C(0xe2), UINT8_C(0)};
    static const uint8_t nul_before_invalid[] = {UINT8_C('a'), UINT8_C(0), UINT8_C(0xff)};
    static const uint8_t invalid_only[] = {UINT8_C('a'), UINT8_C(0xff)};
    RRuntimeAllocator allocator;
    RStdFsPathResult result;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    result = r_std_fs_path_from_utf8_bytes(
        &allocator, (RStdStringView){invalid_before_nul, sizeof(invalid_before_nul)});
    R_TEST_CHECK(result.status == R_STD_FS_CALL_ERROR);
    R_TEST_CHECK(result.error.kind == R_STD_FS_PATH_ERROR_INVALID_UTF8);
    R_TEST_CHECK(result.error.index == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    result = r_std_fs_path_from_utf8_bytes(
        &allocator, (RStdStringView){nul_before_invalid, sizeof(nul_before_invalid)});
    R_TEST_CHECK(result.status == R_STD_FS_CALL_ERROR);
    R_TEST_CHECK(result.error.kind == R_STD_FS_PATH_ERROR_EMBEDDED_NUL);
    R_TEST_CHECK(result.error.index == 1U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    result = r_std_fs_path_from_utf8_bytes(&allocator,
                                           (RStdStringView){invalid_only, sizeof(invalid_only)});
    R_TEST_CHECK(result.status == R_STD_FS_CALL_ERROR);
    R_TEST_CHECK(result.error.kind == R_STD_FS_PATH_ERROR_INVALID_UTF8);
    R_TEST_CHECK(result.error.index == 1U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    result = r_std_fs_path_from_utf8(&allocator,
                                     (RStdStringView){valid_with_nul, sizeof(valid_with_nul)});
    R_TEST_CHECK(result.status == R_STD_FS_CALL_ERROR);
    R_TEST_CHECK(result.error.kind == R_STD_FS_PATH_ERROR_EMBEDDED_NUL);
    R_TEST_CHECK(result.error.index == 1U);

    result = r_std_fs_path_from_utf8(&allocator, r_test_fs_view("valid"));
    R_TEST_CHECK(result.status == R_STD_FS_CALL_ERROR);
    R_TEST_CHECK(result.error.kind == R_STD_FS_PATH_ERROR_ALLOCATION_FAILED);
    R_TEST_CHECK(result.error.allocation_error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    return 0;
}

static int r_test_fs_round_trip_clone_and_allocation(void) {
    static const uint8_t path_bytes[] = {UINT8_C('c'),
                                         UINT8_C('a'),
                                         UINT8_C('f'),
                                         UINT8_C(0xc3),
                                         UINT8_C(0xa9),
                                         UINT8_C('/'),
                                         UINT8_C(0xce),
                                         UINT8_C(0xb4)};
    RRuntimeAllocator allocator;
    RStdFsPathResult created;
    RStdFsPathAllocResult cloned;
    RStdFsStringResult converted;
    RStdStringView view;

    r_runtime_allocator_initialize(&allocator);
    created =
        r_std_fs_path_from_utf8_bytes(&allocator, (RStdStringView){path_bytes, sizeof(path_bytes)});
    R_TEST_CHECK(created.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(!r_std_fs_path_is_absolute(&created.value));
    R_TEST_CHECK(r_library_internal_fs_path_length(&created.value) == sizeof(path_bytes));
    R_TEST_CHECK(r_library_internal_fs_path_bytes(&created.value)[sizeof(path_bytes)] ==
                 UINT8_C(0));

    cloned = r_std_fs_path_clone(&created.value);
    R_TEST_CHECK(cloned.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(cloned.value.storage != created.value.storage);
    converted = r_std_fs_path_to_utf8(&cloned.value);
    R_TEST_CHECK(converted.status == R_STD_FS_CALL_SUCCESS);
    view = r_std_string_as_str(&converted.value);
    R_TEST_CHECK(view.length == sizeof(path_bytes));
    R_TEST_CHECK(memcmp(view.data, path_bytes, sizeof(path_bytes)) == 0);
    r_runtime_string_destroy(&converted.value);
    r_std_fs_path_destroy(&cloned.value);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    cloned = r_std_fs_path_clone(&created.value);
    R_TEST_CHECK(cloned.status == R_STD_FS_CALL_ERROR);
    R_TEST_CHECK(cloned.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(r_test_fs_path_equals(&created.value, "caf\xc3\xa9/\xce\xb4"));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    converted = r_std_fs_path_to_utf8(&created.value);
    R_TEST_CHECK(converted.status == R_STD_FS_CALL_ERROR);
    R_TEST_CHECK(converted.error.kind == R_STD_FS_PATH_ERROR_ALLOCATION_FAILED);
    R_TEST_CHECK(converted.error.allocation_error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);

    r_std_fs_path_destroy(&cloned.value);
    r_std_fs_path_destroy(&created.value);
    return 0;
}

static int r_test_fs_join_and_absolute(void) {
    RRuntimeAllocator base_allocator;
    RRuntimeAllocator component_allocator;
    RStdFsPathResult base;
    RStdFsPathResult base_with_separator;
    RStdFsPathResult component;
    RStdFsPathResult absolute;
    RStdFsPathResult empty;
    RStdFsPathResult parent;
    RStdFsPathResult joined;

    r_runtime_allocator_initialize(&base_allocator);
    r_runtime_allocator_initialize(&component_allocator);
    base = r_std_fs_path_from_utf8(&base_allocator, r_test_fs_view("root"));
    base_with_separator = r_std_fs_path_from_utf8(&base_allocator, r_test_fs_view("root/"));
    component = r_std_fs_path_from_utf8(&component_allocator, r_test_fs_view("child"));
    absolute = r_std_fs_path_from_utf8(&component_allocator, r_test_fs_view("/escape"));
    empty = r_std_fs_path_from_utf8(&base_allocator, r_test_fs_view(""));
    parent = r_std_fs_path_from_utf8(&component_allocator, r_test_fs_view(".."));
    R_TEST_CHECK(base.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(base_with_separator.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(component.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(absolute.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(empty.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(parent.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(r_std_fs_path_is_absolute(&absolute.value));
    R_TEST_CHECK(!r_std_fs_path_is_absolute(&empty.value));

    joined = r_std_fs_path_join(&base.value, &component.value);
    R_TEST_CHECK(joined.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(r_test_fs_path_equals(&joined.value, "root/child"));
    r_std_fs_path_destroy(&joined.value);

    joined = r_std_fs_path_join(&base_with_separator.value, &component.value);
    R_TEST_CHECK(joined.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(r_test_fs_path_equals(&joined.value, "root/child"));
    r_std_fs_path_destroy(&joined.value);

    r_runtime_allocator_set_failure(&base_allocator, UINT64_C(1));
    joined = r_std_fs_path_join(&base.value, &component.value);
    R_TEST_CHECK(joined.status == R_STD_FS_CALL_ERROR);
    R_TEST_CHECK(joined.error.kind == R_STD_FS_PATH_ERROR_ALLOCATION_FAILED);
    R_TEST_CHECK(joined.error.allocation_error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(r_test_fs_path_equals(&base.value, "root"));
    R_TEST_CHECK(r_test_fs_path_equals(&component.value, "child"));

    r_runtime_allocator_set_failure(&base_allocator, UINT64_C(1));
    r_runtime_allocator_set_failure(&component_allocator, UINT64_C(0));
    joined = r_std_fs_path_join(&empty.value, &component.value);
    R_TEST_CHECK(joined.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(r_test_fs_path_equals(&joined.value, "child"));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&base_allocator) == UINT64_C(0));
    r_std_fs_path_destroy(&joined.value);

    r_runtime_allocator_set_failure(&base_allocator, UINT64_C(1));
    joined = r_std_fs_path_join(&base.value, &absolute.value);
    R_TEST_CHECK(joined.status == R_STD_FS_CALL_ERROR);
    R_TEST_CHECK(joined.error.kind == R_STD_FS_PATH_ERROR_ABSOLUTE_COMPONENT);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&base_allocator) == UINT64_C(0));

    r_runtime_allocator_set_failure(&base_allocator, UINT64_C(0));
    joined = r_std_fs_path_join(&base.value, &parent.value);
    R_TEST_CHECK(joined.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(r_test_fs_path_equals(&joined.value, "root/.."));
    r_std_fs_path_destroy(&joined.value);

    joined = r_std_fs_path_join(&base.value, &empty.value);
    R_TEST_CHECK(joined.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(r_test_fs_path_equals(&joined.value, "root"));
    r_std_fs_path_destroy(&joined.value);
    r_std_fs_path_destroy(&joined.value);

    r_std_fs_path_destroy(&parent.value);
    joined = r_std_fs_path_join(&empty.value, &empty.value);
    R_TEST_CHECK(joined.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(r_test_fs_path_equals(&joined.value, ""));
    r_std_fs_path_destroy(&joined.value);

    r_std_fs_path_destroy(&empty.value);
    r_std_fs_path_destroy(&absolute.value);
    r_std_fs_path_destroy(&component.value);
    r_std_fs_path_destroy(&base_with_separator.value);
    r_std_fs_path_destroy(&base.value);
    return 0;
}

static int r_test_fs_relative_case(RRuntimeAllocator *allocator,
                                   const char *text,
                                   RLibraryFsRelativePathStatus expected) {
    RStdFsPathResult path = r_std_fs_path_from_utf8(allocator, r_test_fs_view(text));

    if (path.status != R_STD_FS_CALL_SUCCESS) {
        return 1;
    }
    if (r_library_internal_fs_validate_beneath_relative(&path.value) != expected) {
        r_std_fs_path_destroy(&path.value);
        return 1;
    }
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int r_test_fs_beneath_lexical_validation(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_test_fs_relative_case(&allocator, "a/b", R_LIBRARY_FS_RELATIVE_PATH_VALID) == 0);
    R_TEST_CHECK(r_test_fs_relative_case(&allocator, "a\\b", R_LIBRARY_FS_RELATIVE_PATH_VALID) ==
                 0);
    R_TEST_CHECK(r_test_fs_relative_case(&allocator, "", R_LIBRARY_FS_RELATIVE_PATH_EMPTY) == 0);
    R_TEST_CHECK(r_test_fs_relative_case(&allocator, "/a", R_LIBRARY_FS_RELATIVE_PATH_ABSOLUTE) ==
                 0);
    R_TEST_CHECK(r_test_fs_relative_case(
                     &allocator, "a//b", R_LIBRARY_FS_RELATIVE_PATH_EMPTY_COMPONENT) == 0);
    R_TEST_CHECK(
        r_test_fs_relative_case(&allocator, "a/", R_LIBRARY_FS_RELATIVE_PATH_EMPTY_COMPONENT) == 0);
    R_TEST_CHECK(r_test_fs_relative_case(
                     &allocator, "./a", R_LIBRARY_FS_RELATIVE_PATH_CURRENT_COMPONENT) == 0);
    R_TEST_CHECK(r_test_fs_relative_case(
                     &allocator, "a/.", R_LIBRARY_FS_RELATIVE_PATH_CURRENT_COMPONENT) == 0);
    R_TEST_CHECK(r_test_fs_relative_case(
                     &allocator, "a/../b", R_LIBRARY_FS_RELATIVE_PATH_PARENT_COMPONENT) == 0);
    R_TEST_CHECK(r_test_fs_relative_case(&allocator,
                                         R_RUNTIME_DARWIN_FS_STAGING_PREFIX "user",
                                         R_LIBRARY_FS_RELATIVE_PATH_RESERVED_COMPONENT) == 0);
    R_TEST_CHECK(r_test_fs_relative_case(&allocator,
                                         "a/" R_RUNTIME_DARWIN_FS_STAGING_PREFIX "user/b",
                                         R_LIBRARY_FS_RELATIVE_PATH_RESERVED_COMPONENT) == 0);
    R_TEST_CHECK(r_test_fs_relative_case(
                     &allocator, ".r-dir-stag-user", R_LIBRARY_FS_RELATIVE_PATH_VALID) == 0);
    return 0;
}

static void *r_test_fs_thread(void *opaque_context) {
    RTestFsThreadContext *context = opaque_context;
    size_t iteration;

    for (iteration = 0U; iteration < 500U; ++iteration) {
        RStdFsStringResult converted = r_std_fs_path_to_utf8(context->path);
        RStdStringView view;

        if ((converted.status != R_STD_FS_CALL_SUCCESS) ||
            !r_std_fs_path_is_absolute(context->path)) {
            atomic_store_explicit(&context->failed, 1, memory_order_relaxed);
            return NULL;
        }
        view = r_std_string_as_str(&converted.value);
        if ((view.length != 11U) || (memcmp(view.data, "/tmp/shared", 11U) != 0)) {
            atomic_store_explicit(&context->failed, 1, memory_order_relaxed);
        }
        r_runtime_string_destroy(&converted.value);
    }
    return NULL;
}

static int r_test_fs_send_sync_observation(void) {
    enum {
        R_TEST_FS_THREAD_COUNT = 4
    };
    RRuntimeAllocator allocator;
    RStdFsPathResult path;
    RTestFsThreadContext context;
    pthread_t threads[R_TEST_FS_THREAD_COUNT];
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    path = r_std_fs_path_from_utf8(&allocator, r_test_fs_view("/tmp/shared"));
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    context.path = &path.value;
    atomic_init(&context.failed, 0);
    for (index = 0U; index < R_TEST_FS_THREAD_COUNT; ++index) {
        R_TEST_CHECK(pthread_create(&threads[index], NULL, r_test_fs_thread, &context) == 0);
    }
    for (index = 0U; index < R_TEST_FS_THREAD_COUNT; ++index) {
        R_TEST_CHECK(pthread_join(threads[index], NULL) == 0);
    }
    R_TEST_CHECK(atomic_load_explicit(&context.failed, memory_order_relaxed) == 0);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int r_test_fs_error_and_options(void) {
    RStdError error = r_std_fs_as_error((RStdFsError){R_STD_FS_ERROR_NO_SPACE, INT64_C(1234)});
    RStdError closed = r_std_fs_as_error((RStdFsError){R_STD_FS_ERROR_CLOSED, INT64_C(9)});
    RStdFsOpenFileOptions options = {
        R_STD_FS_ACCESS_READ_WRITE,
        R_STD_FS_CREATE_OPEN_OR_CREATE,
        1,
        0,
        0,
    };
    RStdFsMetadata metadata = {
        R_STD_FS_FILE_KIND_REGULAR,
        UINT64_C(4096),
        {0, {0, 0}},
        {1, {INT64_C(123), UINT32_C(456)}},
        {0, {0, 0}},
    };

    R_TEST_CHECK(error.domain == R_STD_ERROR_DOMAIN_FILESYSTEM);
    R_TEST_CHECK(error.code == UINT32_C(12));
    R_TEST_CHECK(error.native_code == INT64_C(1234));
    R_TEST_CHECK(R_STD_FS_ERROR_OTHER == 18);
    R_TEST_CHECK(R_STD_FS_ERROR_CLOSED == 19);
    R_TEST_CHECK(closed.domain == R_STD_ERROR_DOMAIN_FILESYSTEM);
    R_TEST_CHECK(closed.code == UINT32_C(19));
    R_TEST_CHECK(closed.native_code == INT64_C(9));
    R_TEST_CHECK(options.access == R_STD_FS_ACCESS_READ_WRITE);
    R_TEST_CHECK(options.create == R_STD_FS_CREATE_OPEN_OR_CREATE);
    R_TEST_CHECK(options.truncate);
    R_TEST_CHECK(!options.append);
    R_TEST_CHECK(!options.follow_final_symlink);
    R_TEST_CHECK(R_STD_FS_SEEK_ORIGIN_START != R_STD_FS_SEEK_ORIGIN_CURRENT);
    R_TEST_CHECK(R_STD_FS_SEEK_ORIGIN_CURRENT != R_STD_FS_SEEK_ORIGIN_END);
    R_TEST_CHECK(metadata.kind == R_STD_FS_FILE_KIND_REGULAR);
    R_TEST_CHECK(metadata.size == UINT64_C(4096));
    R_TEST_CHECK(metadata.created.r_tag == UINT32_C(0));
    R_TEST_CHECK(metadata.modified.r_tag == UINT32_C(1));
    R_TEST_CHECK(metadata.modified.r_payload.r_some.unix_seconds == INT64_C(123));
    R_TEST_CHECK(metadata.modified.r_payload.r_some.nanoseconds == UINT32_C(456));
    R_TEST_CHECK(metadata.accessed.r_tag == UINT32_C(0));
    return 0;
}

static int r_test_fs_await_directory(RStdFsTaskStartResult started, RStdFsDirectoryResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 1;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK ? 0 : 1;
}

static int r_test_fs_await_file(RStdFsTaskStartResult started, RStdFsFileResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 1;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK ? 0 : 1;
}

static int r_test_fs_await_metadata(RStdFsTaskStartResult started, RStdFsMetadataResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 1;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK ? 0 : 1;
}

static int r_test_fs_await_void(RStdFsTaskStartResult started, RStdFsVoidResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 1;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK ? 0 : 1;
}

static int r_test_fs_await_u64(RStdFsTaskStartResult started, RStdFsU64Result *result) {
    if (!started.is_ok || started.task == NULL) {
        return 1;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK ? 0 : 1;
}

static int r_test_fs_await_iterator(RStdFsTaskStartResult started,
                                    RStdFsDirectoryIterResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 1;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK ? 0 : 1;
}

static int r_test_fs_await_next(RStdFsTaskStartResult started, RStdFsDirectoryNextResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 1;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK ? 0 : 1;
}

static int r_test_fs_persistent_payload_close(void) {
    static const RStdFsDeadline no_deadline = {0};
    static const RStdFsOpenFileOptions options = {
        R_STD_FS_ACCESS_READ,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        0,
    };
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;
    RRuntimeDarwinIoHandleCreateResult io_created;
    RRuntimeDarwinIoBufferResult buffer_created;
    RRuntimeDarwinIoPrepareResult read_prepared;
    RRuntimeDarwinIoPrepareResult close_prepared;
    RRuntimeDarwinIoSubmitResult read_submission;
    RRuntimeDarwinIoSubmitResult close_submission;
    RRuntimeDarwinIoResult read_result;
    RRuntimeDarwinIoResult close_native_result;
    RRuntimeDarwinIoBuffer returned_buffer;
    RStdFsFileStorage *storage;
    RStdFsFileStorage *preexisting_storage;
    RStdFsFile file;
    RStdFsFile preexisting_file;
    RStdFsTaskStartResult operation;
    RStdFsVoidResult result;
    RStdTimeInstantResult now;
    int pipe_descriptors[2];
    int descriptor;
    int preexisting_descriptor;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    service = r_runtime_darwin_fs_service_start(&allocator, 8U);
    R_TEST_CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);
    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    R_TEST_CHECK(pipe(pipe_descriptors) == 0);
    descriptor = pipe_descriptors[0];
    io_created =
        r_runtime_darwin_io_handle_create(&allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    R_TEST_CHECK(io_created.status == R_RUNTIME_DARWIN_IO_START_OK && io_created.handle != NULL);
    r_library_internal_fs_file_publish(storage, descriptor, options, io_created.handle);
    file.storage = storage;

    r_runtime_darwin_io_testing_fail_prepare_stage(R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST);
    operation = r_std_fs_close_file(&file, no_deadline);
    R_TEST_CHECK(!operation.is_ok && operation.task == NULL &&
                 operation.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(file.storage == storage);
    R_TEST_CHECK(r_library_internal_fs_file_descriptor(&file) == descriptor);
    R_TEST_CHECK(pthread_mutex_lock(&storage->handle.mutex) == 0);
    R_TEST_CHECK(!storage->handle.close_reserved &&
                 storage->handle.payload_io == io_created.handle);
    R_TEST_CHECK(pthread_mutex_unlock(&storage->handle.mutex) == 0);

    buffer_created = r_runtime_darwin_io_buffer_allocate(&allocator, 1U);
    R_TEST_CHECK(buffer_created.status == R_RUNTIME_DARWIN_IO_START_OK);
    read_prepared = r_runtime_darwin_io_prepare_read_some(
        io_created.handle, (off_t)0, &buffer_created.buffer, UINT64_C(0));
    R_TEST_CHECK(read_prepared.status == R_RUNTIME_DARWIN_IO_START_OK &&
                 read_prepared.prepared != NULL);
    read_submission =
        r_runtime_darwin_io_prepared_activate(&read_prepared.prepared, &buffer_created.buffer);
    R_TEST_CHECK(read_submission.status == R_RUNTIME_DARWIN_IO_START_OK &&
                 read_submission.request != NULL && read_prepared.prepared == NULL &&
                 buffer_created.buffer.data == NULL);

    r_runtime_darwin_io_testing_force_handle_root_cleanup_error(io_created.handle, EIO);
    operation = r_std_fs_close_file(&file, no_deadline);
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    R_TEST_CHECK(r_test_fs_await_void(operation, &result) == 0);
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_OTHER &&
                 result.r_payload.r_err.native_code == EIO);
    read_result = r_runtime_darwin_io_request_wait(read_submission.request);
    R_TEST_CHECK(read_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED &&
                 read_result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(read_submission.request);
    R_TEST_CHECK(returned_buffer.data != NULL && returned_buffer.capacity == 1U);
    r_runtime_darwin_io_request_release(read_submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    R_TEST_CHECK(close(pipe_descriptors[1]) == 0);

    preexisting_storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(preexisting_storage != NULL);
    preexisting_descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(preexisting_descriptor >= 0);
    io_created = r_runtime_darwin_io_handle_create(
        &allocator, preexisting_descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    R_TEST_CHECK(io_created.status == R_RUNTIME_DARWIN_IO_START_OK && io_created.handle != NULL);
    r_library_internal_fs_file_publish(
        preexisting_storage, preexisting_descriptor, options, io_created.handle);
    r_runtime_darwin_io_testing_force_handle_root_cleanup_error(io_created.handle, EIO);
    close_prepared = r_runtime_darwin_io_prepare_close(io_created.handle, UINT64_C(0));
    R_TEST_CHECK(close_prepared.status == R_RUNTIME_DARWIN_IO_START_OK &&
                 close_prepared.prepared != NULL);
    close_submission = r_runtime_darwin_io_prepared_activate_close(&close_prepared.prepared, 0);
    R_TEST_CHECK(close_submission.status == R_RUNTIME_DARWIN_IO_START_OK &&
                 close_submission.request != NULL && close_prepared.prepared == NULL);
    close_native_result = r_runtime_darwin_io_request_wait(close_submission.request);
    R_TEST_CHECK(close_native_result.native_error == EIO &&
                 close_native_result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(close_submission.request);
    preexisting_file.storage = preexisting_storage;
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    operation = r_std_fs_close_file(&preexisting_file, (RStdFsDeadline){1, now.value});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && preexisting_file.storage == NULL);
    R_TEST_CHECK(r_test_fs_await_void(operation, &result) == 0);
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_OTHER &&
                 result.r_payload.r_err.native_code == EIO);
    errno = 0;
    R_TEST_CHECK(fcntl(preexisting_descriptor, F_GETFD) < 0 && errno == EBADF);

    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_fs_service_stop();
    return 0;
}

static int r_test_fs_close_deadline_and_cancel_races(void) {
    static const RStdFsDeadline no_deadline = {0};
    static const RStdFsOpenFileOptions options = {
        R_STD_FS_ACCESS_READ,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        0,
    };
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;
    RRuntimeDarwinIoHandleCreateResult io_created;
    RStdFsFileStorage *storage;
    RStdFsFile file;
    RStdFsTaskStartResult operation;
    RStdFsVoidResult result;
    RStdTimeDurationResult delay;
    RStdTimeInstantResult now;
    RStdTimeInstantResult deadline;
    RTestFsCloseStartContext start_context;
    RTestFsExecutorStopContext stop_context;
    pthread_t start_thread;
    pthread_t stop_thread;
    size_t spin;
    uint64_t native_sequence;
    _Bool deadline_won;
    _Bool reached;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    service = r_runtime_darwin_fs_service_start(&allocator, 8U);
    R_TEST_CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(r_test_fs_publish_file(&allocator, storage, descriptor, options) != NULL);
    file.storage = storage;
    operation = r_std_fs_close_file(
        &file, (RStdFsDeadline){1, {INT64_MAX, R_STD_TIME_NANOSECONDS_PER_SECOND - 1U}});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    R_TEST_CHECK(r_test_fs_await_void(operation, &result) == 0);
    R_TEST_CHECK(result.r_tag == UINT32_C(0));
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(r_test_fs_publish_file(&allocator, storage, descriptor, options) != NULL);
    file.storage = storage;
    delay = r_std_time_duration_from_parts(INT64_C(0), UINT32_C(100000000));
    R_TEST_CHECK(delay.is_ok);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    deadline = r_std_time_instant_add(now.value, delay.value);
    R_TEST_CHECK(deadline.is_ok);
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    operation = r_std_fs_close_file(&file, (RStdFsDeadline){1, deadline.value});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    R_TEST_CHECK(r_test_fs_sleep_nanoseconds(110000000L));
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_deadline_reported()) {
            break;
        }
        (void)sched_yield();
    }
    reached = spin < 1000000U;
    if (!reached) {
        r_runtime_darwin_fs_service_testing_pause_before_native(0);
    }
    R_TEST_CHECK(reached);
    R_TEST_CHECK(r_library_internal_fs_close_testing_deadline_selected());
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    R_TEST_CHECK(r_test_fs_await_void(operation, &result) == 0);
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 result.r_payload.r_err.native_code == INT64_C(0));
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(r_test_fs_publish_file(&allocator, storage, descriptor, options) != NULL);
    file.storage = storage;
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    r_runtime_darwin_io_testing_force_root_cleanup_error(EIO);
    r_runtime_darwin_io_testing_pause_next_native_completion();
    r_library_internal_fs_close_testing_pause_before_cancel_report(1);
    operation = r_std_fs_close_file(&file, no_deadline);
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    r_runtime_darwin_io_testing_wait_for_native_completion();
    (void)memset(&stop_context, 0, sizeof(stop_context));
    R_TEST_CHECK(
        pthread_create(&stop_thread, NULL, r_test_fs_executor_stop_thread, &stop_context) == 0);
    R_TEST_CHECK(r_test_fs_wait_executor_stopping());
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_cancel_report_reached()) {
            break;
        }
        (void)sched_yield();
    }
    reached = spin < 1000000U;
    if (!reached) {
        r_runtime_darwin_io_testing_release_native_completion();
        r_runtime_darwin_fs_service_testing_pause_before_native(0);
        r_library_internal_fs_close_testing_pause_before_cancel_report(0);
    }
    R_TEST_CHECK(reached);
    r_runtime_darwin_io_testing_release_native_completion();
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    r_library_internal_fs_close_testing_pause_before_cancel_report(0);
    R_TEST_CHECK(pthread_join(stop_thread, NULL) == 0 && stop_context.stopped);
    R_TEST_CHECK(r_test_fs_await_void(operation, &result) == 0);
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_OTHER &&
                 result.r_payload.r_err.native_code == EIO);
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(r_test_fs_publish_file(&allocator, storage, descriptor, options) != NULL);
    file.storage = storage;
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    r_library_internal_fs_close_testing_pause_before_cancel_report(1);
    operation = r_std_fs_close_file(&file, no_deadline);
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    (void)memset(&stop_context, 0, sizeof(stop_context));
    R_TEST_CHECK(
        pthread_create(&stop_thread, NULL, r_test_fs_executor_stop_thread, &stop_context) == 0);
    R_TEST_CHECK(r_test_fs_wait_executor_stopping());
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_cancel_report_reached()) {
            break;
        }
        (void)sched_yield();
    }
    reached = spin < 1000000U;
    if (!reached) {
        r_runtime_darwin_fs_service_testing_pause_before_native(0);
        r_library_internal_fs_close_testing_pause_before_cancel_report(0);
    }
    R_TEST_CHECK(reached);
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    r_library_internal_fs_close_testing_pause_before_cancel_report(0);
    R_TEST_CHECK(pthread_join(stop_thread, NULL) == 0 && stop_context.stopped);
    R_TEST_CHECK(r_runtime_task_await(&operation.task, &result) == R_RUNTIME_TASK_AWAIT_CANCELLED &&
                 operation.task == NULL);
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(r_test_fs_publish_file(&allocator, storage, descriptor, options) != NULL);
    file.storage = storage;
    delay = r_std_time_duration_from_parts(INT64_C(0), UINT32_C(100000000));
    R_TEST_CHECK(delay.is_ok);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    deadline = r_std_time_instant_add(now.value, delay.value);
    R_TEST_CHECK(deadline.is_ok);
    native_sequence = r_runtime_darwin_fs_service_testing_native_sequence();
    r_runtime_darwin_fs_service_testing_pause_after_native(1);
    operation = r_std_fs_close_file(&file, (RStdFsDeadline){1, deadline.value});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_runtime_darwin_fs_service_testing_native_sequence() != native_sequence) {
            break;
        }
        (void)sched_yield();
    }
    reached = spin < 1000000U;
    if (!reached) {
        r_runtime_darwin_fs_service_testing_pause_after_native(0);
    }
    R_TEST_CHECK(reached);
    R_TEST_CHECK(r_test_fs_sleep_nanoseconds(110000000L));
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_deadline_reported()) {
            break;
        }
        (void)sched_yield();
    }
    reached = spin < 1000000U;
    r_runtime_darwin_fs_service_testing_pause_after_native(0);
    R_TEST_CHECK(reached);
    R_TEST_CHECK(r_library_internal_fs_close_testing_deadline_selected());
    R_TEST_CHECK(r_test_fs_await_void(operation, &result) == 0);
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 result.r_payload.r_err.native_code == INT64_C(0));
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(r_test_fs_publish_file(&allocator, storage, descriptor, options) != NULL);
    file.storage = storage;
    delay = r_std_time_duration_from_parts(INT64_C(0), UINT32_C(100000000));
    R_TEST_CHECK(delay.is_ok);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    deadline = r_std_time_instant_add(now.value, delay.value);
    R_TEST_CHECK(deadline.is_ok);
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    r_library_internal_fs_close_testing_pause_after_component_error_selection(1);
    operation = r_std_fs_close_file(&file, (RStdFsDeadline){1, deadline.value});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    R_TEST_CHECK(close(descriptor) == 0);
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_selected_error_component() ==
            R_LIBRARY_INTERNAL_FS_CLOSE_TEST_COMPONENT_FS) {
            break;
        }
        (void)sched_yield();
    }
    reached = spin < 1000000U;
    if (!reached) {
        r_library_internal_fs_close_testing_pause_after_component_error_selection(0);
    }
    R_TEST_CHECK(reached);
    R_TEST_CHECK(r_test_fs_sleep_nanoseconds(110000000L));
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_deadline_reported()) {
            break;
        }
        (void)sched_yield();
    }
    reached = spin < 1000000U;
    deadline_won = r_library_internal_fs_close_testing_deadline_selected();
    r_library_internal_fs_close_testing_pause_after_component_error_selection(0);
    R_TEST_CHECK(reached);
    R_TEST_CHECK(!deadline_won);
    R_TEST_CHECK(r_test_fs_await_void(operation, &result) == 0);
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_CLOSED &&
                 result.r_payload.r_err.native_code == EBADF);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    io_created =
        r_runtime_darwin_io_handle_create(&allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    R_TEST_CHECK(io_created.status == R_RUNTIME_DARWIN_IO_START_OK && io_created.handle != NULL);
    r_library_internal_fs_file_publish(storage, descriptor, options, io_created.handle);
    file.storage = storage;
    delay = r_std_time_duration_from_parts(INT64_C(0), UINT32_C(100000000));
    R_TEST_CHECK(delay.is_ok);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    deadline = r_std_time_instant_add(now.value, delay.value);
    R_TEST_CHECK(deadline.is_ok);
    r_runtime_darwin_io_testing_force_root_cleanup_error(EIO);
    r_runtime_darwin_io_testing_pause_next_native_completion();
    operation = r_std_fs_close_file(&file, (RStdFsDeadline){1, deadline.value});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    r_runtime_darwin_io_testing_wait_for_native_completion();
    R_TEST_CHECK(r_test_fs_sleep_nanoseconds(110000000L));
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_deadline_reported()) {
            break;
        }
        (void)sched_yield();
    }
    reached = spin < 1000000U;
    deadline_won = r_library_internal_fs_close_testing_deadline_selected();
    R_TEST_CHECK(reached);
    R_TEST_CHECK(deadline_won);
    r_runtime_darwin_io_testing_release_native_completion();
    R_TEST_CHECK(r_test_fs_await_void(operation, &result) == 0);
    R_TEST_CHECK(!r_library_internal_fs_close_testing_deadline_selected());
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_OTHER &&
                 result.r_payload.r_err.native_code == EIO);
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(r_test_fs_publish_file(&allocator, storage, descriptor, options) != NULL);
    file.storage = storage;
    delay = r_std_time_duration_from_parts(INT64_C(0), UINT32_C(1000000));
    R_TEST_CHECK(delay.is_ok);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    deadline = r_std_time_instant_add(now.value, delay.value);
    R_TEST_CHECK(deadline.is_ok);
    (void)memset(&start_context, 0, sizeof(start_context));
    start_context.file = &file;
    start_context.deadline = (RStdFsDeadline){1, deadline.value};
    r_library_internal_fs_close_testing_pause_before_deadline_check(1);
    R_TEST_CHECK(
        pthread_create(&start_thread, NULL, r_test_fs_close_start_thread, &start_context) == 0);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_deadline_check_reached()) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    for (spin = 0U; spin < 1000000U; ++spin) {
        now = r_std_time_monotonic_now();
        R_TEST_CHECK(now.is_ok);
        if (now.value.storage_seconds > deadline.value.storage_seconds ||
            (now.value.storage_seconds == deadline.value.storage_seconds &&
             now.value.storage_nanoseconds >= deadline.value.storage_nanoseconds)) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    r_library_internal_fs_close_testing_pause_before_deadline_check(0);
    R_TEST_CHECK(pthread_join(start_thread, NULL) == 0);
    R_TEST_CHECK(start_context.started.is_ok && start_context.started.task != NULL &&
                 file.storage == NULL);
    R_TEST_CHECK(r_test_fs_await_void(start_context.started, &result) == 0);
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 result.r_payload.r_err.native_code == INT64_C(0));
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    io_created =
        r_runtime_darwin_io_handle_create(&allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    R_TEST_CHECK(io_created.status == R_RUNTIME_DARWIN_IO_START_OK && io_created.handle != NULL);
    r_library_internal_fs_file_publish(storage, descriptor, options, io_created.handle);
    file.storage = storage;
    r_library_internal_fs_close_testing_pause_after_duty_publish(1);
    operation = r_std_fs_close_file(
        &file, (RStdFsDeadline){1, {INT64_MAX, R_STD_TIME_NANOSECONDS_PER_SECOND - 1U}});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_duty_publish_waiter_count() >= 2U) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    r_library_internal_fs_close_testing_pause_after_duty_publish(0);
    R_TEST_CHECK(r_test_fs_await_void(operation, &result) == 0);
    R_TEST_CHECK(result.r_tag == UINT32_C(0));
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);

    storage = r_library_internal_fs_file_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(r_test_fs_publish_file(&allocator, storage, descriptor, options) != NULL);
    file.storage = storage;
    r_library_internal_fs_close_testing_pause_before_finalize_selection(1);
    r_library_internal_fs_close_testing_pause_before_cancel_report(1);
    operation = r_std_fs_close_file(&file, no_deadline);
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_finalize_selection_reached()) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    r_runtime_task_cancel(&operation.task);
    R_TEST_CHECK(operation.task == NULL);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_cancel_report_reached()) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    r_library_internal_fs_close_testing_pause_before_finalize_selection(0);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_close_testing_finalize_selection_completed()) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    r_library_internal_fs_close_testing_pause_before_cancel_report(0);

    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_fs_service_stop();
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    return 0;
}

static RStdFsPathResult r_test_fs_path(RRuntimeAllocator *allocator, const char *text) {
    return r_std_fs_path_from_utf8(allocator, r_test_fs_view(text));
}

static int r_test_fs_create_file(int directory_fd, const char *path) {
    int descriptor = openat(directory_fd,
                            path,
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                            S_IRUSR | S_IWUSR);

    if (descriptor < 0) {
        return -1;
    }
    if (write(descriptor, "R", 1U) != 1) {
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static int r_test_fs_has_staging_entry(int directory_fd) {
    DIR *stream;
    struct dirent *entry;
    int duplicate = openat(directory_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    int found = 0;

    if (duplicate < 0) {
        return -1;
    }
    stream = fdopendir(duplicate);
    if (stream == NULL) {
        (void)close(duplicate);
        return -1;
    }
    while ((entry = readdir(stream)) != NULL) {
        if (strncmp(entry->d_name,
                    R_RUNTIME_DARWIN_FS_STAGING_PREFIX,
                    sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) - 1U) == 0) {
            found = 1;
            break;
        }
    }
    if (closedir(stream) != 0) {
        return -1;
    }
    return found;
}

static int r_test_fs_async_open_lifecycle(void) {
    enum {
        R_TEST_FS_ASYNC_COUNT = 8
    };
    static const RStdFsDeadline no_deadline = {0};
    static const RStdFsOpenFileOptions read_existing = {
        R_STD_FS_ACCESS_READ,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        0,
    };
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;
    RStdFsPathResult root_path;
    RStdFsPathResult relative;
    RStdFsDirectoryResult directory_result = {0};
    RStdFsDirectoryResult child_result = {0};
    RStdFsFileResult file_result = {0};
    RStdFsTaskStartResult started;
    RStdFsTaskStartResult concurrent[R_TEST_FS_ASYNC_COUNT];
    RStdTimeInstantResult now;
    RStdFsDirectory root = {0};
    RStdFsDirectory moved_directory = {0};
    char path_template[] = "/tmp/r-std-fs-XXXXXX";
    char absolute_file[sizeof(path_template) + 16U];
    char *temporary_directory;
    uint8_t *oversized_bytes;
    uint64_t entry_sequence;
    uint64_t signal_count;
    int root_descriptor;
    int descriptor;
    int released_descriptor;
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    R_TEST_CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);
    temporary_directory = mkdtemp(path_template);
    R_TEST_CHECK(temporary_directory != NULL);
    descriptor = open(temporary_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(mkdirat(descriptor, "child", S_IRWXU) == 0);
    released_descriptor = r_test_fs_create_file(descriptor, "existing");
    R_TEST_CHECK(released_descriptor >= 0);
    R_TEST_CHECK(close(released_descriptor) == 0);
    released_descriptor = r_test_fs_create_file(descriptor, "child/nested");
    R_TEST_CHECK(released_descriptor >= 0);
    R_TEST_CHECK(close(released_descriptor) == 0);
    R_TEST_CHECK(symlinkat("existing", descriptor, "link") == 0);
    R_TEST_CHECK(symlinkat("child", descriptor, "dirlink") == 0);
    R_TEST_CHECK(close(descriptor) == 0);

    root_path = r_test_fs_path(&allocator, temporary_directory);
    R_TEST_CHECK(root_path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_open_directory(&root_path.value, no_deadline);
    r_std_fs_path_destroy(&root_path.value);
    R_TEST_CHECK(r_test_fs_await_directory(started, &directory_result) == 0);
    R_TEST_CHECK(directory_result.r_tag == UINT32_C(0));
    root = directory_result.r_payload.r_ok;
    directory_result.r_payload.r_ok.storage = NULL;
    root_descriptor = r_library_internal_fs_directory_descriptor(&root);
    R_TEST_CHECK(root_descriptor >= 0);

    {
        static const RRuntimeDarwinIoHandleCreateFailureStage materialization_failures[] = {
            R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_ALLOCATION,
            R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_DUPLICATE,
            R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_QUEUE,
        };

        for (index = 0U;
             index < sizeof(materialization_failures) / sizeof(materialization_failures[0]);
             ++index) {
            uint64_t cleanup_count = r_runtime_darwin_fs_testing_open_result_cleanup_count();
            int cleanup_fd;

            relative = r_test_fs_path(&allocator, "existing");
            R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
            r_runtime_darwin_io_testing_fail_handle_create_stage(materialization_failures[index]);
            started =
                r_std_fs_open_file_beneath(&root, &relative.value, read_existing, no_deadline);
            R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
            R_TEST_CHECK(file_result.r_tag != UINT32_C(0) &&
                         file_result.r_payload.r_err.code == R_STD_FS_ERROR_RESOURCE_EXHAUSTED);
            if (materialization_failures[index] ==
                R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_ALLOCATION) {
                R_TEST_CHECK(file_result.r_payload.r_err.native_code == INT64_C(0));
            } else {
                R_TEST_CHECK(file_result.r_payload.r_err.native_code == ENOMEM);
            }
            R_TEST_CHECK(r_runtime_darwin_fs_testing_open_result_cleanup_count() ==
                         cleanup_count + UINT64_C(1));
            cleanup_fd = r_runtime_darwin_fs_testing_last_open_result_cleanup_fd();
            errno = 0;
            R_TEST_CHECK(cleanup_fd >= 0 && fcntl(cleanup_fd, F_GETFD) < 0 && errno == EBADF);
            R_TEST_CHECK(faccessat(root_descriptor, "existing", F_OK, AT_SYMLINK_NOFOLLOW) == 0);
            r_std_fs_path_destroy(&relative.value);
        }
    }

    oversized_bytes = malloc(R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT);
    R_TEST_CHECK(oversized_bytes != NULL);
    (void)memset(oversized_bytes, 'a', R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT);
    relative = r_std_fs_path_from_utf8_bytes(
        &allocator, (RStdStringView){oversized_bytes, R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT});
    free(oversized_bytes);
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_open_file(&relative.value, read_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag != UINT32_C(0) &&
                 file_result.r_payload.r_err.code == R_STD_FS_ERROR_NAME_TOO_LONG &&
                 file_result.r_payload.r_err.native_code == INT64_C(0));
    r_std_fs_path_destroy(&relative.value);

    relative = r_test_fs_path(&allocator, "existing");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_open_file_beneath(&root, &relative.value, read_existing, no_deadline);
    r_std_fs_path_destroy(&relative.value);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    released_descriptor = r_library_internal_fs_file_descriptor(&file_result.r_payload.r_ok);
    R_TEST_CHECK(released_descriptor >= 0);
    r_std_fs_file_destroy(&file_result.r_payload.r_ok);
    errno = 0;
    R_TEST_CHECK(fcntl(released_descriptor, F_GETFD) < 0 && errno == EBADF);

    relative = r_test_fs_path(&allocator, "child");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_open_directory_beneath(&root, &relative.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_directory(started, &child_result) == 0);
    R_TEST_CHECK(child_result.r_tag == UINT32_C(0));
    r_std_fs_directory_move_initialize(&moved_directory, &child_result.r_payload.r_ok);
    R_TEST_CHECK(child_result.r_payload.r_ok.storage == NULL);
    r_std_fs_directory_destroy(&moved_directory);
    r_std_fs_path_destroy(&relative.value);

    relative = r_test_fs_path(&allocator, "link");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_open_file_beneath(&root, &relative.value, read_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag != UINT32_C(0) &&
                 file_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH);
    r_std_fs_path_destroy(&relative.value);

    relative = r_test_fs_path(&allocator, "dirlink/nested");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_open_file_beneath(&root, &relative.value, read_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag != UINT32_C(0) &&
                 file_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH);
    r_std_fs_path_destroy(&relative.value);

    relative = r_test_fs_path(&allocator, "existing");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_open_file_beneath(
        &root,
        &relative.value,
        (RStdFsOpenFileOptions){R_STD_FS_ACCESS_READ, R_STD_FS_CREATE_EXISTING, 1, 0, 0},
        no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag != UINT32_C(0) &&
                 file_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION);
    started = r_std_fs_open_file_beneath(
        &root,
        &relative.value,
        (RStdFsOpenFileOptions){R_STD_FS_ACCESS_READ, R_STD_FS_CREATE_EXISTING, 0, 0, 1},
        no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag != UINT32_C(0) &&
                 file_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH);
    r_std_fs_path_destroy(&relative.value);

    relative = r_test_fs_path(&allocator, "created");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_open_file_beneath(
        &root,
        &relative.value,
        (RStdFsOpenFileOptions){R_STD_FS_ACCESS_WRITE, R_STD_FS_CREATE_NEW, 0, 0, 0},
        no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    r_std_fs_file_destroy(&file_result.r_payload.r_ok);
    started = r_std_fs_open_file_beneath(
        &root,
        &relative.value,
        (RStdFsOpenFileOptions){R_STD_FS_ACCESS_WRITE, R_STD_FS_CREATE_NEW, 0, 0, 0},
        no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag != UINT32_C(0) &&
                 file_result.r_payload.r_err.code == R_STD_FS_ERROR_ALREADY_EXISTS);
    r_std_fs_path_destroy(&relative.value);

    R_TEST_CHECK(
        snprintf(absolute_file, sizeof(absolute_file), "%s/%s", temporary_directory, "existing") >
        0);
    relative = r_test_fs_path(&allocator, absolute_file);
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    started = r_std_fs_open_file(&relative.value, read_existing, (RStdFsDeadline){1, now.value});
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag != UINT32_C(0) &&
                 file_result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 file_result.r_payload.r_err.native_code == INT64_C(0));
    r_std_fs_path_destroy(&relative.value);

    relative = r_test_fs_path(&allocator, "must-not-create");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(4));
    started = r_std_fs_open_file_beneath(
        &root,
        &relative.value,
        (RStdFsOpenFileOptions){R_STD_FS_ACCESS_WRITE, R_STD_FS_CREATE_NEW, 0, 0, 0},
        no_deadline);
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(r_library_internal_fs_directory_descriptor(&root) == root_descriptor);
    errno = 0;
    R_TEST_CHECK(faccessat(root_descriptor, "must-not-create", F_OK, AT_SYMLINK_NOFOLLOW) < 0 &&
                 errno == ENOENT);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    r_std_fs_path_destroy(&relative.value);

    relative = r_test_fs_path(&allocator, "existing");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    for (index = 0U; index < R_TEST_FS_ASYNC_COUNT; ++index) {
        concurrent[index] =
            r_std_fs_open_file_beneath(&root, &relative.value, read_existing, no_deadline);
        R_TEST_CHECK(concurrent[index].is_ok);
    }
    r_std_fs_path_destroy(&relative.value);
    for (index = 0U; index < R_TEST_FS_ASYNC_COUNT; ++index) {
        R_TEST_CHECK(r_test_fs_await_file(concurrent[index], &file_result) == 0);
        R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
        r_std_fs_file_destroy(&file_result.r_payload.r_ok);
    }

    relative = r_test_fs_path(&allocator, "cancelled-created");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    signal_count = r_runtime_darwin_fs_service_testing_signal_count();
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    started = r_std_fs_open_file_beneath(
        &root,
        &relative.value,
        (RStdFsOpenFileOptions){R_STD_FS_ACCESS_WRITE, R_STD_FS_CREATE_NEW, 0, 0, 0},
        no_deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    while (r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence) {
        (void)sched_yield();
    }
    r_runtime_task_cancel(&started.task);
    while (r_runtime_darwin_fs_service_testing_signal_count() == signal_count) {
        (void)sched_yield();
    }
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    r_std_fs_path_destroy(&relative.value);

    /* L41-1: the open holds the root until its native call ends, so the worker waits before
       that call while the last handle of the root goes away; without the pause a fast worker
       finished first and the root closed with its handle. */
    relative = r_test_fs_path(&allocator, "existing");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    started = r_std_fs_open_file_beneath(&root, &relative.value, read_existing, no_deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    while (r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence) {
        (void)sched_yield();
    }
    r_std_fs_directory_destroy(&root);
    r_std_fs_path_destroy(&relative.value);
    R_TEST_CHECK(fcntl(root_descriptor, F_GETFD) >= 0);
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    for (index = 0U; index < 1000000U; ++index) {
        errno = 0;
        if (fcntl(root_descriptor, F_GETFD) < 0 && errno == EBADF) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(index < 1000000U);
    r_std_fs_file_destroy(&file_result.r_payload.r_ok);

    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_fs_service_stop();
    descriptor = open(temporary_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(faccessat(descriptor, "cancelled-created", F_OK, AT_SYMLINK_NOFOLLOW) == 0);
    R_TEST_CHECK(unlinkat(descriptor, "existing", 0) == 0);
    R_TEST_CHECK(unlinkat(descriptor, "created", 0) == 0);
    R_TEST_CHECK(unlinkat(descriptor, "cancelled-created", 0) == 0);
    R_TEST_CHECK(unlinkat(descriptor, "link", 0) == 0);
    R_TEST_CHECK(unlinkat(descriptor, "dirlink", 0) == 0);
    R_TEST_CHECK(unlinkat(descriptor, "child/nested", 0) == 0);
    R_TEST_CHECK(unlinkat(descriptor, "child", AT_REMOVEDIR) == 0);
    R_TEST_CHECK(close(descriptor) == 0);
    R_TEST_CHECK(rmdir(temporary_directory) == 0);
    return 0;
}

static int r_test_fs_async_namespace_lifecycle(void) {
    static const RStdFsDeadline no_deadline = {0};
    static const RStdFsOpenFileOptions read_existing = {
        R_STD_FS_ACCESS_READ,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        0,
    };
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;
    RStdFsPathResult path;
    RStdFsPathResult outside_path;
    RStdFsDirectoryResult directory_result = {0};
    RStdFsDirectoryResult outside_result = {0};
    RStdFsFileResult file_result = {0};
    RStdFsMetadataResult metadata_result = {0};
    RStdFsVoidResult void_result = {0};
    RStdFsTaskStartResult started;
    RStdFsDirectory root = {0};
    RStdFsDirectory outside_root = {0};
    RStdTimeInstantResult now;
    struct stat metadata;
    char path_template[] = "/tmp/r-std-fs-namespace-XXXXXX";
    char outside_template[] = "/tmp/r-std-fs-outside-XXXXXX";
    char target[160];
    char *temporary_directory;
    char *outside_directory;
    uint64_t entry_sequence;
    uint64_t signal_count;
    size_t spin;
    _Bool cancellation_seen;
    int root_descriptor;
    int outside_descriptor;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    R_TEST_CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);
    temporary_directory = mkdtemp(path_template);
    outside_directory = mkdtemp(outside_template);
    R_TEST_CHECK(temporary_directory != NULL);
    R_TEST_CHECK(outside_directory != NULL);

    path = r_test_fs_path(&allocator, temporary_directory);
    outside_path = r_test_fs_path(&allocator, outside_directory);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(outside_path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_open_directory(&path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_directory(started, &directory_result) == 0);
    R_TEST_CHECK(directory_result.r_tag == UINT32_C(0));
    started = r_std_fs_open_directory(&outside_path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_directory(started, &outside_result) == 0);
    R_TEST_CHECK(outside_result.r_tag == UINT32_C(0));
    r_std_fs_path_destroy(&outside_path.value);
    r_std_fs_path_destroy(&path.value);
    root = directory_result.r_payload.r_ok;
    outside_root = outside_result.r_payload.r_ok;
    directory_result.r_payload.r_ok.storage = NULL;
    outside_result.r_payload.r_ok.storage = NULL;
    root_descriptor = r_library_internal_fs_directory_descriptor(&root);
    outside_descriptor = r_library_internal_fs_directory_descriptor(&outside_root);
    R_TEST_CHECK(root_descriptor >= 0);
    R_TEST_CHECK(outside_descriptor >= 0);

    path = r_test_fs_path(&allocator, R_RUNTIME_DARWIN_FS_STAGING_PREFIX "ordinary");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    started = r_std_fs_create_directory(&path.value, 0, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_PATH &&
                 void_result.r_payload.r_err.native_code == INT64_C(0));
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    r_std_fs_path_destroy(&path.value);

    R_TEST_CHECK(snprintf(target,
                          sizeof(target),
                          "%s/%s%s",
                          temporary_directory,
                          R_RUNTIME_DARWIN_FS_STAGING_PREFIX,
                          "ordinary") > 0);
    path = r_test_fs_path(&allocator, target);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    started = r_std_fs_open_file(&path.value, read_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag != UINT32_C(0) &&
                 file_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_PATH &&
                 file_result.r_payload.r_err.native_code == INT64_C(0));
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    r_std_fs_path_destroy(&path.value);

    R_TEST_CHECK(snprintf(target, sizeof(target), "%s/ordinary/a", temporary_directory) > 0);
    path = r_test_fs_path(&allocator, target);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_create_directory(&path.value, 1, no_deadline);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(fstatat(root_descriptor, "ordinary/a", &metadata, 0) == 0);
    R_TEST_CHECK(S_ISDIR(metadata.st_mode));

    path = r_test_fs_path(&allocator, "nested/a/b");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_create_directory_beneath(&root, &path.value, 1, no_deadline);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(fstatat(root_descriptor, "nested/a/b", &metadata, 0) == 0);
    R_TEST_CHECK(S_ISDIR(metadata.st_mode));
    R_TEST_CHECK((metadata.st_mode & (S_IRWXG | S_IRWXO)) == 0);
    R_TEST_CHECK(r_test_fs_has_staging_entry(root_descriptor) == 0);

    path = r_test_fs_path(&allocator, "nested/a/b");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_create_directory_beneath(&root, &path.value, 1, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    started = r_std_fs_create_directory_beneath(&root, &path.value, 0, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_ALREADY_EXISTS);
    r_std_fs_path_destroy(&path.value);

    path = r_test_fs_path(&allocator, "missing/child");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_create_directory_beneath(&root, &path.value, 0, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_NOT_FOUND);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(r_test_fs_has_staging_entry(root_descriptor) == 0);

    descriptor = r_test_fs_create_file(root_descriptor, "data");
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(close(descriptor) == 0);
    path = r_test_fs_path(&allocator, "data");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_metadata_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_metadata(started, &metadata_result) == 0);
    R_TEST_CHECK(metadata_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(metadata_result.r_payload.r_ok.kind == R_STD_FS_FILE_KIND_REGULAR);
    R_TEST_CHECK(metadata_result.r_payload.r_ok.size == UINT64_C(1));
    started = r_std_fs_open_file_beneath(&root, &path.value, read_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(started, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    started = r_std_fs_file_metadata(&file_result.r_payload.r_ok, no_deadline);
    r_std_fs_file_destroy(&file_result.r_payload.r_ok);
    R_TEST_CHECK(r_test_fs_await_metadata(started, &metadata_result) == 0);
    R_TEST_CHECK(metadata_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(metadata_result.r_payload.r_ok.kind == R_STD_FS_FILE_KIND_REGULAR);
    R_TEST_CHECK(metadata_result.r_payload.r_ok.size == UINT64_C(1));
    r_std_fs_path_destroy(&path.value);

    R_TEST_CHECK(mkdirat(root_descriptor, "remove-directory", S_IRWXU) == 0);
    descriptor = r_test_fs_create_file(root_descriptor, "remove-file");
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(close(descriptor) == 0);
    path = r_test_fs_path(&allocator, "remove-directory");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_remove_file_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_IS_DIRECTORY);
    started = r_std_fs_remove_directory_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    r_std_fs_path_destroy(&path.value);

    R_TEST_CHECK(mkdirat(root_descriptor, "nonempty-directory", S_IRWXU) == 0);
    descriptor = r_test_fs_create_file(root_descriptor, "nonempty-directory/child");
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(close(descriptor) == 0);
    path = r_test_fs_path(&allocator, "nonempty-directory");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_remove_directory_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_DIRECTORY_NOT_EMPTY);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(unlinkat(root_descriptor, "nonempty-directory/child", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "nonempty-directory", AT_REMOVEDIR) == 0);
    path = r_test_fs_path(&allocator, "remove-file");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_remove_directory_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_NOT_DIRECTORY);
    started = r_std_fs_remove_file_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    r_std_fs_path_destroy(&path.value);

    descriptor = r_test_fs_create_file(outside_descriptor, "target");
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(close(descriptor) == 0);
    R_TEST_CHECK(snprintf(target, sizeof(target), "%s/target", outside_directory) > 0);
    R_TEST_CHECK(symlinkat(target, root_descriptor, "link") == 0);
    path = r_test_fs_path(&allocator, "link");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_metadata_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_metadata(started, &metadata_result) == 0);
    R_TEST_CHECK(metadata_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(metadata_result.r_payload.r_ok.kind == R_STD_FS_FILE_KIND_SYMLINK);
    started = r_std_fs_remove_directory_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_NOT_DIRECTORY);
    outside_path = r_test_fs_path(&allocator, "moved-link");
    R_TEST_CHECK(outside_path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_rename_beneath(&root, &path.value, &root, &outside_path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    r_std_fs_path_destroy(&path.value);
    started = r_std_fs_remove_file_beneath(&root, &outside_path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    r_std_fs_path_destroy(&outside_path.value);
    R_TEST_CHECK(fstatat(outside_descriptor, "target", &metadata, 0) == 0);

    descriptor = r_test_fs_create_file(root_descriptor, "rename-source");
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(close(descriptor) == 0);
    descriptor = r_test_fs_create_file(root_descriptor, "rename-destination");
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(close(descriptor) == 0);
    path = r_test_fs_path(&allocator, "rename-source");
    outside_path = r_test_fs_path(&allocator, "rename-destination");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(outside_path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_rename_beneath(&root, &path.value, &root, &outside_path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_ALREADY_EXISTS);
    R_TEST_CHECK(fstatat(root_descriptor, "rename-source", &metadata, 0) == 0);
    R_TEST_CHECK(fstatat(root_descriptor, "rename-destination", &metadata, 0) == 0);
    r_std_fs_path_destroy(&outside_path.value);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(S_ISREG(metadata.st_mode));

    R_TEST_CHECK(symlinkat(outside_directory, root_descriptor, "escape") == 0);
    path = r_test_fs_path(&allocator, "escape/target");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_metadata_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_metadata(started, &metadata_result) == 0);
    R_TEST_CHECK(metadata_result.r_tag != UINT32_C(0) &&
                 metadata_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH);
    started = r_std_fs_remove_file_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH);
    outside_path = r_test_fs_path(&allocator, "must-not-move");
    R_TEST_CHECK(outside_path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_rename_beneath(&root, &path.value, &root, &outside_path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH);
    r_std_fs_path_destroy(&outside_path.value);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(fstatat(outside_descriptor, "target", &metadata, 0) == 0);

    descriptor = r_test_fs_create_file(root_descriptor, "cross-source");
    R_TEST_CHECK(descriptor >= 0);
    R_TEST_CHECK(close(descriptor) == 0);
    path = r_test_fs_path(&allocator, "cross-source");
    outside_path = r_test_fs_path(&allocator, "cross-destination");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(outside_path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_rename_beneath(
        &root, &path.value, &outside_root, &outside_path.value, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    r_std_fs_path_destroy(&outside_path.value);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(fstatat(outside_descriptor, "cross-destination", &metadata, 0) == 0);

    path = r_test_fs_path(&allocator, R_RUNTIME_DARWIN_FS_STAGING_PREFIX "user");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    started = r_std_fs_create_directory_beneath(&root, &path.value, 0, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH &&
                 void_result.r_payload.r_err.native_code == INT64_C(0));
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    r_std_fs_path_destroy(&path.value);

    path = r_test_fs_path(&allocator, "expired-directory");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    started =
        r_std_fs_create_directory_beneath(&root, &path.value, 0, (RStdFsDeadline){1, now.value});
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 void_result.r_payload.r_err.native_code == INT64_C(0));
    r_std_fs_path_destroy(&path.value);
    errno = 0;
    R_TEST_CHECK(faccessat(root_descriptor, "expired-directory", F_OK, AT_SYMLINK_NOFOLLOW) != 0 &&
                 errno == ENOENT);

    path = r_test_fs_path(&allocator, "allocation-failure-directory");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(2));
    started = r_std_fs_create_directory_beneath(&root, &path.value, 0, no_deadline);
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(r_test_fs_path_equals(&path.value, "allocation-failure-directory"));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    r_std_fs_path_destroy(&path.value);
    errno = 0;
    R_TEST_CHECK(
        faccessat(root_descriptor, "allocation-failure-directory", F_OK, AT_SYMLINK_NOFOLLOW) !=
            0 &&
        errno == ENOENT);

    r_runtime_darwin_fs_testing_force_staging_collisions(R_RUNTIME_DARWIN_FS_CREATE_RETRY_LIMIT);
    path = r_test_fs_path(&allocator, "collision-exhausted");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_create_directory_beneath(&root, &path.value, 0, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(started, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_RESOURCE_EXHAUSTED &&
                 void_result.r_payload.r_err.native_code == EAGAIN);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(r_test_fs_has_staging_entry(root_descriptor) == 0);

    path = r_test_fs_path(&allocator, "cancelled-directory");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    signal_count = r_runtime_darwin_fs_service_testing_signal_count();
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    started = r_std_fs_create_directory_beneath(&root, &path.value, 0, no_deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_runtime_darwin_fs_service_testing_entry_sequence() != entry_sequence) {
            break;
        }
        (void)sched_yield();
    }
    cancellation_seen = spin < 1000000U;
    if (cancellation_seen) {
        r_runtime_task_cancel(&started.task);
        for (spin = 0U; spin < 1000000U; ++spin) {
            if (r_runtime_darwin_fs_service_testing_signal_count() != signal_count) {
                break;
            }
            (void)sched_yield();
        }
        cancellation_seen = spin < 1000000U;
    }
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    R_TEST_CHECK(cancellation_seen);
    r_std_fs_path_destroy(&path.value);

    path = r_test_fs_path(&allocator, "data");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    started = r_std_fs_metadata_beneath(&root, &path.value, no_deadline);
    r_std_fs_directory_destroy(&root);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(r_test_fs_await_metadata(started, &metadata_result) == 0);
    R_TEST_CHECK(metadata_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(metadata_result.r_payload.r_ok.kind == R_STD_FS_FILE_KIND_REGULAR);
    r_std_fs_directory_destroy(&outside_root);

    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_fs_service_stop();
    root_descriptor = open(temporary_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    outside_descriptor = open(outside_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    R_TEST_CHECK(root_descriptor >= 0);
    R_TEST_CHECK(outside_descriptor >= 0);
    errno = 0;
    R_TEST_CHECK(faccessat(root_descriptor, "cancelled-directory", F_OK, AT_SYMLINK_NOFOLLOW) !=
                     0 &&
                 errno == ENOENT);
    R_TEST_CHECK(r_test_fs_has_staging_entry(root_descriptor) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "data", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "escape", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "rename-source", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "rename-destination", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "nested/a/b", AT_REMOVEDIR) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "nested/a", AT_REMOVEDIR) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "nested", AT_REMOVEDIR) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "ordinary/a", AT_REMOVEDIR) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "ordinary", AT_REMOVEDIR) == 0);
    R_TEST_CHECK(unlinkat(outside_descriptor, "target", 0) == 0);
    R_TEST_CHECK(unlinkat(outside_descriptor, "cross-destination", 0) == 0);
    R_TEST_CHECK(close(outside_descriptor) == 0);
    R_TEST_CHECK(close(root_descriptor) == 0);
    R_TEST_CHECK(rmdir(outside_directory) == 0);
    R_TEST_CHECK(rmdir(temporary_directory) == 0);
    return 0;
}

static int r_test_fs_control_fifo_lifecycle(void) {
    static const RStdFsDeadline no_deadline = {0};
    static const RStdFsOpenFileOptions read_write_existing = {
        R_STD_FS_ACCESS_READ_WRITE,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        1,
    };
    static const RStdFsOpenFileOptions append_existing = {
        R_STD_FS_ACCESS_READ_WRITE,
        R_STD_FS_CREATE_EXISTING,
        0,
        1,
        1,
    };
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;
    RStdFsPathResult path;
    RStdFsFileResult file_result = {0};
    RStdFsU64Result first_result = {0};
    RStdFsU64Result second_result = {0};
    RStdFsVoidResult close_result = {0};
    RStdFsTaskStartResult first_operation;
    RStdFsTaskStartResult second_operation;
    RStdFsTaskStartResult close_operation;
    RStdFsFile file = {0};
    RStdTimeDurationResult short_delay;
    RStdTimeInstantResult now;
    RStdTimeInstantResult deadline;
    RTestFsSeekStartContext start_context;
    RTestFsExecutorStopContext stop_context = {0};
    pthread_t start_thread;
    pthread_t stop_thread;
    char directory_template[] = "/tmp/r-std-fs-control-fifo-XXXXXX";
    char file_path[256];
    char *temporary_directory;
    uint64_t native_sequence;
    uint64_t entry_sequence;
    size_t spin;
    int directory_descriptor;
    int file_descriptor;
    int written;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    R_TEST_CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);
    temporary_directory = mkdtemp(directory_template);
    R_TEST_CHECK(temporary_directory != NULL);
    directory_descriptor = open(temporary_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    R_TEST_CHECK(directory_descriptor >= 0);
    file_descriptor = r_test_fs_create_file(directory_descriptor, "data");
    R_TEST_CHECK(file_descriptor >= 0 && close(file_descriptor) == 0);
    R_TEST_CHECK(close(directory_descriptor) == 0);
    written = snprintf(file_path, sizeof(file_path), "%s/data", temporary_directory);
    R_TEST_CHECK(written > 0 && (size_t)written < sizeof(file_path));
    path = r_test_fs_path(&allocator, file_path);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    first_operation = r_std_fs_open_file(&path.value, read_write_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(first_operation, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    file = file_result.r_payload.r_ok;
    file_result.r_payload.r_ok.storage = NULL;
    file_descriptor = r_library_internal_fs_file_descriptor(&file);
    R_TEST_CHECK(file_descriptor >= 0);
    R_TEST_CHECK(lseek(file_descriptor, (off_t)0, SEEK_CUR) == (off_t)0);

    native_sequence = r_runtime_darwin_fs_service_testing_native_sequence();
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_START, INT64_C(5), no_deadline);
    R_TEST_CHECK(first_operation.is_ok);
    second_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(-2), no_deadline);
    R_TEST_CHECK(second_operation.is_ok);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_C(5));
    R_TEST_CHECK(r_test_fs_await_u64(second_operation, &second_result) == 0);
    R_TEST_CHECK((second_result.r_tag == UINT32_C(0)) &&
                 second_result.r_payload.r_ok == UINT64_C(3));
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_native_sequence() == native_sequence);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    R_TEST_CHECK(lseek(file_descriptor, (off_t)0, SEEK_CUR) == (off_t)0);

    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_START, INT64_MAX, no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) &&
                 first_result.r_payload.r_ok == (uint64_t)INT64_MAX);
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_MAX, no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) &&
                 first_result.r_payload.r_ok == UINT64_MAX - UINT64_C(1));
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(1), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_MAX);
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(1), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK(!(first_result.r_tag == UINT32_C(0)) &&
                 first_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION);
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(0), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_MAX);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_native_sequence() == native_sequence);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    R_TEST_CHECK(lseek(file_descriptor, (off_t)0, SEEK_CUR) == (off_t)0);

    native_sequence = r_runtime_darwin_fs_service_testing_native_sequence();
    r_runtime_darwin_fs_service_testing_pause_after_native(1);
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_END, INT64_C(1), no_deadline);
    R_TEST_CHECK(first_operation.is_ok);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_runtime_darwin_fs_service_testing_native_sequence() != native_sequence) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    short_delay = r_std_time_duration_from_parts(INT64_C(0), UINT32_C(1000000));
    R_TEST_CHECK(short_delay.is_ok);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    deadline = r_std_time_instant_add(now.value, short_delay.value);
    R_TEST_CHECK(deadline.is_ok);
    second_operation = r_std_fs_seek(
        &file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(1), (RStdFsDeadline){1, deadline.value});
    R_TEST_CHECK(second_operation.is_ok);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_runtime_task_state(second_operation.task) == R_RUNTIME_TASK_COMPLETED) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_native_sequence() == native_sequence + 1U);
    R_TEST_CHECK(r_test_fs_await_u64(second_operation, &second_result) == 0);
    R_TEST_CHECK(!(second_result.r_tag == UINT32_C(0)) &&
                 second_result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 second_result.r_payload.r_err.native_code == INT64_C(0));
    r_runtime_darwin_fs_service_testing_pause_after_native(0);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_C(2));
    R_TEST_CHECK(lseek(file_descriptor, (off_t)0, SEEK_CUR) == (off_t)0);
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_END, INT64_C(-2), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK(!(first_result.r_tag == UINT32_C(0)) &&
                 first_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION);
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(0), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_C(2));
    R_TEST_CHECK(lseek(file_descriptor, (off_t)0, SEEK_CUR) == (off_t)0);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(2));
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(1), no_deadline);
    R_TEST_CHECK(!first_operation.is_ok && first_operation.task == NULL &&
                 first_operation.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(file.storage != NULL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(2));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(0), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_C(2));

    (void)memset(&start_context, 0, sizeof(start_context));
    start_context.file = &file;
    start_context.origin = R_STD_FS_SEEK_ORIGIN_CURRENT;
    start_context.offset = INT64_C(1);
    start_context.deadline = no_deadline;
    r_library_internal_fs_control_testing_pause_before_publish(1);
    R_TEST_CHECK(pthread_create(&start_thread, NULL, r_test_fs_seek_start_thread, &start_context) ==
                 0);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_control_testing_publish_reached()) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    close_operation = r_std_fs_close_file(&file, no_deadline);
    R_TEST_CHECK(close_operation.is_ok && file.storage == NULL);
    r_library_internal_fs_control_testing_pause_before_publish(0);
    R_TEST_CHECK(pthread_join(start_thread, NULL) == 0);
    R_TEST_CHECK(start_context.started.is_ok);
    R_TEST_CHECK(r_test_fs_await_u64(start_context.started, &first_result) == 0);
    R_TEST_CHECK(!(first_result.r_tag == UINT32_C(0)) &&
                 first_result.r_payload.r_err.code == R_STD_FS_ERROR_CANCELLED);
    R_TEST_CHECK(r_test_fs_await_void(close_operation, &close_result) == 0);
    R_TEST_CHECK(close_result.r_tag == UINT32_C(0));

    first_operation = r_std_fs_open_file(&path.value, append_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(first_operation, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    file = file_result.r_payload.r_ok;
    file_result.r_payload.r_ok.storage = NULL;
    file_descriptor = r_library_internal_fs_file_descriptor(&file);
    R_TEST_CHECK(file_descriptor >= 0);
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_START, INT64_C(0), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_C(0));
    first_operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(1), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(first_operation, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_C(1));
    R_TEST_CHECK(lseek(file_descriptor, (off_t)0, SEEK_CUR) == (off_t)0);

    native_sequence = r_runtime_darwin_fs_service_testing_native_sequence();
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    (void)memset(&start_context, 0, sizeof(start_context));
    start_context.file = &file;
    start_context.origin = R_STD_FS_SEEK_ORIGIN_CURRENT;
    start_context.offset = INT64_C(1);
    start_context.deadline = no_deadline;
    r_library_internal_fs_control_testing_pause_after_logical_commit(1);
    R_TEST_CHECK(pthread_create(&start_thread, NULL, r_test_fs_seek_start_thread, &start_context) ==
                 0);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_control_testing_logical_commit_reached()) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_native_sequence() == native_sequence);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    close_operation = r_std_fs_close_file(&file, no_deadline);
    R_TEST_CHECK(close_operation.is_ok && file.storage == NULL);
    r_library_internal_fs_control_testing_pause_after_logical_commit(0);
    R_TEST_CHECK(pthread_join(start_thread, NULL) == 0);
    R_TEST_CHECK(start_context.started.is_ok && start_context.started.task != NULL);
    R_TEST_CHECK(r_test_fs_await_u64(start_context.started, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_C(2));
    R_TEST_CHECK(r_test_fs_await_void(close_operation, &close_result) == 0);
    R_TEST_CHECK(close_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_native_sequence() == native_sequence + 1U);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence + 1U);

    first_operation = r_std_fs_open_file(&path.value, read_write_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(first_operation, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    file = file_result.r_payload.r_ok;
    file_result.r_payload.r_ok.storage = NULL;

    (void)memset(&start_context, 0, sizeof(start_context));
    start_context.file = &file;
    start_context.origin = R_STD_FS_SEEK_ORIGIN_START;
    start_context.offset = INT64_C(-1);
    start_context.deadline = no_deadline;
    r_library_internal_fs_control_testing_pause_before_publish(1);
    R_TEST_CHECK(pthread_create(&start_thread, NULL, r_test_fs_seek_start_thread, &start_context) ==
                 0);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_control_testing_publish_reached()) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    (void)memset(&stop_context, 0, sizeof(stop_context));
    R_TEST_CHECK(
        pthread_create(&stop_thread, NULL, r_test_fs_executor_stop_thread, &stop_context) == 0);
    R_TEST_CHECK(r_test_fs_wait_executor_stopping());
    r_library_internal_fs_control_testing_pause_before_publish(0);
    R_TEST_CHECK(pthread_join(start_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(stop_thread, NULL) == 0 && stop_context.stopped);
    R_TEST_CHECK(start_context.started.is_ok && start_context.started.task != NULL);
    R_TEST_CHECK(r_test_fs_await_u64(start_context.started, &first_result) == 0);
    R_TEST_CHECK(!(first_result.r_tag == UINT32_C(0)) &&
                 first_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION);
    R_TEST_CHECK(file.storage != NULL);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);

    (void)memset(&start_context, 0, sizeof(start_context));
    start_context.file = &file;
    start_context.origin = R_STD_FS_SEEK_ORIGIN_CURRENT;
    start_context.offset = INT64_C(1);
    start_context.deadline = no_deadline;
    r_library_internal_fs_control_testing_pause_before_publish(1);
    R_TEST_CHECK(pthread_create(&start_thread, NULL, r_test_fs_seek_start_thread, &start_context) ==
                 0);
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_control_testing_publish_reached()) {
            break;
        }
        (void)sched_yield();
    }
    R_TEST_CHECK(spin < 1000000U);
    (void)memset(&stop_context, 0, sizeof(stop_context));
    R_TEST_CHECK(
        pthread_create(&stop_thread, NULL, r_test_fs_executor_stop_thread, &stop_context) == 0);
    R_TEST_CHECK(!r_library_internal_fs_control_testing_cancel_reached());
    r_library_internal_fs_control_testing_pause_before_publish(0);
    R_TEST_CHECK(pthread_join(start_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(stop_thread, NULL) == 0 && stop_context.stopped);
    R_TEST_CHECK(start_context.started.is_ok && start_context.started.task != NULL);
    R_TEST_CHECK(r_test_fs_await_u64(start_context.started, &first_result) == 0);
    R_TEST_CHECK((first_result.r_tag == UINT32_C(0)) && first_result.r_payload.r_ok == UINT64_C(1));
    R_TEST_CHECK(file.storage != NULL);
    r_std_fs_file_destroy(&file);

    r_std_fs_path_destroy(&path.value);
    r_runtime_darwin_fs_service_stop();
    R_TEST_CHECK(unlink(file_path) == 0);
    R_TEST_CHECK(rmdir(temporary_directory) == 0);
    return 0;
}

static int r_test_fs_control_close_and_iteration(void) {
    static const RStdFsDeadline no_deadline = {0};
    static const RStdFsOpenFileOptions read_write_existing = {
        R_STD_FS_ACCESS_READ_WRITE,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        0,
    };
    static const RStdFsOpenFileOptions read_existing = {
        R_STD_FS_ACCESS_READ,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        0,
    };
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;
    RStdFsPathResult path;
    RStdFsDirectoryResult directory_result = {0};
    RStdFsDirectoryIterResult iterator_result = {0};
    RStdFsDirectoryNextResult next_result = {0};
    RStdFsFileResult file_result = {0};
    RStdFsMetadataResult metadata_result = {0};
    RStdFsU64Result u64_result = {0};
    RStdFsVoidResult void_result = {0};
    RStdFsTaskStartResult operation;
    RStdFsTaskStartResult close_operation;
    RStdFsDirectory root = {0};
    RStdFsDirectory invalid_directory = {0};
    RStdFsDirectoryIter iterator = {0};
    RStdFsFile file = {0};
    RStdTimeInstantResult now;
    RStdTimeDurationResult short_delay;
    RStdTimeInstantResult deadline;
    RStdFsDirectoryIterStorage *malformed_storage;
    RStdFsDirectoryEntry malformed_entry = {0};
    RStdFsError malformed_error = {0};
    struct attrlist attributes;
    struct stat metadata;
    uint8_t native_buffer[4096];
    char path_template[] = "/tmp/r-std-fs-control-XXXXXX";
    char *temporary_directory;
    uint64_t entry_sequence;
    uint64_t native_sequence;
    uint64_t signal_count;
    size_t cached_offset;
    int64_t cached_index;
    size_t spin;
    uint32_t attempt;
    _Bool early;
    unsigned int seen_entries;
    int root_descriptor;
    int file_descriptor;
    int setup_descriptor;
    int native_count;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    R_TEST_CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);
    temporary_directory = mkdtemp(path_template);
    R_TEST_CHECK(temporary_directory != NULL);
    setup_descriptor = open(temporary_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    R_TEST_CHECK(setup_descriptor >= 0);
    file_descriptor = r_test_fs_create_file(setup_descriptor, "stream");
    R_TEST_CHECK(file_descriptor >= 0);
    R_TEST_CHECK(close(file_descriptor) == 0);
    file_descriptor = r_test_fs_create_file(setup_descriptor, "second");
    R_TEST_CHECK(file_descriptor >= 0);
    R_TEST_CHECK(close(file_descriptor) == 0);
    R_TEST_CHECK(mkdirat(setup_descriptor, "sub", S_IRWXU) == 0);
    R_TEST_CHECK(close(setup_descriptor) == 0);

    path = r_test_fs_path(&allocator, temporary_directory);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    operation = r_std_fs_open_directory(&path.value, no_deadline);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(r_test_fs_await_directory(operation, &directory_result) == 0);
    R_TEST_CHECK(directory_result.r_tag == UINT32_C(0));
    root = directory_result.r_payload.r_ok;
    directory_result.r_payload.r_ok.storage = NULL;
    root_descriptor = r_library_internal_fs_directory_descriptor(&root);
    R_TEST_CHECK(root_descriptor >= 0);

    path = r_test_fs_path(&allocator, "stream");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    operation = r_std_fs_open_file_beneath(&root, &path.value, read_write_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(operation, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    file = file_result.r_payload.r_ok;
    file_result.r_payload.r_ok.storage = NULL;
    file_descriptor = r_library_internal_fs_file_descriptor(&file);
    R_TEST_CHECK(file_descriptor >= 0);

    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_START, INT64_C(-1), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(operation, &u64_result) == 0);
    R_TEST_CHECK(!(u64_result.r_tag == UINT32_C(0)) &&
                 u64_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION &&
                 u64_result.r_payload.r_err.native_code == INT64_C(0));
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);

    operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_END, INT64_C(0), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(operation, &u64_result) == 0);
    R_TEST_CHECK((u64_result.r_tag == UINT32_C(0)) && u64_result.r_payload.r_ok == UINT64_C(1));
    operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_CURRENT, INT64_C(-1), no_deadline);
    R_TEST_CHECK(r_test_fs_await_u64(operation, &u64_result) == 0);
    R_TEST_CHECK((u64_result.r_tag == UINT32_C(0)) && u64_result.r_payload.r_ok == UINT64_C(0));
    operation = r_std_fs_flush(&file, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    /* R-SLIB-FS-0014: every sync level reaches the lane and succeeds on a regular file; a level
       outside the enum is refused before any native entry. */
    operation = r_std_fs_sync(&file, R_STD_FS_SYNC_LEVEL_BARRIER, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    operation = r_std_fs_sync(&file, R_STD_FS_SYNC_LEVEL_DEVICE, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    operation = r_std_fs_sync(&file, R_STD_FS_SYNC_LEVEL_MEDIA, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() != entry_sequence);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    operation = r_std_fs_sync(&file, (RStdFsSyncLevel)7, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    operation = r_std_fs_close_file(&file, no_deadline);
    R_TEST_CHECK(!operation.is_ok && operation.task == NULL &&
                 operation.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(file.storage != NULL);
    R_TEST_CHECK(r_library_internal_fs_file_descriptor(&file) == file_descriptor);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));

    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    operation = r_std_fs_close_file(&file, (RStdFsDeadline){1, now.value});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage == NULL);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT);
    errno = 0;
    R_TEST_CHECK(fcntl(file_descriptor, F_GETFD) < 0 && errno == EBADF);

    operation = r_std_fs_open_file_beneath(&root, &path.value, read_write_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(operation, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    file = file_result.r_payload.r_ok;
    file_result.r_payload.r_ok.storage = NULL;
    file_descriptor = r_library_internal_fs_file_descriptor(&file);
    R_TEST_CHECK(close(file_descriptor) == 0);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    operation = r_std_fs_close_file(&file, (RStdFsDeadline){1, now.value});
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_CLOSED &&
                 void_result.r_payload.r_err.native_code == EBADF);

    operation = r_std_fs_open_file_beneath(&root, &path.value, read_write_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(operation, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    file = file_result.r_payload.r_ok;
    file_result.r_payload.r_ok.storage = NULL;
    file_descriptor = r_library_internal_fs_file_descriptor(&file);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    operation = r_std_fs_close_file(&file, (RStdFsDeadline){1, now.value});
    R_TEST_CHECK(operation.is_ok && file.storage == NULL);
    R_TEST_CHECK(close(file_descriptor) == 0);
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 void_result.r_payload.r_err.native_code == INT64_C(0));

    operation = r_std_fs_open_file_beneath(&root, &path.value, read_write_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(operation, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    file = file_result.r_payload.r_ok;
    file_result.r_payload.r_ok.storage = NULL;
    file_descriptor = r_library_internal_fs_file_descriptor(&file);
    R_TEST_CHECK(file_descriptor >= 0);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_START, INT64_C(0), no_deadline);
    R_TEST_CHECK(!operation.is_ok && operation.task == NULL &&
                 operation.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(file.storage != NULL);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    R_TEST_CHECK(close(file_descriptor) == 0);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    operation = r_std_fs_seek(
        &file, R_STD_FS_SEEK_ORIGIN_START, INT64_C(0), (RStdFsDeadline){1, now.value});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage != NULL);
    R_TEST_CHECK(r_test_fs_await_u64(operation, &u64_result) == 0);
    R_TEST_CHECK(!(u64_result.r_tag == UINT32_C(0)) &&
                 u64_result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 u64_result.r_payload.r_err.native_code == INT64_C(0));
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    operation = r_std_fs_seek(&file, R_STD_FS_SEEK_ORIGIN_END, INT64_C(0), no_deadline);
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage != NULL);
    R_TEST_CHECK(r_test_fs_await_u64(operation, &u64_result) == 0);
    R_TEST_CHECK(!(u64_result.r_tag == UINT32_C(0)) &&
                 u64_result.r_payload.r_err.code == R_STD_FS_ERROR_CLOSED &&
                 u64_result.r_payload.r_err.native_code == EBADF);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    operation = r_std_fs_flush(&file, no_deadline);
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && file.storage != NULL);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_CLOSED &&
                 void_result.r_payload.r_err.native_code == EBADF);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    r_std_fs_file_destroy(&file);

    operation = r_std_fs_open_file_beneath(&root, &path.value, read_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(operation, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    operation = r_std_fs_flush(&file_result.r_payload.r_ok, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    operation =
        r_std_fs_sync(&file_result.r_payload.r_ok, R_STD_FS_SYNC_LEVEL_BARRIER, no_deadline);
    R_TEST_CHECK(r_test_fs_await_void(operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(1) &&
                 void_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    r_std_fs_file_destroy(&file_result.r_payload.r_ok);
    r_std_fs_path_destroy(&path.value);

    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME;
    do {
        native_count = getattrlistbulk(root_descriptor,
                                       &attributes,
                                       native_buffer,
                                       sizeof(native_buffer),
                                       FSOPT_PACK_INVAL_ATTRS);
        R_TEST_CHECK(native_count >= 0);
    } while (native_count != 0);

    operation = r_std_fs_iterate(&root, no_deadline);
    R_TEST_CHECK(r_test_fs_await_iterator(operation, &iterator_result) == 0);
    R_TEST_CHECK((iterator_result.r_tag == UINT32_C(0)) &&
                 iterator_result.r_payload.r_ok.storage != NULL);
    iterator = iterator_result.r_payload.r_ok;
    iterator_result.r_payload.r_ok.storage = NULL;
    seen_entries = 0U;
    for (;;) {
        operation = r_std_fs_next(&iterator, no_deadline);
        R_TEST_CHECK(operation.is_ok && operation.task != NULL && iterator.storage == NULL);
        R_TEST_CHECK(r_test_fs_await_next(operation, &next_result) == 0);
        if (next_result.kind == R_STD_FS_DIRECTORY_NEXT_END) {
            break;
        }
        R_TEST_CHECK(next_result.kind == R_STD_FS_DIRECTORY_NEXT_ENTRY);
        if (r_test_fs_path_equals(&next_result.entry.name, "stream")) {
            seen_entries |= 1U;
        } else if (r_test_fs_path_equals(&next_result.entry.name, "second")) {
            seen_entries |= 2U;
        } else if (r_test_fs_path_equals(&next_result.entry.name, "sub")) {
            seen_entries |= 4U;
            R_TEST_CHECK(next_result.entry.kind == R_STD_FS_FILE_KIND_DIRECTORY);
        }
        r_library_internal_fs_directory_iter_move(&iterator, &next_result.iterator);
        r_std_fs_directory_entry_destroy(&next_result.entry);
    }
    R_TEST_CHECK(seen_entries == 7U);
    R_TEST_CHECK(iterator.storage == NULL);

    /* L41-2: a read that ends before its deadline expires wins over the expiry the worker sees
       while it waits after the read (R-SLIB-ASYNC-0007). The read is late when the worker starts
       it after the deadline, which a loaded machine may do; such a round proves nothing and is
       retried with a fresh iterator and a longer deadline. */
    for (attempt = 0U;; ++attempt) {
        RStdTimeInstantResult read_done;

        R_TEST_CHECK(attempt < 10U);
        operation = r_std_fs_iterate(&root, no_deadline);
        R_TEST_CHECK(r_test_fs_await_iterator(operation, &iterator_result) == 0);
        R_TEST_CHECK((iterator_result.r_tag == UINT32_C(0)));
        iterator = iterator_result.r_payload.r_ok;
        iterator_result.r_payload.r_ok.storage = NULL;
        short_delay = r_std_time_duration_from_parts(INT64_C(0), UINT32_C(1000000) << attempt);
        R_TEST_CHECK(short_delay.is_ok);
        now = r_std_time_monotonic_now();
        R_TEST_CHECK(now.is_ok);
        deadline = r_std_time_instant_add(now.value, short_delay.value);
        R_TEST_CHECK(deadline.is_ok);
        native_sequence = r_runtime_darwin_fs_service_testing_native_sequence();
        signal_count = r_runtime_darwin_fs_service_testing_signal_count();
        r_runtime_darwin_fs_service_testing_pause_after_native(1);
        operation = r_std_fs_next(&iterator, (RStdFsDeadline){1, deadline.value});
        R_TEST_CHECK(operation.is_ok && iterator.storage == NULL);
        for (spin = 0U; spin < 1000000U; ++spin) {
            if (r_runtime_darwin_fs_service_testing_native_sequence() != native_sequence) {
                break;
            }
            (void)sched_yield();
        }
        read_done = r_std_time_monotonic_now();
        R_TEST_CHECK(read_done.is_ok);
        early = (spin < 1000000U) && r_test_fs_instant_before(read_done.value, deadline.value);
        for (spin = 0U; early && (spin < 1000000U); ++spin) {
            if (r_runtime_darwin_fs_service_testing_signal_count() != signal_count) {
                break;
            }
            (void)sched_yield();
        }
        r_runtime_darwin_fs_service_testing_pause_after_native(0);
        R_TEST_CHECK(!early || (spin < 1000000U));
        R_TEST_CHECK(r_test_fs_await_next(operation, &next_result) == 0);
        if (early) {
            R_TEST_CHECK(next_result.kind == R_STD_FS_DIRECTORY_NEXT_ENTRY &&
                         next_result.iterator.storage != NULL);
            r_library_internal_fs_directory_iter_move(&iterator, &next_result.iterator);
            r_std_fs_directory_entry_destroy(&next_result.entry);
            break;
        }
        /* A late or withdrawn read: either outcome is linearizable. */
        R_TEST_CHECK((next_result.kind == R_STD_FS_DIRECTORY_NEXT_ENTRY) ||
                     ((next_result.kind == R_STD_FS_DIRECTORY_NEXT_FAILED) &&
                      (next_result.error.code == R_STD_FS_ERROR_TIMED_OUT)));
        if (next_result.kind == R_STD_FS_DIRECTORY_NEXT_ENTRY) {
            r_std_fs_directory_entry_destroy(&next_result.entry);
        }
        r_library_internal_fs_directory_iter_move(&iterator, &next_result.iterator);
        r_std_fs_directory_iter_destroy(&iterator);
    }
    R_TEST_CHECK(iterator.storage->cache.data != NULL);
    cached_offset = iterator.storage->cache_offset;
    cached_index = iterator.storage->cache_index;
    r_runtime_allocator_set_failure(&allocator, UINT64_C(2));
    operation = r_std_fs_next(&iterator, no_deadline);
    R_TEST_CHECK(operation.is_ok && iterator.storage == NULL);
    R_TEST_CHECK(r_test_fs_await_next(operation, &next_result) == 0);
    R_TEST_CHECK(next_result.kind == R_STD_FS_DIRECTORY_NEXT_FAILED &&
                 next_result.error.code == R_STD_FS_ERROR_RESOURCE_EXHAUSTED);
    r_library_internal_fs_directory_iter_move(&iterator, &next_result.iterator);
    R_TEST_CHECK(iterator.storage->cache_offset == cached_offset);
    R_TEST_CHECK(iterator.storage->cache_index == cached_index);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    operation = r_std_fs_next(&iterator, no_deadline);
    R_TEST_CHECK(r_test_fs_await_next(operation, &next_result) == 0);
    R_TEST_CHECK(next_result.kind == R_STD_FS_DIRECTORY_NEXT_ENTRY);
    r_library_internal_fs_directory_iter_move(&iterator, &next_result.iterator);
    r_std_fs_directory_entry_destroy(&next_result.entry);
    r_std_fs_directory_iter_destroy(&iterator);

    operation = r_std_fs_iterate(&root, no_deadline);
    R_TEST_CHECK(r_test_fs_await_iterator(operation, &iterator_result) == 0);
    R_TEST_CHECK((iterator_result.r_tag == UINT32_C(0)));
    iterator = iterator_result.r_payload.r_ok;
    iterator_result.r_payload.r_ok.storage = NULL;
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    operation = r_std_fs_next(&iterator, no_deadline);
    R_TEST_CHECK(!operation.is_ok && operation.task == NULL &&
                 operation.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(iterator.storage != NULL);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    file_descriptor = iterator.storage->descriptor;
    R_TEST_CHECK(file_descriptor >= 0 && close(file_descriptor) == 0);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    operation = r_std_fs_next(&iterator, (RStdFsDeadline){1, now.value});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && iterator.storage == NULL);
    R_TEST_CHECK(r_test_fs_await_next(operation, &next_result) == 0);
    R_TEST_CHECK(next_result.kind == R_STD_FS_DIRECTORY_NEXT_FAILED &&
                 next_result.error.code == R_STD_FS_ERROR_TIMED_OUT &&
                 next_result.error.native_code == INT64_C(0) &&
                 next_result.iterator.storage != NULL);
    r_library_internal_fs_directory_iter_move(&iterator, &next_result.iterator);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    operation = r_std_fs_next(&iterator, no_deadline);
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && iterator.storage == NULL);
    R_TEST_CHECK(r_test_fs_await_next(operation, &next_result) == 0);
    R_TEST_CHECK(next_result.kind == R_STD_FS_DIRECTORY_NEXT_FAILED &&
                 next_result.error.code == R_STD_FS_ERROR_CLOSED &&
                 next_result.error.native_code == EBADF && next_result.iterator.storage != NULL);
    r_library_internal_fs_directory_iter_move(&iterator, &next_result.iterator);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    r_std_fs_directory_iter_destroy(&iterator);

    path = r_test_fs_path(&allocator, temporary_directory);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    operation = r_std_fs_open_directory(&path.value, no_deadline);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(r_test_fs_await_directory(operation, &directory_result) == 0);
    R_TEST_CHECK(directory_result.r_tag == UINT32_C(0));
    invalid_directory = directory_result.r_payload.r_ok;
    directory_result.r_payload.r_ok.storage = NULL;
    file_descriptor = r_library_internal_fs_directory_descriptor(&invalid_directory);
    R_TEST_CHECK(file_descriptor >= 0 && close(file_descriptor) == 0);
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    operation = r_std_fs_iterate(&invalid_directory, (RStdFsDeadline){1, now.value});
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && invalid_directory.storage != NULL);
    R_TEST_CHECK(r_test_fs_await_iterator(operation, &iterator_result) == 0);
    R_TEST_CHECK(!(iterator_result.r_tag == UINT32_C(0)) &&
                 iterator_result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 iterator_result.r_payload.r_err.native_code == INT64_C(0));
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    operation = r_std_fs_iterate(&invalid_directory, no_deadline);
    R_TEST_CHECK(operation.is_ok && operation.task != NULL && invalid_directory.storage != NULL);
    R_TEST_CHECK(r_test_fs_await_iterator(operation, &iterator_result) == 0);
    R_TEST_CHECK(!(iterator_result.r_tag == UINT32_C(0)) &&
                 iterator_result.r_payload.r_err.code == R_STD_FS_ERROR_CLOSED &&
                 iterator_result.r_payload.r_err.native_code == EBADF);
    R_TEST_CHECK(r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence);
    r_std_fs_directory_destroy(&invalid_directory);

    malformed_storage = r_library_internal_fs_directory_iter_reserve(&allocator);
    R_TEST_CHECK(malformed_storage != NULL);
    R_TEST_CHECK(r_runtime_allocator_allocate(
                     &allocator, 32U, _Alignof(unsigned char), &malformed_storage->cache.data) ==
                 R_RUNTIME_ALLOCATION_OK);
    (void)memset(malformed_storage->cache.data, 0, 32U);
    malformed_storage->cache.allocator = &allocator;
    malformed_storage->cache.capacity = 32U;
    malformed_storage->cache.entry_count = 1;
    R_TEST_CHECK(!r_library_internal_fs_testing_parse_directory_entry(
        malformed_storage, &malformed_entry, &malformed_error));
    R_TEST_CHECK(malformed_error.code == R_STD_FS_ERROR_OTHER);
    R_TEST_CHECK(malformed_storage->cache_offset == 0U && malformed_storage->cache_index == 0);
    r_std_fs_directory_entry_destroy(&malformed_entry);
    r_library_internal_fs_directory_iter_storage_release(malformed_storage);

    path = r_test_fs_path(&allocator, "stream");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    operation = r_std_fs_open_file_beneath(&root, &path.value, read_existing, no_deadline);
    R_TEST_CHECK(r_test_fs_await_file(operation, &file_result) == 0);
    R_TEST_CHECK(file_result.r_tag == UINT32_C(0));
    file = file_result.r_payload.r_ok;
    file_result.r_payload.r_ok.storage = NULL;
    file_descriptor = r_library_internal_fs_file_descriptor(&file);
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    operation = r_std_fs_file_metadata(&file, no_deadline);
    R_TEST_CHECK(operation.is_ok);
    close_operation = r_std_fs_close_file(&file, no_deadline);
    R_TEST_CHECK(close_operation.is_ok && file.storage == NULL);
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    R_TEST_CHECK(r_test_fs_await_metadata(operation, &metadata_result) == 0);
    R_TEST_CHECK(metadata_result.r_tag != UINT32_C(0) &&
                 metadata_result.r_payload.r_err.code == R_STD_FS_ERROR_CANCELLED);
    R_TEST_CHECK(r_test_fs_await_void(close_operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    errno = 0;
    R_TEST_CHECK(fcntl(file_descriptor, F_GETFD) < 0 && errno == EBADF);
    r_std_fs_path_destroy(&path.value);

    path = r_test_fs_path(&allocator, "second");
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    operation = r_std_fs_metadata_beneath(&root, &path.value, no_deadline);
    R_TEST_CHECK(operation.is_ok);
    close_operation = r_std_fs_close_directory(&root, no_deadline);
    R_TEST_CHECK(close_operation.is_ok && root.storage == NULL);
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    R_TEST_CHECK(r_test_fs_await_metadata(operation, &metadata_result) == 0);
    R_TEST_CHECK(metadata_result.r_tag != UINT32_C(0) &&
                 metadata_result.r_payload.r_err.code == R_STD_FS_ERROR_CANCELLED);
    R_TEST_CHECK(r_test_fs_await_void(close_operation, &void_result) == 0);
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    errno = 0;
    R_TEST_CHECK(fcntl(root_descriptor, F_GETFD) < 0 && errno == EBADF);
    r_std_fs_path_destroy(&path.value);

    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_fs_service_stop();
    setup_descriptor = open(temporary_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    R_TEST_CHECK(setup_descriptor >= 0);
    R_TEST_CHECK(fstatat(setup_descriptor, "stream", &metadata, 0) == 0);
    R_TEST_CHECK(unlinkat(setup_descriptor, "stream", 0) == 0);
    R_TEST_CHECK(unlinkat(setup_descriptor, "second", 0) == 0);
    R_TEST_CHECK(unlinkat(setup_descriptor, "sub", AT_REMOVEDIR) == 0);
    R_TEST_CHECK(close(setup_descriptor) == 0);
    R_TEST_CHECK(rmdir(temporary_directory) == 0);
    return 0;
}

/* Timestamp option lowering must preserve the existing native metadata layout. */
typedef struct RTestLegacySystemTimeOption {
    _Bool has_value;
    RStdTimeSystemTime value;
} RTestLegacySystemTimeOption;
_Static_assert(sizeof(RStdFsSystemTimeOption) == sizeof(RTestLegacySystemTimeOption),
               "timestamp option size changed");
_Static_assert(_Alignof(RStdFsSystemTimeOption) == _Alignof(RTestLegacySystemTimeOption),
               "timestamp option alignment changed");
_Static_assert(offsetof(RStdFsSystemTimeOption, r_payload) ==
                   offsetof(RTestLegacySystemTimeOption, value),
               "timestamp option payload offset changed");

int main(void) {
    R_TEST_CHECK(r_test_fs_move_initialize_overwrites_uninitialized_storage() == 0);
    R_TEST_CHECK(r_test_fs_position_early_unregister_rejected() == 0);
    R_TEST_CHECK(r_test_fs_position_activation_close_race() == 0);
    R_TEST_CHECK(r_test_fs_position_repeated_cancel_and_queue_progress() == 0);
    R_TEST_CHECK(r_test_fs_validation_precedence() == 0);
    R_TEST_CHECK(r_test_fs_round_trip_clone_and_allocation() == 0);
    R_TEST_CHECK(r_test_fs_join_and_absolute() == 0);
    R_TEST_CHECK(r_test_fs_beneath_lexical_validation() == 0);
    R_TEST_CHECK(r_test_fs_send_sync_observation() == 0);
    R_TEST_CHECK(r_test_fs_error_and_options() == 0);
    R_TEST_CHECK(r_test_fs_persistent_payload_close() == 0);
    R_TEST_CHECK(r_test_fs_close_deadline_and_cancel_races() == 0);
    R_TEST_CHECK(r_test_fs_async_open_lifecycle() == 0);
    R_TEST_CHECK(r_test_fs_async_namespace_lifecycle() == 0);
    R_TEST_CHECK(r_test_fs_control_fifo_lifecycle() == 0);
    R_TEST_CHECK(r_test_fs_control_close_and_iteration() == 0);
    return 0;
}
