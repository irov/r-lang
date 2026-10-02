#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_std_fs.h"
#include "r_std_net.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static RRuntimeAllocator allocator;
static RRuntimeAllocator *test_allocator(void) {
    return &allocator;
}
int r_json_generated_main(int argc, char *argv[]);
#define r_runtime_hosted_allocator test_allocator
#define main r_json_generated_main
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_hosted_allocator

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value)) {                                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #value);                            \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
static RStdFsFile open_file(bool malformed) {
    char filename[] = "/private/tmp/r-json-source-XXXXXX";
    int fd = mkstemp(filename);
    CHECK(fd >= 0);
    const char source[] = "{\"id\":\"18446744073709551615\",\"text\":\"hello\"}";
    CHECK(write(fd, source, sizeof(source) - 1U) == (ssize_t)(sizeof(source) - 1U));
    if (malformed)
        CHECK(write(fd, "!", 1U) == 1);
    CHECK(close(fd) == 0);
    RStdFsPathResult path = r_std_fs_path_from_utf8(
        &allocator, (RStdStringView){(const uint8_t *)filename, strlen(filename)});
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
    return file.r_payload.r_ok;
}
static RStdNetTcpStream connect_tcp(void) {
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
    const char text[] = "[{\"id\":\"1\",\"text\":\"first\"},{\"id\":\"2\",\"text\":\"second\"}]";
    CHECK(write(peer, text, sizeof(text) - 1U) == (ssize_t)(sizeof(text) - 1U));
    CHECK(close(peer) == 0);
    return stream.value;
}
static int32_t run(unsigned transport, uint64_t failure, uint64_t *attempts) {
    RRuntimeTaskStartResult started;
    RStdFsFile file = {0};
    RStdNetTcpStream tcp = {0};
    if (transport != 1U) {
        file = open_file(transport == 2U);
        r_async_wrapper_context_00000007 context = {&file};
        r_runtime_allocator_set_failure(&allocator, failure);
        started = r_async_start_00000007_gate(r_async_wrapper_initialize_00000007_gate, &context, UINT32_C(0));
    } else {
        tcp = connect_tcp();
        r_async_wrapper_context_00000008 context = {&tcp};
        r_runtime_allocator_set_failure(&allocator, failure);
        started = r_async_start_00000008_gate(r_async_wrapper_initialize_00000008_gate, &context, UINT32_C(0));
    }
    int32_t result = 97;
    if (started.status == R_RUNTIME_TASK_START_OK) {
        CHECK(file.storage == NULL && tcp.storage == NULL);
        CHECK(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK);
    } else
        CHECK(started.status == R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    *attempts = r_runtime_allocator_attempt_count(&allocator);
    r_runtime_allocator_set_failure(&allocator, 0U);
    r_std_fs_file_destroy(&file);
    r_std_net_tcp_stream_destroy(&tcp);
    return result;
}
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    CHECK(r_runtime_stack_initialize_current_thread());
    r_runtime_allocator_initialize(&allocator);
    CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    RRuntimeDarwinFsServiceStartResult service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);
    uint64_t malformed_attempts;
    CHECK(run(2U, 0U, &malformed_attempts) == 98);
    for (uint64_t failure = 1U; failure <= malformed_attempts; ++failure) {
        uint64_t attempts;
        int32_t result = run(2U, failure, &attempts);
        CHECK(result == 98 || result == 99 || result == 97);
    }
    for (unsigned transport = 0U; transport < 2U; ++transport) {
        uint64_t attempts;
        int32_t baseline = run(transport, 0U, &attempts);
        if (baseline != 0) {
            fprintf(stderr, "transport=%u baseline=%d\n", transport, baseline);
            return 101;
        }
        for (uint64_t failure = 1U; failure <= attempts + 1U; ++failure) {
            uint64_t actual;
            int32_t result = run(transport, failure, &actual);
            if (result != 0 && result != 97 && result != 99) {
                fprintf(stderr,
                        "transport=%u allocation=%llu/%llu result=%d\n",
                        transport,
                        (unsigned long long)failure,
                        (unsigned long long)attempts,
                        result);
                return 102;
            }
        }
    }
    CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_fs_service_stop();
    return 0;
}
