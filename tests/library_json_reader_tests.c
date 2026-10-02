#include "r_std_fs.h"
#include "r_std_io.h"
#include "r_std_net.h"

#include "r_runtime_darwin_fs_lane.h"
#include "r_std_json_reader.h"
#include <arpa/inet.h>
#include <sys/socket.h>

#include <sched.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static atomic_uint transport_starts;
static atomic_uint text_drops;
static void input_move(void *out, void *source) {
    r_std_io_input_move_initialize(out, source);
}
static void input_drop(void *value) {
    r_std_io_input_destroy(value);
}
static RStdJsonTaskStartResult
transport_start(const void *input, RRuntimeArray *buffer, RStdJsonDeadline deadline) {
    atomic_fetch_add(&transport_starts, 1U);
    RStdIoTaskStartResult result =
        r_std_io_read(input, buffer, (RStdIoDeadline){deadline.has_value, deadline.value});
    return (RStdJsonTaskStartResult){result.is_ok, result.task, result.error};
}
static void transport_take(void *storage, RStdJsonTransportRead *out) {
    RStdIoReadResult *read = storage;
    *out = (RStdJsonTransportRead){.buffer = read->buffer,
                                   .count = read->count,
                                   .end = read->kind == R_STD_IO_READ_RESULT_END,
                                   .failed = read->kind == R_STD_IO_READ_RESULT_FAILED,
                                   .code = (uint32_t)read->error.code,
                                   .native_code = read->error.native_code};
    *read = (RStdIoReadResult){0};
}
static bool transport_deadline(RStdJsonDeadline deadline, RStdJsonReadOutcome *out) {
    if (!deadline.has_value)
        return true;
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    if (now.is_ok && (now.value.storage_seconds < deadline.value.storage_seconds ||
                      (now.value.storage_seconds == deadline.value.storage_seconds &&
                       now.value.storage_nanoseconds < deadline.value.storage_nanoseconds)))
        return true;
    *out = (RStdJsonReadOutcome){.status = R_STD_JSON_READ_TRANSPORT_ERROR,
                                 .transport_code =
                                     now.is_ok ? R_STD_IO_ERROR_TIMED_OUT : R_STD_IO_ERROR_OTHER,
                                 .native_code = now.is_ok ? 0 : now.error.native_code};
    return false;
}
static RStdJsonTransport transport(void) {
    return (RStdJsonTransport){
        .handle_type = {sizeof(RStdIoInput), alignof(RStdIoInput), input_move, input_drop},
        .read_type = {sizeof(RStdIoReadResult), alignof(RStdIoReadResult), NULL, NULL},
        .start = transport_start,
        .take = transport_take,
        .deadline = transport_deadline};
}
static RStdIoInput pipe_input(RRuntimeAllocator *allocator, int *writer) {
    int descriptors[2];
    CHECK(pipe(descriptors) == 0);
    RRuntimeDarwinIoHandleCreateResult made =
        r_runtime_darwin_io_handle_create(allocator, descriptors[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(made.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(close(descriptors[0]) == 0);
    *writer = descriptors[1];
    return (RStdIoInput){made.handle};
}
static void text_drop(void *value) {
    atomic_fetch_add(&text_drops, 1U);
    r_runtime_string_destroy(value);
}
static RStdJsonDecodeStep text_step(RStdJsonCursor *cursor, RStdJsonDecodeFrame *frame) {
    if (!cursor->has_token)
        return R_STD_JSON_DECODE_WAIT;
    return r_json_cursor_string(cursor, frame->output) ? R_STD_JSON_DECODE_COMPLETE
                                                       : R_STD_JSON_DECODE_ERROR;
}
static bool text_create(RStdJsonCursor *cursor, void *output, bool quoted) {
    return r_json_decode_push(cursor,
                              sizeof(RStdJsonDecodeFrame),
                              alignof(RStdJsonDecodeFrame),
                              text_step,
                              NULL,
                              output,
                              quoted);
}
static RStdJsonDecodeStep int_step(RStdJsonCursor *cursor, RStdJsonDecodeFrame *frame) {
    if (!cursor->has_token)
        return R_STD_JSON_DECODE_WAIT;
    uint64_t integer;
    bool negative;
    if (!r_json_cursor_integer(cursor, false, INT64_MAX, UINT64_C(1) << 63U, &negative, &integer))
        return R_STD_JSON_DECODE_ERROR;
    if (negative)
        integer = UINT64_C(0) - integer;
    memcpy(frame->output, &integer, sizeof(integer));
    return R_STD_JSON_DECODE_COMPLETE;
}
static bool int_create(RStdJsonCursor *cursor, void *output, bool quoted) {
    return r_json_decode_push(cursor,
                              sizeof(RStdJsonDecodeFrame),
                              alignof(RStdJsonDecodeFrame),
                              int_step,
                              NULL,
                              output,
                              quoted);
}
typedef struct ReadResult {
    RStdJsonReadOutcome outcome;
    bool text;
    union {
        RStdString string;
        int64_t integer;
    } value;
} ReadResult;
static void read_drop(void *storage) {
    ReadResult *result = storage;
    if (result->outcome.status == R_STD_JSON_READ_VALUE && result->text)
        text_drop(&result->value.string);
    r_json_error_destroy(&result->outcome.json.error);
}
static void text_complete(void *storage, RStdJsonDecoder *decoder, RStdJsonReadOutcome outcome) {
    ReadResult *result = storage;
    *result = (ReadResult){.outcome = outcome, .text = true};
    if (outcome.status == R_STD_JSON_READ_VALUE)
        CHECK(r_json_decoder_take(decoder, &result->value.string).status ==
              R_STD_JSON_CALL_SUCCESS);
}
static void int_complete(void *storage, RStdJsonDecoder *decoder, RStdJsonReadOutcome outcome) {
    ReadResult *result = storage;
    *result = (ReadResult){.outcome = outcome};
    if (outcome.status == R_STD_JSON_READ_VALUE)
        CHECK(r_json_decoder_take(decoder, &result->value.integer).status ==
              R_STD_JSON_CALL_SUCCESS);
}
static RStdJsonTaskStartResult
start_read(RStdJsonReader *reader, bool text, RStdJsonDeadline deadline) {
    return r_json_reader_read_next(
        reader,
        deadline,
        text ? (RRuntimeTypeInfo){sizeof(RStdString), alignof(RStdString), NULL, text_drop}
             : (RRuntimeTypeInfo){sizeof(int64_t), alignof(int64_t), NULL, NULL},
        text ? text_create : int_create,
        (RRuntimeTypeInfo){sizeof(ReadResult), alignof(ReadResult), NULL, read_drop},
        text ? text_complete : int_complete);
}
static ReadResult read_next(RStdJsonReader *reader, bool text) {
    RStdJsonTaskStartResult start = start_read(reader, text, (RStdJsonDeadline){0});
    CHECK(start.is_ok);
    ReadResult result;
    CHECK(r_runtime_task_await(&start.task, &result) == R_RUNTIME_TASK_AWAIT_OK);
    return result;
}
static void wait_for_reads(unsigned count) {
    struct timespec start, now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    while (atomic_load(&transport_starts) < count) {
        CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
        CHECK(now.tv_sec - start.tv_sec < 10);
        sched_yield();
    }
}
static void test_sequence(RRuntimeAllocator *allocator) {
    int writer;
    RStdIoInput input = pipe_input(allocator, &writer);
    RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
    options.mode = R_STD_JSON_MODE_SEQUENCE;
    RStdJsonReader reader;
    CHECK(r_json_reader_initialize(&reader, allocator, options, transport(), &input).status ==
          R_STD_JSON_CALL_SUCCESS);
    CHECK(input.handle == NULL);
    const char *source = "\"Madrid\\uD83D\\uDE00\" -42 \"tail\"";
    CHECK(write(writer, source, strlen(source)) == (ssize_t)strlen(source));
    CHECK(close(writer) == 0);
    ReadResult first = read_next(&reader, true);
    CHECK(first.outcome.status == R_STD_JSON_READ_VALUE);
    CHECK(r_runtime_string_length(&first.value.string) == 10U);
    read_drop(&first);
    ReadResult second = read_next(&reader, false);
    CHECK(second.outcome.status == R_STD_JSON_READ_VALUE && second.value.integer == -42);
    read_drop(&second);
    RStdJsonDetached detached;
    CHECK(r_json_reader_detach(&reader, &detached).status == R_STD_JSON_CALL_SUCCESS);
    CHECK(reader.state == NULL);
    RRuntimeArray bytes;
    CHECK(r_json_detached_take_bytes(&detached, &bytes).status == R_STD_JSON_CALL_SUCCESS);
    CHECK(bytes.length == 7U && memcmp(bytes.data, " \"tail\"", 7U) == 0);
    CHECK(r_json_detached_take_bytes(&detached, &bytes).status == R_STD_JSON_CALL_JSON_ERROR);
    r_runtime_array_destroy(&bytes);
    CHECK(r_json_detached_take_handle(&detached, &input).status == R_STD_JSON_CALL_SUCCESS);
    CHECK(input.handle != NULL);
    CHECK(r_json_detached_take_handle(&detached, &input).status == R_STD_JSON_CALL_JSON_ERROR);
    r_std_io_input_destroy(&input);
    r_json_detached_destroy(&detached);
}
static void test_suspension_and_exclusion(RRuntimeAllocator *allocator) {
    atomic_store(&transport_starts, 0U);
    int writer;
    RStdIoInput input = pipe_input(allocator, &writer);
    RStdJsonReader reader;
    RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
    CHECK(r_json_reader_initialize(&reader, allocator, options, transport(), &input).status ==
          R_STD_JSON_CALL_SUCCESS);
    RStdJsonTaskStartResult first = start_read(&reader, true, (RStdJsonDeadline){0});
    CHECK(first.is_ok);
    wait_for_reads(1U);
    RStdJsonDetached detached;
    CHECK(r_json_reader_detach(&reader, &detached).status == R_STD_JSON_CALL_JSON_ERROR);
    ReadResult busy = read_next(&reader, true);
    CHECK(busy.outcome.status == R_STD_JSON_READ_JSON_ERROR &&
          busy.outcome.json.error.code == R_STD_JSON_ERROR_INVALID_STATE);
    read_drop(&busy);
    CHECK(write(writer, "\"part", 5U) == 5);
    wait_for_reads(2U);
    CHECK(write(writer, "ial\"", 4U) == 4);
    CHECK(close(writer) == 0);
    ReadResult result;
    CHECK(r_runtime_task_await(&first.task, &result) == R_RUNTIME_TASK_AWAIT_OK);
    CHECK(result.outcome.status == R_STD_JSON_READ_VALUE &&
          r_runtime_string_length(&result.value.string) == 7U);
    read_drop(&result);
    result = read_next(&reader, true);
    CHECK(result.outcome.status == R_STD_JSON_READ_END);
    read_drop(&result);
    r_json_reader_destroy(&reader);
}
static void test_error_and_cancellation(RRuntimeAllocator *allocator) {
    for (unsigned mode = 0U; mode < 3U; ++mode) {
        atomic_store(&transport_starts, 0U);
        int writer;
        RStdIoInput input = pipe_input(allocator, &writer);
        RStdJsonReader reader;
        CHECK(r_json_reader_initialize(
                  &reader, allocator, R_STD_JSON_DEFAULT_OPTIONS, transport(), &input)
                  .status == R_STD_JSON_CALL_SUCCESS);
        RStdJsonTaskStartResult start = start_read(&reader, true, (RStdJsonDeadline){0});
        CHECK(start.is_ok);
        unsigned drops_before = atomic_load(&text_drops);
        if (mode == 2U)
            CHECK(write(writer, "\"ready\"", 7U) == 7);
        else
            CHECK(write(writer, "\"partial", 8U) == 8);
        wait_for_reads(2U);
        if (mode == 0U) {
            CHECK(close(writer) == 0);
            ReadResult result;
            CHECK(r_runtime_task_await(&start.task, &result) == R_RUNTIME_TASK_AWAIT_OK);
            CHECK(result.outcome.status == R_STD_JSON_READ_JSON_ERROR &&
                  result.outcome.json.error.code == R_STD_JSON_ERROR_UNEXPECTED_EOF);
            read_drop(&result);
        } else {
            r_runtime_task_cancel(&start.task);
            CHECK(r_runtime_executor_lifecycle_stop());
            CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
            CHECK(close(writer) == 0);
        }
        CHECK(atomic_load(&text_drops) == drops_before + (mode == 2U ? 1U : 0U));
        ReadResult poisoned = read_next(&reader, true);
        CHECK(poisoned.outcome.status == R_STD_JSON_READ_JSON_ERROR &&
              poisoned.outcome.json.error.code == R_STD_JSON_ERROR_INVALID_STATE);
        read_drop(&poisoned);
        r_json_reader_destroy(&reader);
    }
}
static void test_failures(RRuntimeAllocator *allocator) {
    for (uint64_t failure = 1U; failure < 9U; ++failure) {
        int writer;
        RStdIoInput input = pipe_input(allocator, &writer);
        RStdJsonReader reader;
        RRuntimeDarwinIoHandle *original = input.handle;
        r_runtime_allocator_set_failure(allocator, failure);
        RStdJsonResult made = r_json_reader_initialize(
            &reader, allocator, R_STD_JSON_DEFAULT_OPTIONS, transport(), &input);
        r_runtime_allocator_set_failure(allocator, 0U);
        if (made.status != R_STD_JSON_CALL_SUCCESS)
            CHECK(input.handle == original && reader.state == NULL);
        else
            CHECK(input.handle == NULL);
        r_json_reader_destroy(&reader);
        r_std_io_input_destroy(&input);
        CHECK(close(writer) == 0);
    }
    int writer;
    RStdIoInput input = pipe_input(allocator, &writer);
    RStdJsonReader reader;
    CHECK(r_json_reader_initialize(
              &reader, allocator, R_STD_JSON_DEFAULT_OPTIONS, transport(), &input)
              .status == R_STD_JSON_CALL_SUCCESS);
    CHECK(write(writer, "\"kept\"", 6U) == 6);
    CHECK(close(writer) == 0);
    r_runtime_allocator_set_failure(allocator, 1U);
    RStdJsonTaskStartResult failed = start_read(&reader, true, (RStdJsonDeadline){0});
    CHECK(!failed.is_ok && failed.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    r_runtime_allocator_set_failure(allocator, 0U);
    ReadResult kept = read_next(&reader, true);
    CHECK(kept.outcome.status == R_STD_JSON_READ_VALUE &&
          r_runtime_string_length(&kept.value.string) == 4U);
    read_drop(&kept);
    r_json_reader_destroy(&reader);
}
static void test_deadline_and_shell_lifetime(RRuntimeAllocator *allocator) {
    int writer;
    RStdIoInput input = pipe_input(allocator, &writer);
    RStdJsonReader reader;
    CHECK(r_json_reader_initialize(
              &reader, allocator, R_STD_JSON_DEFAULT_OPTIONS, transport(), &input)
              .status == R_STD_JSON_CALL_SUCCESS);
    RStdJsonTaskStartResult expired = start_read(&reader, true, (RStdJsonDeadline){true, {0, 0}});
    CHECK(expired.is_ok);
    ReadResult result;
    CHECK(r_runtime_task_await(&expired.task, &result) == R_RUNTIME_TASK_AWAIT_OK);
    CHECK(result.outcome.status == R_STD_JSON_READ_TRANSPORT_ERROR &&
          result.outcome.transport_code == R_STD_IO_ERROR_TIMED_OUT);
    read_drop(&result);
    r_json_reader_destroy(&reader);
    CHECK(close(writer) == 0);
    input = pipe_input(allocator, &writer);
    CHECK(r_json_reader_initialize(
              &reader, allocator, R_STD_JSON_DEFAULT_OPTIONS, transport(), &input)
              .status == R_STD_JSON_CALL_SUCCESS);
    RStdJsonTaskStartResult pending = start_read(&reader, true, (RStdJsonDeadline){0});
    CHECK(pending.is_ok);
    r_json_reader_destroy(&reader);
    CHECK(write(writer, "\"retained\"", 10U) == 10);
    CHECK(close(writer) == 0);
    CHECK(r_runtime_task_await(&pending.task, &result) == R_RUNTIME_TASK_AWAIT_OK);
    CHECK(result.outcome.status == R_STD_JSON_READ_VALUE &&
          r_runtime_string_length(&result.value.string) == 8U);
    read_drop(&result);
}
static void file_move(void *output, void *source) {
    r_std_fs_file_move_initialize(output, source);
}
static void file_drop(void *value) {
    r_std_fs_file_destroy(value);
}
static RStdJsonTaskStartResult
file_start(const void *handle, RRuntimeArray *buffer, RStdJsonDeadline deadline) {
    RStdFsTaskStartResult started =
        r_std_fs_read(handle, buffer, (RStdFsDeadline){deadline.has_value, deadline.value});
    return (RStdJsonTaskStartResult){started.is_ok, started.task, started.error};
}
static void tcp_move(void *output, void *source) {
    r_std_net_tcp_stream_move_initialize(output, source);
}
static void tcp_drop(void *value) {
    r_std_net_tcp_stream_destroy(value);
}
static RStdJsonTaskStartResult
tcp_start(const void *handle, RRuntimeArray *buffer, RStdJsonDeadline deadline) {
    RStdNetTaskStartResult started =
        r_std_net_tcp_read(handle, buffer, (RStdNetDeadline){deadline.has_value, deadline.value});
    return (RStdJsonTaskStartResult){started.is_ok, started.task, started.error};
}
static void tcp_take(void *storage, RStdJsonTransportRead *output) {
    RStdNetTcpReadResult *read = storage;
    *output = (RStdJsonTransportRead){.buffer = read->buffer,
                                      .count = read->count,
                                      .end = read->kind == R_STD_NET_TCP_READ_RESULT_END,
                                      .failed = read->kind == R_STD_NET_TCP_READ_RESULT_FAILED,
                                      .code = (uint32_t)read->error.code,
                                      .native_code = read->error.native_code};
    *read = (RStdNetTcpReadResult){0};
}
static void test_empty_reader(RRuntimeAllocator *allocator) {
    for (unsigned mode = 0U; mode < 3U; ++mode) {
        int writer;
        RStdIoInput input = pipe_input(allocator, &writer);
        RStdJsonReader reader;
        RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
        options.mode = (RStdJsonMode)mode;
        CHECK(r_json_reader_initialize(&reader, allocator, options, transport(), &input).status ==
              R_STD_JSON_CALL_SUCCESS);
        CHECK(write(writer, " \n\t\r", 4U) == 4);
        CHECK(close(writer) == 0);
        ReadResult result = read_next(&reader, true);
        CHECK(result.outcome.status == R_STD_JSON_READ_END);
        read_drop(&result);
        result = read_next(&reader, false);
        CHECK(result.outcome.status == R_STD_JSON_READ_END);
        read_drop(&result);
        r_json_reader_destroy(&reader);
    }
}
static void test_file(RRuntimeAllocator *allocator) {
    char filename[] = "/private/tmp/r-json-reader-XXXXXX";
    int fd = mkstemp(filename);
    CHECK(fd >= 0);
    const char text[] = "\"from file\"";
    CHECK(write(fd, text, sizeof(text) - 1U) == (ssize_t)(sizeof(text) - 1U));
    CHECK(close(fd) == 0);
    RStdFsPathResult path = r_std_fs_path_from_utf8(
        allocator, (RStdStringView){(const uint8_t *)filename, strlen(filename)});
    CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    RStdFsTaskStartResult opened = r_std_fs_open_file(
        &path.value,
        (RStdFsOpenFileOptions){R_STD_FS_ACCESS_READ, R_STD_FS_CREATE_EXISTING, false, false, true},
        (RStdFsDeadline){0});
    CHECK(opened.is_ok);
    RStdFsFileResult file;
    CHECK(r_runtime_task_await(&opened.task, &file) == R_RUNTIME_TASK_AWAIT_OK &&
          file.r_tag == UINT32_C(0));
    CHECK(unlink(filename) == 0);
    r_std_fs_path_destroy(&path.value);
    RStdJsonTransport adapter = transport();
    adapter.handle_type =
        (RRuntimeTypeInfo){sizeof(RStdFsFile), alignof(RStdFsFile), file_move, file_drop};
    adapter.start = file_start;
    RStdJsonReader reader;
    CHECK(r_json_reader_initialize(
              &reader, allocator, R_STD_JSON_DEFAULT_OPTIONS, adapter, &file.r_payload.r_ok)
              .status == R_STD_JSON_CALL_SUCCESS);
    CHECK(file.r_payload.r_ok.storage == NULL);
    ReadResult result = read_next(&reader, true);
    CHECK(result.outcome.status == R_STD_JSON_READ_VALUE &&
          r_runtime_string_length(&result.value.string) == 9U);
    read_drop(&result);
    result = read_next(&reader, true);
    CHECK(result.outcome.status == R_STD_JSON_READ_END);
    read_drop(&result);
    r_json_reader_destroy(&reader);
}
static void test_tcp(RRuntimeAllocator *allocator) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(listener, (const struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t length = sizeof(address);
    CHECK(getsockname(listener, (struct sockaddr *)&address, &length) == 0);
    CHECK(listen(listener, 1) == 0);
    RStdNetSocketAddress remote = {
        .address = {.kind = R_STD_NET_IP_ADDRESS_V4, .bytes.v4 = {127, 0, 0, 1}},
        .port = ntohs(address.sin_port)};
    RStdNetTaskStartResult connected = r_std_net_tcp_connect(remote, (RStdNetDeadline){0});
    CHECK(connected.is_ok);
    int peer = accept(listener, NULL, NULL);
    CHECK(peer >= 0);
    CHECK(close(listener) == 0);
    RStdNetTcpStreamResult stream;
    CHECK(r_runtime_task_await(&connected.task, &stream) == R_RUNTIME_TASK_AWAIT_OK &&
          stream.r_tag == UINT32_C(0));
    RStdJsonTransport adapter = transport();
    adapter.handle_type =
        (RRuntimeTypeInfo){sizeof(RStdNetTcpStream), alignof(RStdNetTcpStream), tcp_move, tcp_drop};
    adapter.read_type =
        (RRuntimeTypeInfo){sizeof(RStdNetTcpReadResult), alignof(RStdNetTcpReadResult), NULL, NULL};
    adapter.start = tcp_start;
    adapter.take = tcp_take;
    adapter.deadline = NULL;
    RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
    options.mode = R_STD_JSON_MODE_ARRAY_ELEMENTS;
    RStdJsonReader reader;
    CHECK(r_json_reader_initialize(&reader, allocator, options, adapter, &stream.value).status ==
          R_STD_JSON_CALL_SUCCESS);
    CHECK(stream.value.storage == NULL);
    const char text[] = "[\"alpha\",\"beta\"]";
    CHECK(write(peer, text, sizeof(text) - 1U) == (ssize_t)(sizeof(text) - 1U));
    CHECK(close(peer) == 0);
    for (unsigned i = 0U; i < 2U; ++i) {
        ReadResult result = read_next(&reader, true);
        CHECK(result.outcome.status == R_STD_JSON_READ_VALUE &&
              r_runtime_string_length(&result.value.string) == (i == 0U ? 5U : 4U));
        read_drop(&result);
    }
    ReadResult end = read_next(&reader, true);
    CHECK(end.outcome.status == R_STD_JSON_READ_END);
    read_drop(&end);
    r_json_reader_destroy(&reader);
}
int main(void) {
    RRuntimeAllocator allocator;
    r_runtime_allocator_initialize(&allocator);
    CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    RRuntimeDarwinFsServiceStartResult service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);
    test_empty_reader(&allocator);
    test_file(&allocator);
    test_tcp(&allocator);
    test_sequence(&allocator);
    test_suspension_and_exclusion(&allocator);
    test_error_and_cancellation(&allocator);
    test_failures(&allocator);
    test_deadline_and_shell_lifetime(&allocator);
    CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_fs_service_stop();
    puts("library_json_reader_tests: ok");
    return 0;
}
