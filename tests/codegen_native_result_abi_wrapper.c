#include "r_std_fs.h"

#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main

void r_library_internal_fs_directory_move(RStdFsDirectory *destination, RStdFsDirectory *source) {
    *destination = *source;
    *source = (RStdFsDirectory){0};
}

void r_library_internal_fs_directory_drop(RStdFsDirectory *directory) {
    *directory = (RStdFsDirectory){0};
}

int main(int argc, char *argv[]) {
    if (!r_runtime_stack_initialize_current_thread()) {
        return __LINE__;
    }
    return r_generated_main(argc, argv);
}
