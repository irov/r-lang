#include "r_std_thread.h"

#include <stddef.h>
#include <stdint.h>

static void r_test_thread_detach(RStdThreadJoinHandle *handle);
static void r_test_thread_handle_destroy(RStdThreadJoinHandle *handle);

#define r_library_internal_thread_handle_destroy r_test_thread_handle_destroy
#define r_std_thread_detach r_test_thread_detach
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_std_thread_detach
#undef r_library_internal_thread_handle_destroy

static max_align_t r_test_descriptor_storage;
static size_t r_test_detach_count;

static void r_test_thread_detach(RStdThreadJoinHandle *handle) {
    if ((handle != NULL) && (handle->descriptor != NULL)) {
        handle->descriptor = NULL;
        r_test_detach_count += 1U;
    }
}

static void r_test_thread_handle_destroy(RStdThreadJoinHandle *handle) {
    if (handle != NULL) {
        handle->descriptor = NULL;
    }
}

int main(int argc, char *argv[]) {
    RStdThreadJoinHandle handle = {
        .descriptor = (RStdThreadDescriptor *)(void *)&r_test_descriptor_storage,
    };

    if (!r_runtime_stack_initialize_current_thread()) {
        return 70;
    }
    r_f00000001(handle);
    if ((r_test_detach_count != 1U) || (handle.descriptor == NULL)) {
        return 71;
    }
    return r_generated_main(argc, argv);
}
