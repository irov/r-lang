#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_task.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Allocation failures in the streaming JSON reader over a file and a TCP stream. The program
 * runs one reader per argument after the two document files; the wrapper arms the failure of
 * one allocation attempt when the program prepares a reader task and disarms it when the
 * program's await of that reader ends, so the failure falls anywhere from the reader's start to
 * its terminal outcome. The first pass records the attempts of each transport without a
 * failure; the second fails each attempt in turn (and one beyond the last for the well-formed
 * documents). Every run must end in its baseline outcome or in the error of the failure: alloc
 * error (99) or start error (97), and json error (98) for the malformed file. A failure that
 * was not reached leaves the baseline outcome; 97 and 99 occur only after the failure was
 * reached.
 */
RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step);
RRuntimeTaskStartResult r_test_commit_inline(RRuntimeTask **transaction,
                                             RRuntimeTaskPayloadInitializeFn initialize,
                                             const void *context,
                                             size_t step_stack_bytes);
RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result_storage);

#define r_runtime_task_resumable_start_prepare r_test_prepare
#define r_runtime_task_start_commit_initialize_inline r_test_commit_inline
#define r_runtime_task_execution_await r_test_await
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_task_execution_await
#undef r_runtime_task_start_commit_initialize_inline
#undef r_runtime_task_resumable_start_prepare

enum {
    R_TEST_MAX_RUNS = 1024,
    R_TEST_MALFORMED = 0,
    R_TEST_FILE,
    R_TEST_TCP,
    R_TEST_KINDS
};

typedef struct RTestRun {
    unsigned kind;
    uint64_t failure;
    uint64_t attempts;
    int32_t result;
    _Bool finished;
} RTestRun;

static char r_test_kind_malformed[] = "malformed";
static char r_test_kind_file[] = "file";
static char r_test_kind_tcp[] = "tcp";
static char *const r_test_kind_names[R_TEST_KINDS] = {
    r_test_kind_malformed,
    r_test_kind_file,
    r_test_kind_tcp,
};
static const int32_t r_test_baseline_results[R_TEST_KINDS] = {98, 0, 0};

static RTestRun r_test_runs[R_TEST_MAX_RUNS];
static char *r_test_arguments[R_TEST_MAX_RUNS + 4U];
static unsigned r_test_planned;
static _Atomic unsigned r_test_prepares;
static _Atomic unsigned r_test_started;
/* The run whose failure is armed, or -1, and the reader task it committed. */
static _Atomic int r_test_active = -1;
static RRuntimeTask *_Atomic r_test_reader;
static _Atomic _Bool r_test_valid = 1;

static void r_test_finish(int run, int32_t result) {
    RRuntimeAllocator *allocator = r_runtime_hosted_allocator();
    if (run < 0 || allocator == NULL) {
        atomic_store(&r_test_valid, 0);
        return;
    }
    r_test_runs[run].attempts = r_runtime_allocator_attempt_count(allocator);
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    r_test_runs[run].result = result;
    r_test_runs[run].finished = 1;
    atomic_store(&r_test_reader, NULL);
    atomic_store(&r_test_active, -1);
}

RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step) {
    RRuntimeAllocator *allocator = r_runtime_hosted_allocator();
    unsigned run;
    RRuntimeTaskPrepareResult result;
    /* The first task is main; main starts no task but the readers. */
    if (atomic_fetch_add(&r_test_prepares, 1U) == 0U) {
        return r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    }
    run = atomic_fetch_add(&r_test_started, 1U);
    if (run >= r_test_planned || allocator == NULL || atomic_load(&r_test_active) >= 0) {
        atomic_store(&r_test_valid, 0);
        return r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    }
    atomic_store(&r_test_active, (int)run);
    r_runtime_allocator_set_failure(allocator, r_test_runs[run].failure);
    result = r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    if (result.status != R_RUNTIME_TASK_START_OK) {
        /* The program reports the start error of the reader as 97. */
        r_test_finish((int)run, INT32_C(97));
    }
    return result;
}

RRuntimeTaskStartResult r_test_commit_inline(RRuntimeTask **transaction,
                                             RRuntimeTaskPayloadInitializeFn initialize,
                                             const void *context,
                                             size_t step_stack_bytes) {
    const int run = atomic_load(&r_test_active);
    const RRuntimeTaskStartResult result = r_runtime_task_start_commit_initialize_inline(
        transaction, initialize, context, step_stack_bytes);
    if (run >= 0) {
        if (result.status != R_RUNTIME_TASK_START_OK) {
            r_test_finish(run, INT32_C(97));
        } else {
            atomic_store(&r_test_reader, result.task);
        }
    }
    return result;
}

RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result_storage) {
    RRuntimeTask *const awaited = task == NULL ? NULL : *task;
    const RRuntimeTaskExecutionAwaitStatus status =
        r_runtime_task_execution_await(execution, task, result_storage);
    if (awaited != NULL && awaited == atomic_load(&r_test_reader) &&
        status != R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
        /* The readers catch their checked errors, so only a panic ends one without a value. */
        int32_t result = INT32_C(-1);
        if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_OK) {
            result = *(const int32_t *)result_storage;
        }
        r_test_finish(atomic_load(&r_test_active), result);
    }
    return status;
}

/* Runs the program once over the planned runs; the reader kinds become its arguments. */
static int r_test_pass(char *program, char *malformed, char *document) {
    atomic_store(&r_test_prepares, 0U);
    atomic_store(&r_test_started, 0U);
    atomic_store(&r_test_active, -1);
    atomic_store(&r_test_reader, NULL);
    r_test_arguments[0] = program;
    r_test_arguments[1] = malformed;
    r_test_arguments[2] = document;
    for (unsigned run = 0U; run < r_test_planned; ++run) {
        r_test_runs[run].attempts = 0U;
        r_test_runs[run].result = INT32_C(-2);
        r_test_runs[run].finished = 0;
        r_test_arguments[run + 3U] = r_test_kind_names[r_test_runs[run].kind];
    }
    r_test_arguments[r_test_planned + 3U] = NULL;
    return r_generated_main((int)r_test_planned + 3, r_test_arguments);
}

static _Bool r_test_plan(unsigned kind, uint64_t failure) {
    if (r_test_planned >= R_TEST_MAX_RUNS) {
        return 0;
    }
    r_test_runs[r_test_planned].kind = kind;
    r_test_runs[r_test_planned].failure = failure;
    r_test_planned += 1U;
    return 1;
}

static _Bool r_test_create(char *path, _Bool malformed) {
    static const char source[] = "{\"id\":\"18446744073709551615\",\"text\":\"hello\"}";
    const int descriptor = mkstemp(path);
    _Bool written;
    if (descriptor < 0) {
        return 0;
    }
    written = write(descriptor, source, sizeof(source) - 1U) == (ssize_t)(sizeof(source) - 1U);
    if (written && malformed) {
        written = write(descriptor, "!", 1U) == 1;
    }
    return close(descriptor) == 0 && written;
}

static int r_test_report(const char *what, unsigned run) {
    (void)fprintf(stderr,
                  "%s: run %u (%s), failure %llu, attempts %llu, result %d\n",
                  what,
                  run,
                  r_test_kind_names[r_test_runs[run].kind],
                  (unsigned long long)r_test_runs[run].failure,
                  (unsigned long long)r_test_runs[run].attempts,
                  (int)r_test_runs[run].result);
    return 1;
}

static int r_test_sweep(char *program, char *malformed, char *document) {
    uint64_t baseline[R_TEST_KINDS];
    int status;
    r_test_planned = 0U;
    for (unsigned kind = 0U; kind < R_TEST_KINDS; ++kind) {
        (void)r_test_plan(kind, UINT64_C(0));
    }
    status = r_test_pass(program, malformed, document);
    if (status != 0 || !atomic_load(&r_test_valid)) {
        (void)fprintf(stderr, "baseline pass failed with status %d\n", status);
        return 1;
    }
    for (unsigned run = 0U; run < R_TEST_KINDS; ++run) {
        if (!r_test_runs[run].finished || r_test_runs[run].attempts == 0U ||
            r_test_runs[run].result != r_test_baseline_results[run]) {
            return r_test_report("baseline", run);
        }
        baseline[run] = r_test_runs[run].attempts;
    }
    r_test_planned = 0U;
    for (unsigned kind = 0U; kind < R_TEST_KINDS; ++kind) {
        /* A well-formed document is also read with a failure one past its last attempt. */
        const uint64_t last = kind == R_TEST_MALFORMED ? baseline[kind] : baseline[kind] + 1U;
        for (uint64_t failure = 1U; failure <= last; ++failure) {
            if (!r_test_plan(kind, failure)) {
                (void)fprintf(stderr, "more than %u planned runs\n", (unsigned)R_TEST_MAX_RUNS);
                return 1;
            }
        }
    }
    status = r_test_pass(program, malformed, document);
    if (status != 0 || !atomic_load(&r_test_valid)) {
        (void)fprintf(stderr, "failure pass failed with status %d\n", status);
        return 1;
    }
    for (unsigned run = 0U; run < r_test_planned; ++run) {
        const RTestRun *entry = &r_test_runs[run];
        const int32_t expected = r_test_baseline_results[entry->kind];
        const _Bool reached = entry->attempts >= entry->failure;
        if (!entry->finished) {
            return r_test_report("unfinished", run);
        }
        if (entry->result != expected && entry->result != INT32_C(97) &&
            entry->result != INT32_C(99)) {
            return r_test_report("unexpected outcome", run);
        }
        if (!reached && entry->result != expected) {
            return r_test_report("outcome without failure", run);
        }
        if (reached != (entry->result != expected) && entry->kind != R_TEST_MALFORMED) {
            /* A reached failure of a well-formed document is reported, never absorbed. */
            return r_test_report("failure not reported", run);
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    char malformed[] = "/private/tmp/r-json-source-XXXXXX";
    char document[] = "/private/tmp/r-json-source-XXXXXX";
    int status = 1;
    (void)argc;
    if (r_test_create(malformed, 1)) {
        if (r_test_create(document, 0)) {
            status = r_test_sweep(argv[0], malformed, document);
            (void)unlink(document);
        }
        (void)unlink(malformed);
    }
    return status;
}
