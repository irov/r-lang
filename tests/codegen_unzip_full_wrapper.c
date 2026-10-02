#define _DARWIN_C_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main

enum {
    R_TEST_PATH_CAPACITY = 1024,
    R_TEST_ARCHIVE_CAPACITY = 1024,
};

static _Bool r_test_append_bytes(
    uint8_t *archive, size_t capacity, size_t *length, const void *bytes, size_t byte_count) {
    if ((archive == NULL) || (length == NULL) || (bytes == NULL) || (*length > capacity) ||
        (byte_count > capacity - *length)) {
        return 0;
    }
    (void)memcpy(archive + *length, bytes, byte_count);
    *length += byte_count;
    return 1;
}

static _Bool r_test_append_u16(uint8_t *archive, size_t capacity, size_t *length, uint16_t value) {
    const uint8_t bytes[] = {
        (uint8_t)(value & UINT16_C(0x00ff)),
        (uint8_t)((value >> UINT16_C(8)) & UINT16_C(0x00ff)),
    };

    return r_test_append_bytes(archive, capacity, length, bytes, sizeof(bytes));
}

static _Bool r_test_append_u32(uint8_t *archive, size_t capacity, size_t *length, uint32_t value) {
    const uint8_t bytes[] = {
        (uint8_t)(value & UINT32_C(0x000000ff)),
        (uint8_t)((value >> UINT32_C(8)) & UINT32_C(0x000000ff)),
        (uint8_t)((value >> UINT32_C(16)) & UINT32_C(0x000000ff)),
        (uint8_t)((value >> UINT32_C(24)) & UINT32_C(0x000000ff)),
    };

    return r_test_append_bytes(archive, capacity, length, bytes, sizeof(bytes));
}

static uint32_t r_test_crc32(const uint8_t *bytes, size_t length) {
    uint32_t crc = UINT32_MAX;
    size_t index;

    for (index = 0U; index < length; ++index) {
        unsigned bit;

        crc ^= (uint32_t)bytes[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = UINT32_C(0) - (crc & UINT32_C(1));

            crc = (crc >> UINT32_C(1)) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static _Bool r_test_write_archive(const char *path) {
    static const uint8_t file_name[] = "nested/hello.txt";
    static const uint8_t payload[] = "R language unzip\n";
    uint8_t archive[R_TEST_ARCHIVE_CAPACITY];
    const size_t file_name_length = sizeof(file_name) - 1U;
    const size_t payload_length = sizeof(payload) - 1U;
    const uint32_t crc = r_test_crc32(payload, payload_length);
    const uint32_t payload_size = (uint32_t)payload_length;
    size_t length = 0U;
    size_t central_offset;
    size_t central_size;
    FILE *stream;
    _Bool valid;

    if ((file_name_length > (size_t)UINT16_MAX) || (payload_length > (size_t)UINT32_MAX)) {
        return 0;
    }

    valid = r_test_append_u32(archive, sizeof(archive), &length, UINT32_C(0x04034b50)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(20)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u32(archive, sizeof(archive), &length, crc) &&
            r_test_append_u32(archive, sizeof(archive), &length, payload_size) &&
            r_test_append_u32(archive, sizeof(archive), &length, payload_size) &&
            r_test_append_u16(archive, sizeof(archive), &length, (uint16_t)file_name_length) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_bytes(archive, sizeof(archive), &length, file_name, file_name_length) &&
            r_test_append_bytes(archive, sizeof(archive), &length, payload, payload_length);
    if (!valid) {
        return 0;
    }

    central_offset = length;
    valid = r_test_append_u32(archive, sizeof(archive), &length, UINT32_C(0x02014b50)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(20)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(20)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u32(archive, sizeof(archive), &length, crc) &&
            r_test_append_u32(archive, sizeof(archive), &length, payload_size) &&
            r_test_append_u32(archive, sizeof(archive), &length, payload_size) &&
            r_test_append_u16(archive, sizeof(archive), &length, (uint16_t)file_name_length) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u32(archive, sizeof(archive), &length, UINT32_C(0)) &&
            r_test_append_u32(archive, sizeof(archive), &length, UINT32_C(0)) &&
            r_test_append_bytes(archive, sizeof(archive), &length, file_name, file_name_length);
    if (!valid) {
        return 0;
    }

    central_size = length - central_offset;
    if ((central_offset > (size_t)UINT32_MAX) || (central_size > (size_t)UINT32_MAX)) {
        return 0;
    }
    valid = r_test_append_u32(archive, sizeof(archive), &length, UINT32_C(0x06054b50)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(1)) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(1)) &&
            r_test_append_u32(archive, sizeof(archive), &length, (uint32_t)central_size) &&
            r_test_append_u32(archive, sizeof(archive), &length, (uint32_t)central_offset) &&
            r_test_append_u16(archive, sizeof(archive), &length, UINT16_C(0));
    if (!valid) {
        return 0;
    }

    stream = fopen(path, "wb");
    if (stream == NULL) {
        return 0;
    }
    valid = fwrite(archive, 1U, length, stream) == length;
    if (fclose(stream) != 0) {
        valid = 0;
    }
    return valid;
}

static _Bool
r_test_join_path(char *output, size_t output_capacity, const char *left, const char *right) {
    const int length = snprintf(output, output_capacity, "%s/%s", left, right);

    return (length >= 0) && ((size_t)length < output_capacity);
}

static _Bool r_test_file_equals(const char *path, const uint8_t *expected, size_t expected_length) {
    uint8_t observed[64];
    FILE *stream = fopen(path, "rb");
    long file_length;
    size_t observed_length;
    _Bool valid;

    if ((stream == NULL) || (expected_length > sizeof(observed))) {
        if (stream != NULL) {
            (void)fclose(stream);
        }
        return 0;
    }
    if ((fseek(stream, 0L, SEEK_END) != 0) || ((file_length = ftell(stream)) < 0L) ||
        (fseek(stream, 0L, SEEK_SET) != 0) || ((size_t)file_length != expected_length)) {
        (void)fclose(stream);
        return 0;
    }
    observed_length = fread(observed, 1U, expected_length, stream);
    valid =
        (observed_length == expected_length) && (memcmp(observed, expected, expected_length) == 0);
    if (fclose(stream) != 0) {
        valid = 0;
    }
    return valid;
}

int main(void) {
    static const uint8_t expected[] = "R language unzip\n";
    char root_template[] = "/tmp/r-unzip-codegen-XXXXXX";
    char archive_path[R_TEST_PATH_CAPACITY];
    char output_path[R_TEST_PATH_CAPACITY];
    char nested_path[R_TEST_PATH_CAPACITY];
    char output_file[R_TEST_PATH_CAPACITY];
    char *root = mkdtemp(root_template);
    char *arguments[] = {"unzip", archive_path, output_path, NULL};
    int status = EXIT_FAILURE;
    _Bool paths_ready = 0;
    _Bool archive_created = 0;
    _Bool output_created = 0;

    if (root == NULL) {
        return EXIT_FAILURE;
    }
    if (!r_test_join_path(archive_path, sizeof(archive_path), root, "archive.zip") ||
        !r_test_join_path(output_path, sizeof(output_path), root, "output") ||
        !r_test_join_path(nested_path, sizeof(nested_path), output_path, "nested") ||
        !r_test_join_path(output_file, sizeof(output_file), nested_path, "hello.txt")) {
        goto cleanup;
    }
    paths_ready = 1;
    if (!r_test_write_archive(archive_path)) {
        goto cleanup;
    }
    archive_created = 1;
    if (mkdir(output_path, S_IRWXU) != 0) {
        goto cleanup;
    }
    output_created = 1;

    status = r_generated_main(3, arguments);
    if ((status == EXIT_SUCCESS) &&
        !r_test_file_equals(output_file, expected, sizeof(expected) - 1U)) {
        status = EXIT_FAILURE;
    }

cleanup:
    if (paths_ready && (unlink(output_file) != 0) && (errno != ENOENT) &&
        (status == EXIT_SUCCESS)) {
        status = EXIT_FAILURE;
    }
    if (paths_ready && (rmdir(nested_path) != 0) && (errno != ENOENT) && (status == EXIT_SUCCESS)) {
        status = EXIT_FAILURE;
    }
    if (output_created && (rmdir(output_path) != 0) && (status == EXIT_SUCCESS)) {
        status = EXIT_FAILURE;
    }
    if (archive_created && (unlink(archive_path) != 0) && (status == EXIT_SUCCESS)) {
        status = EXIT_FAILURE;
    }
    if ((rmdir(root) != 0) && (status == EXIT_SUCCESS)) {
        status = EXIT_FAILURE;
    }
    return status;
}
