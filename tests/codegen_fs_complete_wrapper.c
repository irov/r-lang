#include "r_std_fs.h"

/* Refuse one start per owning argument shape before native work can begin. */
static size_t r_test_write_count;
static size_t r_test_read_count;
static size_t r_test_close_file_count;
static size_t r_test_close_directory_count;
static size_t r_test_next_count;

static RStdFsTaskStartResult r_test_refused_start(void) {
    const RStdFsTaskStartResult result = {
        .is_ok = 0, .task = NULL, .error = R_STD_ASYNC_START_ALLOCATION_FAILED};
    return result;
}

RStdFsTaskStartResult
r_test_write(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline);
RStdFsTaskStartResult
r_test_write(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline) {
    if (++r_test_write_count == 1U) {
        return r_test_refused_start();
    }
    return r_std_fs_write(file, buffer, deadline);
}

RStdFsTaskStartResult
r_test_read(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline);
RStdFsTaskStartResult
r_test_read(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline) {
    if (++r_test_read_count == 1U) {
        return r_test_refused_start();
    }
    return r_std_fs_read(file, buffer, deadline);
}

RStdFsTaskStartResult r_test_close_file(RStdFsFile *file, RStdFsDeadline deadline);
RStdFsTaskStartResult r_test_close_file(RStdFsFile *file, RStdFsDeadline deadline) {
    if (++r_test_close_file_count == 1U) {
        return r_test_refused_start();
    }
    return r_std_fs_close_file(file, deadline);
}

RStdFsTaskStartResult r_test_close_directory(RStdFsDirectory *directory, RStdFsDeadline deadline);
RStdFsTaskStartResult r_test_close_directory(RStdFsDirectory *directory, RStdFsDeadline deadline) {
    if (++r_test_close_directory_count == 1U) {
        return r_test_refused_start();
    }
    return r_std_fs_close_directory(directory, deadline);
}

RStdFsTaskStartResult r_test_next(RStdFsDirectoryIter *iterator, RStdFsDeadline deadline);
RStdFsTaskStartResult r_test_next(RStdFsDirectoryIter *iterator, RStdFsDeadline deadline) {
    if (++r_test_next_count == 1U) {
        return r_test_refused_start();
    }
    return r_std_fs_next(iterator, deadline);
}

#define r_std_fs_write r_test_write
#define r_std_fs_read r_test_read
#define r_std_fs_close_file r_test_close_file
#define r_std_fs_close_directory r_test_close_directory
#define r_std_fs_next r_test_next
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_fs_write
#undef r_std_fs_read
#undef r_std_fs_close_file
#undef r_std_fs_close_directory
#undef r_std_fs_next

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    if (status != 0) {
        return status;
    }
    return r_test_write_count == 2U && r_test_read_count == 2U && r_test_close_file_count == 2U &&
                   r_test_close_directory_count == 3U && r_test_next_count == 4U
               ? 0
               : 22;
}
