#include "r_std_process.h"

#include "r_library_process_internal.h"
#include "r_runtime_0_1.h"

#if defined(__APPLE__)
#include <crt_externs.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int r_test_process_exit_probe_descriptor = -1;

static void r_test_process_exit_probe(void) {
    RRuntimeArgumentSnapshotView snapshot;
    const uint8_t marker = r_runtime_hosted_argument_snapshot(&snapshot) ? UINT8_C(0) : UINT8_C(1);
    ssize_t written;

    do {
        written = write(r_test_process_exit_probe_descriptor, &marker, sizeof(marker));
    } while ((written < (ssize_t)0) && (errno == EINTR));
    if (written != (ssize_t)sizeof(marker)) {
        _Exit(240);
    }
    if (close(r_test_process_exit_probe_descriptor) != 0) {
        _Exit(241);
    }
    r_test_process_exit_probe_descriptor = -1;
}

static int r_test_process_error_conversion(void) {
    size_t index;

    for (index = 0U; index <= (size_t)R_STD_PROCESS_ERROR_OTHER; ++index) {
        RStdProcessError source = {(RStdProcessErrorCode)index, -(int64_t)index};
        RStdError converted = r_std_process_as_error(source);

        R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_PROCESS);
        R_TEST_CHECK(converted.code == (uint32_t)index);
        R_TEST_CHECK(converted.native_code == -(int64_t)index);
    }
    return 0;
}

static RStdStringView r_test_process_view(const char *text) {
    return (RStdStringView){(const uint8_t *)text, strlen(text)};
}

static _Bool r_test_process_view_equals(RStdStringView value, const char *expected) {
    const size_t length = strlen(expected);

    return (value.length == length) &&
           ((length == 0U) || (memcmp(value.data, expected, length) == 0));
}

static _Bool r_test_process_error_is(RStdProcessCallStatus status,
                                     RStdProcessError error,
                                     RStdProcessErrorCode code) {
    return (status == R_STD_PROCESS_CALL_ERROR) && (error.code == code) &&
           (error.native_code == INT64_C(0));
}

static _Bool r_test_process_state_equals(RLibraryProcessState left, RLibraryProcessState right) {
    return memcmp(&left, &right, sizeof(left)) == 0;
}

static _Bool r_test_process_command_valid(const RStdProcessCommand *command) {
    const RStdProcessCommandStorage *storage;

    if ((command == NULL) || (command->storage == NULL)) {
        return 0;
    }
    storage = command->storage;
    if ((storage->allocator == NULL) || (storage->executable.storage == NULL) ||
        (storage->arguments.allocator != storage->allocator) ||
        (storage->arguments.element.size != sizeof(RRuntimeString)) ||
        (storage->arguments.length == 0U) ||
        (storage->arguments.length > storage->arguments.capacity) ||
        (storage->environment.allocator != storage->allocator) ||
        (storage->current_directory < 0)) {
        return 0;
    }
    return !storage->has_working_directory || (storage->working_directory.storage != NULL);
}

static int r_test_process_command_create(void) {
    char environment_name[96];
    RRuntimeAllocator path_allocator;
    RRuntimeAllocator command_allocator;
    RStdFsPathResult path_result;
    RStdProcessCommandResult command_result;
    RStdProcessCommand moved = {0};
    RStdStringView value;
    RStdProcessStdio policy;
    struct stat captured_status;
    struct stat current_status;
    int directory_flags;

    R_TEST_CHECK(snprintf(environment_name,
                          sizeof(environment_name),
                          "R_PROCESS_TEST_SNAPSHOT_%ld",
                          (long)getpid()) > 0);
    R_TEST_CHECK(getenv(environment_name) == NULL);
    R_TEST_CHECK(setenv(environment_name, "captured", 1) == 0);

    r_runtime_allocator_initialize(&path_allocator);
    path_result = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("/bin/echo"));
    R_TEST_CHECK(path_result.status == R_STD_FS_CALL_SUCCESS);
    r_runtime_allocator_initialize(&command_allocator);
    command_result = r_std_process_command_create(&command_allocator, &path_result.value);
    R_TEST_CHECK(command_result.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(command_result.value.storage != NULL);
    r_std_fs_path_destroy(&path_result.value);

    R_TEST_CHECK(r_test_process_view_equals(
        r_library_internal_process_executable(&command_result.value), "/bin/echo"));
    R_TEST_CHECK(r_library_internal_process_argument_count(&command_result.value) == 1U);
    R_TEST_CHECK(r_test_process_view_equals(
        r_library_internal_process_argument(&command_result.value, 0U), "/bin/echo"));
    R_TEST_CHECK(r_library_internal_process_environment_value(
        &command_result.value, r_test_process_view(environment_name), &value));
    R_TEST_CHECK(r_test_process_view_equals(value, "captured"));
    R_TEST_CHECK(setenv(environment_name, "changed-after-capture", 1) == 0);
    R_TEST_CHECK(r_library_internal_process_environment_value(
        &command_result.value, r_test_process_view(environment_name), &value));
    R_TEST_CHECK(r_test_process_view_equals(value, "captured"));

    R_TEST_CHECK(fstat(r_library_internal_process_current_directory(&command_result.value),
                       &captured_status) == 0);
    R_TEST_CHECK(stat(".", &current_status) == 0);
    R_TEST_CHECK(captured_status.st_dev == current_status.st_dev);
    R_TEST_CHECK(captured_status.st_ino == current_status.st_ino);
    directory_flags =
        fcntl(r_library_internal_process_current_directory(&command_result.value), F_GETFD);
    R_TEST_CHECK((directory_flags >= 0) && ((directory_flags & FD_CLOEXEC) != 0));
    R_TEST_CHECK(!r_library_internal_process_working_directory(&command_result.value, &value));
    policy = r_library_internal_process_stdio(&command_result.value);
    R_TEST_CHECK((policy.input == R_STD_PROCESS_PIPE_INHERIT) &&
                 (policy.output == R_STD_PROCESS_PIPE_INHERIT) &&
                 (policy.error == R_STD_PROCESS_PIPE_INHERIT));

    r_library_internal_process_command_move(&moved, &command_result.value);
    R_TEST_CHECK(command_result.value.storage == NULL);
    R_TEST_CHECK(r_test_process_command_valid(&moved));
    r_library_internal_process_command_destroy(&moved);
    R_TEST_CHECK(moved.storage == NULL);
    R_TEST_CHECK(unsetenv(environment_name) == 0);
    return 0;
}

static int r_test_process_create_validation_and_failures(void) {
    RRuntimeAllocator path_allocator;
    RRuntimeAllocator command_allocator;
    RStdFsPathResult executable;
    RStdFsPathResult empty;
    RStdProcessCommandResult result;
    uint64_t allocation_attempts;
    uint64_t fail_at;

    r_runtime_allocator_initialize(&path_allocator);
    executable = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("/bin/echo"));
    empty = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view(""));
    R_TEST_CHECK((executable.status == R_STD_FS_CALL_SUCCESS) &&
                 (empty.status == R_STD_FS_CALL_SUCCESS));

    r_runtime_allocator_initialize(&command_allocator);
    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(1));
    result = r_std_process_command_create(&command_allocator, &empty.value);
    R_TEST_CHECK(
        r_test_process_error_is(result.status, result.error, R_STD_PROCESS_ERROR_INVALID_COMMAND));
    R_TEST_CHECK(result.value.storage == NULL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&command_allocator) == UINT64_C(0));

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(0));
    result = r_std_process_command_create(&command_allocator, &executable.value);
    R_TEST_CHECK(result.status == R_STD_PROCESS_CALL_SUCCESS);
    allocation_attempts = r_runtime_allocator_attempt_count(&command_allocator);
    R_TEST_CHECK(allocation_attempts >= UINT64_C(4));
    r_library_internal_process_command_destroy(&result.value);

    for (fail_at = UINT64_C(1); fail_at <= allocation_attempts; ++fail_at) {
        r_runtime_allocator_set_failure(&command_allocator, fail_at);
        result = r_std_process_command_create(&command_allocator, &executable.value);
        R_TEST_CHECK(r_test_process_error_is(
            result.status, result.error, R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED));
        R_TEST_CHECK(result.value.storage == NULL);
    }

    r_std_fs_path_destroy(&empty.value);
    r_std_fs_path_destroy(&executable.value);
    return 0;
}

static int r_test_process_canonical_environment_snapshot(void) {
#if defined(__APPLE__)
    char environment_name[96];
    char first_entry[160];
    char second_entry[160];
    char malformed_entry[128];
    char ***native_environment = _NSGetEnviron();
    char *saved_first;
    char *saved_second;
    RRuntimeAllocator path_allocator;
    RRuntimeAllocator command_allocator;
    RStdFsPathResult executable;
    RStdProcessCommandResult result;
    RStdStringView observed;

    R_TEST_CHECK((native_environment != NULL) && (*native_environment != NULL) &&
                 ((*native_environment)[0] != NULL) && ((*native_environment)[1] != NULL));
    R_TEST_CHECK(snprintf(environment_name,
                          sizeof(environment_name),
                          "R_PROCESS_DUPLICATE_%ld",
                          (long)getpid()) > 0);
    R_TEST_CHECK(getenv(environment_name) == NULL);
    R_TEST_CHECK(snprintf(first_entry, sizeof(first_entry), "%s=first", environment_name) > 0);
    R_TEST_CHECK(snprintf(second_entry, sizeof(second_entry), "%s=second", environment_name) > 0);
    R_TEST_CHECK(snprintf(malformed_entry,
                          sizeof(malformed_entry),
                          "R_PROCESS_MALFORMED_%ld",
                          (long)getpid()) > 0);

    r_runtime_allocator_initialize(&path_allocator);
    executable = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("/bin/echo"));
    R_TEST_CHECK(executable.status == R_STD_FS_CALL_SUCCESS);
    r_runtime_allocator_initialize(&command_allocator);

    saved_first = (*native_environment)[0];
    saved_second = (*native_environment)[1];
    (*native_environment)[0] = first_entry;
    (*native_environment)[1] = second_entry;
    result = r_std_process_command_create(&command_allocator, &executable.value);
    (*native_environment)[0] = saved_first;
    (*native_environment)[1] = saved_second;
    R_TEST_CHECK(result.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(r_library_internal_process_environment_value(
        &result.value, r_test_process_view(environment_name), &observed));
    R_TEST_CHECK(r_test_process_view_equals(observed, "second"));
    r_library_internal_process_command_destroy(&result.value);

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(0));
    saved_first = (*native_environment)[0];
    (*native_environment)[0] = malformed_entry;
    result = r_std_process_command_create(&command_allocator, &executable.value);
    (*native_environment)[0] = saved_first;
    R_TEST_CHECK(
        r_test_process_error_is(result.status, result.error, R_STD_PROCESS_ERROR_INVALID_ARGUMENT));
    R_TEST_CHECK(result.value.storage == NULL);

    r_std_fs_path_destroy(&executable.value);
#endif
    return 0;
}

static int r_test_process_setter_validation_precedence(void) {
    static const uint8_t nul_argument[] = {'a', UINT8_C(0), 'b'};
    static const uint8_t invalid_name[] = {'B', 'A', 'D', '=', 'N', 'A', 'M', 'E'};
    static const uint8_t nul_value[] = {'v', UINT8_C(0), 'x'};
    RRuntimeAllocator path_allocator;
    RRuntimeAllocator command_allocator;
    RStdFsPathResult executable;
    RStdFsPathResult empty;
    RStdProcessCommandResult command_result;
    RStdProcessVoidResult call;
    RLibraryProcessState before;
    RLibraryProcessState after;

    r_runtime_allocator_initialize(&path_allocator);
    executable = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("/bin/echo"));
    empty = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view(""));
    R_TEST_CHECK((executable.status == R_STD_FS_CALL_SUCCESS) &&
                 (empty.status == R_STD_FS_CALL_SUCCESS));
    r_runtime_allocator_initialize(&command_allocator);
    command_result = r_std_process_command_create(&command_allocator, &executable.value);
    R_TEST_CHECK(command_result.status == R_STD_PROCESS_CALL_SUCCESS);

    before = r_library_internal_process_command_state(&command_result.value);
    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(1));
    call = r_std_process_arg(&command_result.value,
                             (RStdStringView){nul_argument, sizeof(nul_argument)});
    R_TEST_CHECK(
        r_test_process_error_is(call.status, call.error, R_STD_PROCESS_ERROR_INVALID_ARGUMENT));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&command_allocator) == UINT64_C(0));
    after = r_library_internal_process_command_state(&command_result.value);
    R_TEST_CHECK(r_test_process_state_equals(before, after));

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(1));
    call = r_std_process_environment(&command_result.value,
                                     (RStdStringView){invalid_name, sizeof(invalid_name)},
                                     (RStdStringView){nul_value, sizeof(nul_value)});
    R_TEST_CHECK(
        r_test_process_error_is(call.status, call.error, R_STD_PROCESS_ERROR_INVALID_ARGUMENT));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&command_allocator) == UINT64_C(0));

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(1));
    call = r_std_process_environment(&command_result.value,
                                     r_test_process_view("VALID_NAME"),
                                     (RStdStringView){nul_value, sizeof(nul_value)});
    R_TEST_CHECK(
        r_test_process_error_is(call.status, call.error, R_STD_PROCESS_ERROR_INVALID_ARGUMENT));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&command_allocator) == UINT64_C(0));

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(1));
    call = r_std_process_remove_environment(&command_result.value, r_test_process_view(""));
    R_TEST_CHECK(
        r_test_process_error_is(call.status, call.error, R_STD_PROCESS_ERROR_INVALID_ARGUMENT));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&command_allocator) == UINT64_C(0));

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(1));
    call = r_std_process_working_directory(&command_result.value, &empty.value);
    R_TEST_CHECK(
        r_test_process_error_is(call.status, call.error, R_STD_PROCESS_ERROR_INVALID_COMMAND));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&command_allocator) == UINT64_C(0));

    after = r_library_internal_process_command_state(&command_result.value);
    R_TEST_CHECK(r_test_process_state_equals(before, after));

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(0));
    r_library_internal_process_command_destroy(&command_result.value);
    r_std_fs_path_destroy(&empty.value);
    r_std_fs_path_destroy(&executable.value);
    return 0;
}

static int r_test_process_builder_mutations(void) {
    char argument_bytes[] = "alpha";
    char name_bytes[] = "R_PROCESS_BUILDER_VALUE";
    char value_bytes[] = "first";
    RRuntimeAllocator path_allocator;
    RRuntimeAllocator command_allocator;
    RStdFsPathResult executable;
    RStdFsPathResult relative;
    RStdFsPathResult absolute;
    RStdProcessCommandResult command_result;
    RStdProcessVoidResult call;
    RStdStringView value;
    RStdProcessStdio policy;

    r_runtime_allocator_initialize(&path_allocator);
    executable = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("/bin/echo"));
    relative = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("relative/child"));
    absolute = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("/tmp"));
    R_TEST_CHECK((executable.status == R_STD_FS_CALL_SUCCESS) &&
                 (relative.status == R_STD_FS_CALL_SUCCESS) &&
                 (absolute.status == R_STD_FS_CALL_SUCCESS));
    r_runtime_allocator_initialize(&command_allocator);
    command_result = r_std_process_command_create(&command_allocator, &executable.value);
    R_TEST_CHECK(command_result.status == R_STD_PROCESS_CALL_SUCCESS);

    call = r_std_process_arg(&command_result.value, r_test_process_view(argument_bytes));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    argument_bytes[0] = 'X';
    R_TEST_CHECK(r_test_process_view_equals(
        r_library_internal_process_argument(&command_result.value, 1U), "alpha"));

    call = r_std_process_environment(
        &command_result.value, r_test_process_view(name_bytes), r_test_process_view(value_bytes));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    name_bytes[0] = 'X';
    value_bytes[0] = 'X';
    R_TEST_CHECK(r_library_internal_process_environment_value(
        &command_result.value, r_test_process_view("R_PROCESS_BUILDER_VALUE"), &value));
    R_TEST_CHECK(r_test_process_view_equals(value, "first"));
    call = r_std_process_environment(&command_result.value,
                                     r_test_process_view("R_PROCESS_BUILDER_VALUE"),
                                     r_test_process_view("second"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(r_library_internal_process_environment_value(
        &command_result.value, r_test_process_view("R_PROCESS_BUILDER_VALUE"), &value));
    R_TEST_CHECK(r_test_process_view_equals(value, "second"));
    call = r_std_process_remove_environment(&command_result.value,
                                            r_test_process_view("R_PROCESS_BUILDER_VALUE"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(!r_library_internal_process_environment_value(
        &command_result.value, r_test_process_view("R_PROCESS_BUILDER_VALUE"), &value));
    call = r_std_process_remove_environment(&command_result.value,
                                            r_test_process_view("R_PROCESS_BUILDER_VALUE"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);

    r_std_process_clear_environment(&command_result.value);
    R_TEST_CHECK(r_library_internal_process_environment_count(&command_result.value) == 0U);
    call = r_std_process_environment(
        &command_result.value, r_test_process_view("ONLY"), r_test_process_view("entry"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(r_library_internal_process_environment_count(&command_result.value) == 1U);

    call = r_std_process_working_directory(&command_result.value, &relative.value);
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    r_std_fs_path_destroy(&relative.value);
    R_TEST_CHECK(r_library_internal_process_working_directory(&command_result.value, &value));
    R_TEST_CHECK(r_test_process_view_equals(value, "relative/child"));
    call = r_std_process_working_directory(&command_result.value, &absolute.value);
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(r_library_internal_process_working_directory(&command_result.value, &value));
    R_TEST_CHECK(r_test_process_view_equals(value, "/tmp"));

    policy.input = R_STD_PROCESS_PIPE_PIPED;
    policy.output = R_STD_PROCESS_PIPE_NULL_DEVICE;
    policy.error = R_STD_PROCESS_PIPE_PIPED;
    r_std_process_set_stdio(&command_result.value, policy);
    policy = r_library_internal_process_stdio(&command_result.value);
    R_TEST_CHECK((policy.input == R_STD_PROCESS_PIPE_PIPED) &&
                 (policy.output == R_STD_PROCESS_PIPE_NULL_DEVICE) &&
                 (policy.error == R_STD_PROCESS_PIPE_PIPED));

    r_library_internal_process_command_destroy(&command_result.value);
    r_std_fs_path_destroy(&absolute.value);
    r_std_fs_path_destroy(&executable.value);
    return 0;
}

static int r_test_process_native_text_snapshot(void) {
    RRuntimeAllocator path_allocator;
    RRuntimeAllocator command_allocator;
    RStdFsPathResult executable;
    RStdFsPathResult working_directory;
    RStdProcessCommandResult command_result;
    RStdProcessVoidResult call;
    RLibraryProcessNativeText text = {0};
    RLibraryProcessState before;
    RLibraryProcessState after;

    r_runtime_allocator_initialize(&path_allocator);
    executable = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("/bin/echo"));
    working_directory = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("/tmp"));
    R_TEST_CHECK((executable.status == R_STD_FS_CALL_SUCCESS) &&
                 (working_directory.status == R_STD_FS_CALL_SUCCESS));
    r_runtime_allocator_initialize(&command_allocator);
    command_result = r_std_process_command_create(&command_allocator, &executable.value);
    R_TEST_CHECK(command_result.status == R_STD_PROCESS_CALL_SUCCESS);
    r_std_process_clear_environment(&command_result.value);
    call = r_std_process_arg(&command_result.value, r_test_process_view("alpha beta"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    call = r_std_process_environment(
        &command_result.value, r_test_process_view("SECOND"), r_test_process_view("two words"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    call = r_std_process_environment(
        &command_result.value, r_test_process_view("FIRST"), r_test_process_view("one"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    call = r_std_process_working_directory(&command_result.value, &working_directory.value);
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);

    R_TEST_CHECK(r_library_internal_process_native_text_create(&command_result.value, &text) ==
                 R_LIBRARY_PROCESS_NATIVE_TEXT_OK);
    R_TEST_CHECK(text.allocator == &command_allocator);
    R_TEST_CHECK(text.allocation != NULL);
    R_TEST_CHECK(strcmp(text.executable, "/bin/echo") == 0);
    R_TEST_CHECK(text.argument_count == 2U);
    R_TEST_CHECK(strcmp(text.arguments[0], "/bin/echo") == 0);
    R_TEST_CHECK(strcmp(text.arguments[1], "alpha beta") == 0);
    R_TEST_CHECK(text.arguments[2] == NULL);
    R_TEST_CHECK(text.environment_count == 2U);
    R_TEST_CHECK(strcmp(text.environment[0], "SECOND=two words") == 0);
    R_TEST_CHECK(strcmp(text.environment[1], "FIRST=one") == 0);
    R_TEST_CHECK(text.environment[2] == NULL);
    R_TEST_CHECK(strcmp(text.working_directory, "/tmp") == 0);

    call = r_std_process_arg(&command_result.value, r_test_process_view("later"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    call = r_std_process_environment(
        &command_result.value, r_test_process_view("SECOND"), r_test_process_view("changed"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(text.argument_count == 2U);
    R_TEST_CHECK(strcmp(text.arguments[1], "alpha beta") == 0);
    R_TEST_CHECK(strcmp(text.environment[0], "SECOND=two words") == 0);

    r_library_internal_process_command_destroy(&command_result.value);
    R_TEST_CHECK(strcmp(text.executable, "/bin/echo") == 0);
    R_TEST_CHECK(strcmp(text.arguments[1], "alpha beta") == 0);
    R_TEST_CHECK(strcmp(text.environment[1], "FIRST=one") == 0);
    r_library_internal_process_native_text_destroy(&text);
    R_TEST_CHECK((text.allocation == NULL) && (text.arguments == NULL) &&
                 (text.environment == NULL) && (text.executable == NULL) &&
                 (text.working_directory == NULL));

    command_result = r_std_process_command_create(&command_allocator, &executable.value);
    R_TEST_CHECK(command_result.status == R_STD_PROCESS_CALL_SUCCESS);
    before = r_library_internal_process_command_state(&command_result.value);
    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(1));
    R_TEST_CHECK(r_library_internal_process_native_text_create(&command_result.value, &text) ==
                 R_LIBRARY_PROCESS_NATIVE_TEXT_RESOURCE_EXHAUSTED);
    R_TEST_CHECK(text.allocation == NULL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&command_allocator) == UINT64_C(1));
    after = r_library_internal_process_command_state(&command_result.value);
    R_TEST_CHECK(r_test_process_state_equals(before, after));
    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(0));
    r_library_internal_process_command_destroy(&command_result.value);

    r_std_fs_path_destroy(&working_directory.value);
    r_std_fs_path_destroy(&executable.value);
    return 0;
}

static int r_test_process_setter_allocation_failures(void) {
    RRuntimeAllocator path_allocator;
    RRuntimeAllocator command_allocator;
    RStdFsPathResult executable;
    RStdFsPathResult first_directory;
    RStdFsPathResult second_directory;
    RStdProcessCommandResult command_result;
    RStdProcessVoidResult call;
    RLibraryProcessState before;
    RLibraryProcessState after;
    RStdStringView observed;
    size_t maximum_environment_length;
    size_t fill_index = 0U;
    char name[96];
    uint64_t fail_at;

    r_runtime_allocator_initialize(&path_allocator);
    executable = r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("/bin/echo"));
    first_directory =
        r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("first-directory"));
    second_directory =
        r_std_fs_path_from_utf8(&path_allocator, r_test_process_view("second-directory"));
    R_TEST_CHECK((executable.status == R_STD_FS_CALL_SUCCESS) &&
                 (first_directory.status == R_STD_FS_CALL_SUCCESS) &&
                 (second_directory.status == R_STD_FS_CALL_SUCCESS));
    r_runtime_allocator_initialize(&command_allocator);
    command_result = r_std_process_command_create(&command_allocator, &executable.value);
    R_TEST_CHECK(command_result.status == R_STD_PROCESS_CALL_SUCCESS);

    before = r_library_internal_process_command_state(&command_result.value);
    while (before.arguments_length < before.arguments_capacity) {
        r_runtime_allocator_set_failure(&command_allocator, UINT64_C(0));
        call = r_std_process_arg(&command_result.value, r_test_process_view("fill"));
        R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
        before = r_library_internal_process_command_state(&command_result.value);
    }
    for (fail_at = UINT64_C(1); fail_at <= UINT64_C(2); ++fail_at) {
        before = r_library_internal_process_command_state(&command_result.value);
        r_runtime_allocator_set_failure(&command_allocator, fail_at);
        call = r_std_process_arg(&command_result.value, r_test_process_view("target-argument"));
        R_TEST_CHECK(r_test_process_error_is(
            call.status, call.error, R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED));
        after = r_library_internal_process_command_state(&command_result.value);
        R_TEST_CHECK(r_test_process_state_equals(before, after));
    }

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(0));
    r_std_process_clear_environment(&command_result.value);
    for (;;) {
        before = r_library_internal_process_command_state(&command_result.value);
        maximum_environment_length = ((before.environment_capacity / 3U) * 2U) +
                                     (((before.environment_capacity % 3U) * 2U) / 3U);
        if ((before.environment_capacity != 0U) &&
            (before.environment_length == maximum_environment_length)) {
            break;
        }
        R_TEST_CHECK(
            snprintf(name, sizeof(name), "R_PROCESS_FILL_%zu_%ld", fill_index, (long)getpid()) > 0);
        call = r_std_process_environment(
            &command_result.value, r_test_process_view(name), r_test_process_view("value"));
        R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
        fill_index += 1U;
    }
    for (fail_at = UINT64_C(1); fail_at <= UINT64_C(3); ++fail_at) {
        before = r_library_internal_process_command_state(&command_result.value);
        r_runtime_allocator_set_failure(&command_allocator, fail_at);
        call = r_std_process_environment(&command_result.value,
                                         r_test_process_view("R_PROCESS_FAIL_TARGET"),
                                         r_test_process_view("target-value"));
        R_TEST_CHECK(r_test_process_error_is(
            call.status, call.error, R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED));
        after = r_library_internal_process_command_state(&command_result.value);
        R_TEST_CHECK(r_test_process_state_equals(before, after));
        R_TEST_CHECK(!r_library_internal_process_environment_value(
            &command_result.value, r_test_process_view("R_PROCESS_FAIL_TARGET"), &observed));
    }
    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(0));
    call = r_std_process_environment(&command_result.value,
                                     r_test_process_view("R_PROCESS_FAIL_TARGET"),
                                     r_test_process_view("target-value"));
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);

    before = r_library_internal_process_command_state(&command_result.value);
    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(1));
    call = r_std_process_remove_environment(&command_result.value,
                                            r_test_process_view("R_PROCESS_FAIL_TARGET"));
    R_TEST_CHECK(
        r_test_process_error_is(call.status, call.error, R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED));
    after = r_library_internal_process_command_state(&command_result.value);
    R_TEST_CHECK(r_test_process_state_equals(before, after));
    R_TEST_CHECK(r_library_internal_process_environment_value(
        &command_result.value, r_test_process_view("R_PROCESS_FAIL_TARGET"), &observed));

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(0));
    call = r_std_process_working_directory(&command_result.value, &first_directory.value);
    R_TEST_CHECK(call.status == R_STD_PROCESS_CALL_SUCCESS);
    before = r_library_internal_process_command_state(&command_result.value);
    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(1));
    call = r_std_process_working_directory(&command_result.value, &second_directory.value);
    R_TEST_CHECK(
        r_test_process_error_is(call.status, call.error, R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED));
    after = r_library_internal_process_command_state(&command_result.value);
    R_TEST_CHECK(r_test_process_state_equals(before, after));
    R_TEST_CHECK(r_library_internal_process_working_directory(&command_result.value, &observed));
    R_TEST_CHECK(r_test_process_view_equals(observed, "first-directory"));

    r_runtime_allocator_set_failure(&command_allocator, UINT64_C(0));
    r_library_internal_process_command_destroy(&command_result.value);
    r_std_fs_path_destroy(&second_directory.value);
    r_std_fs_path_destroy(&first_directory.value);
    r_std_fs_path_destroy(&executable.value);
    return 0;
}

static int r_test_process_public_types(void) {
    RStdProcessStdio policy = {
        R_STD_PROCESS_PIPE_PIPED,
        R_STD_PROCESS_PIPE_INHERIT,
        R_STD_PROCESS_PIPE_NULL_DEVICE,
    };
    RStdProcessExitStatus success = {
        R_STD_PROCESS_TERMINATION_EXITED,
        INT32_C(0),
        1,
    };
    RStdProcessExitStatus signal = {
        R_STD_PROCESS_TERMINATION_SIGNALLED,
        INT32_C(9),
        0,
    };

    R_TEST_CHECK(policy.input == R_STD_PROCESS_PIPE_PIPED);
    R_TEST_CHECK(policy.output == R_STD_PROCESS_PIPE_INHERIT);
    R_TEST_CHECK(policy.error == R_STD_PROCESS_PIPE_NULL_DEVICE);
    R_TEST_CHECK(success.kind == R_STD_PROCESS_TERMINATION_EXITED);
    R_TEST_CHECK(success.code == INT32_C(0));
    R_TEST_CHECK(success.success);
    R_TEST_CHECK(signal.kind == R_STD_PROCESS_TERMINATION_SIGNALLED);
    R_TEST_CHECK(signal.code == INT32_C(9));
    R_TEST_CHECK(!signal.success);
    return 0;
}

static int r_test_process_id(void) {
    RStdProcessChild first = {NULL, UINT64_C(0x0102030405060708)};
    RStdProcessChild second = {NULL, UINT64_MAX};

    R_TEST_CHECK(r_std_process_id(&first) == UINT64_C(0x0102030405060708));
    R_TEST_CHECK(r_std_process_id(&first) == UINT64_C(0x0102030405060708));
    R_TEST_CHECK(r_std_process_id(&second) == UINT64_MAX);
    return 0;
}

static int r_test_process_exit_case(int32_t source_status, int expected_host_status) {
    int descriptors[2];
    pid_t child;
    int status = 0;
    uint8_t markers[2] = {UINT8_C(0), UINT8_C(0)};
    size_t marker_count = 0U;
    ssize_t read_count;

    R_TEST_CHECK(pipe(descriptors) == 0);
    child = fork();
    R_TEST_CHECK(child >= (pid_t)0);
    if (child == (pid_t)0) {
        char program_name[] = "std-process-exit-test";
        char *arguments[] = {program_name, NULL};
        RRuntimeStartResult start;

        (void)close(descriptors[0]);
        r_test_process_exit_probe_descriptor = descriptors[1];
        if (atexit(r_test_process_exit_probe) != 0) {
            _Exit(242);
        }
        start = r_runtime_hosted_start(1, arguments);
        if (!start.started || (start.process_status != 0)) {
            _Exit(243);
        }
        r_std_process_exit(source_status);
    }

    R_TEST_CHECK(close(descriptors[1]) == 0);
    do {
        read_count = read(descriptors[0], markers + marker_count, sizeof(markers) - marker_count);
        if (read_count > (ssize_t)0) {
            marker_count += (size_t)read_count;
        }
    } while ((read_count > (ssize_t)0) || ((read_count < (ssize_t)0) && (errno == EINTR)));
    R_TEST_CHECK(read_count == (ssize_t)0);
    R_TEST_CHECK(close(descriptors[0]) == 0);
    R_TEST_CHECK(waitpid(child, &status, 0) == child);
    R_TEST_CHECK(WIFEXITED(status));
    R_TEST_CHECK(WEXITSTATUS(status) == expected_host_status);
    R_TEST_CHECK(marker_count == 1U);
    R_TEST_CHECK(markers[0] == UINT8_C(1));
    return 0;
}

static int r_test_process_exit(void) {
    R_TEST_CHECK(r_test_process_exit_case(INT32_C(0), 0) == 0);
    R_TEST_CHECK(r_test_process_exit_case(INT32_C(37), 37) == 0);
    R_TEST_CHECK(r_test_process_exit_case(-INT32_C(1), 255) == 0);
    R_TEST_CHECK(r_test_process_exit_case(INT32_C(4660), 52) == 0);
    return 0;
}

static int r_test_process_abort(void) {
    pid_t child = fork();
    int status = 0;

    R_TEST_CHECK(child >= (pid_t)0);
    if (child == (pid_t)0) {
        r_std_process_abort();
    }
    R_TEST_CHECK(waitpid(child, &status, 0) == child);
    R_TEST_CHECK(WIFSIGNALED(status));
    R_TEST_CHECK(WTERMSIG(status) == SIGABRT);
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_process_exit() == 0);
    R_TEST_CHECK(r_test_process_abort() == 0);
    R_TEST_CHECK(r_test_process_error_conversion() == 0);
    R_TEST_CHECK(r_test_process_public_types() == 0);
    R_TEST_CHECK(r_test_process_id() == 0);
    R_TEST_CHECK(r_test_process_command_create() == 0);
    R_TEST_CHECK(r_test_process_create_validation_and_failures() == 0);
    R_TEST_CHECK(r_test_process_canonical_environment_snapshot() == 0);
    R_TEST_CHECK(r_test_process_setter_validation_precedence() == 0);
    R_TEST_CHECK(r_test_process_builder_mutations() == 0);
    R_TEST_CHECK(r_test_process_native_text_snapshot() == 0);
    R_TEST_CHECK(r_test_process_setter_allocation_failures() == 0);
    return 0;
}
