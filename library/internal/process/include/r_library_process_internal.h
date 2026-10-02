#ifndef R_LIBRARY_PROCESS_INTERNAL_H
#define R_LIBRARY_PROCESS_INTERNAL_H

#include "r_std_process.h"

#include "r_runtime_array.h"
#include "r_runtime_darwin_process.h"
#include "r_runtime_dict.h"
#include "r_runtime_string.h"

#include <stddef.h>
#include <stdint.h>

struct RStdProcessCommandStorage {
    RRuntimeAllocator *allocator;
    RStdFsPath executable;
    RRuntimeArray arguments;
    RRuntimeDict environment;
    int current_directory;
    _Bool has_working_directory;
    RStdFsPath working_directory;
    RStdProcessStdio stdio;
};

struct RStdProcessChildStorage {
    RRuntimeAllocator *allocator;
    RRuntimeDarwinProcessChild *native;
};

typedef enum RLibraryProcessValidation {
    R_LIBRARY_PROCESS_VALID = 0,
    R_LIBRARY_PROCESS_INVALID = 1,
    R_LIBRARY_PROCESS_CONTRACT_VIOLATION = 2
} RLibraryProcessValidation;

typedef struct RLibraryProcessState {
    const RStdProcessCommandStorage *storage;
    const void *arguments_data;
    size_t arguments_length;
    size_t arguments_capacity;
    const void *environment_slots;
    size_t environment_length;
    size_t environment_capacity;
    const void *working_directory_storage;
    int current_directory;
    _Bool has_working_directory;
    RStdProcessStdio stdio;
    uint64_t content_hash;
} RLibraryProcessState;

typedef enum RLibraryProcessNativeTextStatus {
    R_LIBRARY_PROCESS_NATIVE_TEXT_OK = 0,
    R_LIBRARY_PROCESS_NATIVE_TEXT_INVALID,
    R_LIBRARY_PROCESS_NATIVE_TEXT_RESOURCE_EXHAUSTED
} RLibraryProcessNativeTextStatus;

typedef enum RLibraryProcessDeadlineStatus {
    R_LIBRARY_PROCESS_DEADLINE_READY = 0,
    R_LIBRARY_PROCESS_DEADLINE_EXPIRED,
    R_LIBRARY_PROCESS_DEADLINE_ERROR
} RLibraryProcessDeadlineStatus;

/*
 * Complete pre-commit native text snapshot for posix_spawn. allocation owns every pointer array
 * and byte sequence below in one allocator object. arguments and environment each include their
 * required trailing null pointer. No field borrows the source command after successful creation.
 */
typedef struct RLibraryProcessNativeText {
    RRuntimeAllocator *allocator;
    void *allocation;
    char *executable;
    char **arguments;
    size_t argument_count;
    char **environment;
    size_t environment_count;
    char *working_directory;
} RLibraryProcessNativeText;

RStdProcessError r_library_internal_process_error(RStdProcessErrorCode code, int64_t native_code);
RStdProcessError r_library_internal_process_allocation_error(void);
RStdProcessError r_library_internal_process_control_error_from_native(int native_error);
RLibraryProcessDeadlineStatus r_library_internal_process_deadline_timeout(
    RStdProcessDeadline deadline, uint64_t *timeout_nanoseconds, RStdProcessError *error);
RStdProcessCallStatus r_library_internal_process_string_status(RRuntimeStringStatus status,
                                                               RStdProcessError *error);
RStdProcessCallStatus r_library_internal_process_array_status(RRuntimeArrayStatus status,
                                                              RStdProcessError *error);
RStdProcessCallStatus r_library_internal_process_dict_status(RRuntimeDictStatus status,
                                                             RStdProcessError *error);
RStdProcessCallStatus r_library_internal_process_copy_string(RRuntimeAllocator *allocator,
                                                             RStdStringView source,
                                                             RRuntimeString *result,
                                                             RStdProcessError *error);
RStdProcessCallStatus r_library_internal_process_copy_path(RRuntimeAllocator *allocator,
                                                           const RStdFsPath *source,
                                                           RStdFsPath *result,
                                                           RStdProcessError *error);
RLibraryProcessValidation r_library_internal_process_validate_argument(RStdStringView value);
RLibraryProcessValidation r_library_internal_process_validate_environment_name(RStdStringView name);
RLibraryProcessValidation
r_library_internal_process_validate_environment_value(RStdStringView value);
RRuntimeTypeInfo r_library_internal_process_string_type(void);
void r_library_internal_process_command_move(RStdProcessCommand *destination,
                                             RStdProcessCommand *source);
void r_library_internal_process_command_destroy(RStdProcessCommand *command);
RLibraryProcessState r_library_internal_process_command_state(const RStdProcessCommand *command);
RStdStringView r_library_internal_process_executable(const RStdProcessCommand *command);
size_t r_library_internal_process_argument_count(const RStdProcessCommand *command);
RStdStringView r_library_internal_process_argument(const RStdProcessCommand *command, size_t index);
size_t r_library_internal_process_environment_count(const RStdProcessCommand *command);
_Bool r_library_internal_process_environment_value(const RStdProcessCommand *command,
                                                   RStdStringView name,
                                                   RStdStringView *value);
int r_library_internal_process_current_directory(const RStdProcessCommand *command);
_Bool r_library_internal_process_working_directory(const RStdProcessCommand *command,
                                                   RStdStringView *path);
RStdProcessStdio r_library_internal_process_stdio(const RStdProcessCommand *command);
RLibraryProcessNativeTextStatus
r_library_internal_process_native_text_create(const RStdProcessCommand *command,
                                              RLibraryProcessNativeText *result);
void r_library_internal_process_native_text_destroy(RLibraryProcessNativeText *text);
RStdProcessChildStorage *r_library_internal_process_child_reserve(RRuntimeAllocator *allocator);
void r_library_internal_process_child_publish(RStdProcessChildStorage *storage,
                                              RRuntimeDarwinProcessChild *native);
void r_library_internal_process_child_storage_destroy(RStdProcessChildStorage *storage);
RRuntimeDarwinIoHandle *r_library_internal_process_child_take_pipe(RStdProcessChild *child,
                                                                   RRuntimeDarwinProcessPipe pipe);
RStdProcessTaskStartResult r_library_internal_process_spawn(RStdProcessCommand *command,
                                                            RStdProcessDeadline deadline);
RStdProcessTaskStartResult r_library_internal_process_wait(RStdProcessChild *child,
                                                           RStdProcessDeadline deadline);
RStdProcessTaskStartResult r_library_internal_process_terminate(const RStdProcessChild *child,
                                                                RStdProcessDeadline deadline);

#if defined(R_LIBRARY_PROCESS_TESTING)
RRuntimeDarwinProcessChild *
r_library_internal_process_child_testing_native(const RStdProcessChild *child);
#endif

#endif
