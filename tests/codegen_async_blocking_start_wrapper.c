#include "r_std_async.h"

/* R-SLIB-ASYNC-0017: the first two starts of std.async::blocking fail before the library sees
   the staged arguments; later starts reach the blocking call pool. */
RStdAsyncStartResult r_test_blocking(RRuntimeTypeInfo payload_type,
                                     RRuntimeTypeInfo result_type,
                                     RStdAsyncBlockingEntryFn entry,
                                     void *staged_payload);

#define r_std_async_blocking r_test_blocking
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_async_blocking

static unsigned r_test_blocking_starts;

RStdAsyncStartResult r_test_blocking(RRuntimeTypeInfo payload_type,
                                     RRuntimeTypeInfo result_type,
                                     RStdAsyncBlockingEntryFn entry,
                                     void *staged_payload) {
    RStdAsyncStartResult failed = {0};

    r_test_blocking_starts += 1U;
    if (r_test_blocking_starts == 1U) {
        failed.error = R_STD_ASYNC_START_ALLOCATION_FAILED;
        return failed;
    }
    if (r_test_blocking_starts == 2U) {
        failed.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return failed;
    }
    return r_std_async_blocking(payload_type, result_type, entry, staged_payload);
}

int main(int argc, char *argv[]) {
    int status = r_generated_main(argc, argv);

    return (status == 0) && (r_test_blocking_starts == 3U) ? 0 : (status == 0 ? 90 : status);
}
