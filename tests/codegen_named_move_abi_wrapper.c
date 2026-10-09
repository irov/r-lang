#include "r_std_fs.h"

#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main

void r_library_internal_fs_directory_move(RStdFsDirectory *destination, RStdFsDirectory *source) {
    *destination = *source;
    *source = (RStdFsDirectory){0};
}

void r_library_internal_fs_directory_drop(RStdFsDirectory *directory) {
    *directory = (RStdFsDirectory){0};
}

void r_library_internal_fs_file_move(RStdFsFile *destination, RStdFsFile *source) {
    *destination = *source;
    *source = (RStdFsFile){0};
}

void r_library_internal_fs_file_drop(RStdFsFile *file) {
    *file = (RStdFsFile){0};
}

void r_library_internal_fs_directory_iter_move(RStdFsDirectoryIter *destination,
                                               RStdFsDirectoryIter *source) {
    *destination = *source;
    *source = (RStdFsDirectoryIter){0};
}

void r_library_internal_fs_directory_iter_drop(RStdFsDirectoryIter *iterator) {
    *iterator = (RStdFsDirectoryIter){0};
}

void r_library_internal_fs_directory_entry_move(RStdFsDirectoryEntry *destination,
                                                RStdFsDirectoryEntry *source) {
    *destination = *source;
    *source = (RStdFsDirectoryEntry){0};
}

void r_library_internal_fs_directory_entry_drop(RStdFsDirectoryEntry *entry) {
    *entry = (RStdFsDirectoryEntry){0};
}

void r_library_internal_fs_directory_next_result_move(RStdFsDirectoryNextResult *destination,
                                                      RStdFsDirectoryNextResult *source) {
    *destination = *source;
    *source = (RStdFsDirectoryNextResult){0};
}

void r_library_internal_fs_directory_next_result_drop(RStdFsDirectoryNextResult *result) {
    *result = (RStdFsDirectoryNextResult){0};
}

void r_library_internal_fs_write_file_result_move(RStdFsWriteFileResult *destination,
                                                  RStdFsWriteFileResult *source) {
    *destination = *source;
    *source = (RStdFsWriteFileResult){0};
}

void r_library_internal_fs_write_file_result_drop(RStdFsWriteFileResult *result) {
    *result = (RStdFsWriteFileResult){0};
}

int main(int argc, char *argv[]) {
    if (!r_runtime_stack_initialize_current_thread()) {
        return __LINE__;
    }
    return r_generated_main(argc, argv);
}
