#include "r_std_format.h"
#include "r_std_io.h"

#include <stddef.h>

static RStdIoTaskStartResult r_test_close_input(RStdIoInput *stream, RStdIoDeadline deadline);
#define r_std_io_close_input r_test_close_input
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_std_io_close_input

static unsigned r_test_close_calls;
static RRuntimeDarwinIoHandle *r_test_original_input;

static RStdIoTaskStartResult r_test_close_input(RStdIoInput *stream, RStdIoDeadline deadline) {
    ++r_test_close_calls;
    if (r_test_close_calls == 1U) {
        r_test_original_input = stream->handle;
        const RStdIoTaskStartResult failed = {0, NULL, R_STD_ASYNC_START_ALLOCATION_FAILED};
        return failed;
    }
    if (stream->handle != r_test_original_input) {
        const RStdIoTaskStartResult failed = {0, NULL, R_STD_ASYNC_START_RUNTIME_STOPPING};
        return failed;
    }
    return r_std_io_close_input(stream, deadline);
}

int main(int argc, char *argv[]) {
    const int result = r_generated_main(argc, argv);
    return result != 0 ? result : r_test_close_calls == 2U ? 0 : 20;
}
