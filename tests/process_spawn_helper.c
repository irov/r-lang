#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_all(int descriptor, const char *bytes, size_t length) {
    while (length != 0U) {
        ssize_t written = write(descriptor, bytes, length);

        if (written < (ssize_t)0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (written == (ssize_t)0) {
            return 0;
        }
        bytes += (size_t)written;
        length -= (size_t)written;
    }
    return 1;
}

static int write_text(int descriptor, const char *text) {
    return write_all(descriptor, text, strlen(text));
}

static int write_report(const char *path, int argc, char *argv[]) {
    char working_directory[4096];
    const char *environment = getenv("R_PROCESS_SPAWN_VALUE");
    int descriptor;
    int index;

    if (getcwd(working_directory, sizeof(working_directory)) == NULL) {
        return 20;
    }
    descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, (mode_t)0600);
    if (descriptor < 0) {
        return 21;
    }
    if (!write_text(descriptor, "cwd=") || !write_text(descriptor, working_directory) ||
        !write_text(descriptor, "\nenv=") ||
        !write_text(descriptor, environment == NULL ? "<missing>" : environment) ||
        !write_text(descriptor, "\n")) {
        (void)close(descriptor);
        return 22;
    }
    for (index = 0; index < argc; ++index) {
        char prefix[64];
        int prefix_length =
            snprintf(prefix, sizeof(prefix), "arg%d[%zu]=", index, strlen(argv[index]));

        if (prefix_length < 0 || (size_t)prefix_length >= sizeof(prefix) ||
            !write_all(descriptor, prefix, (size_t)prefix_length) ||
            !write_text(descriptor, argv[index]) || !write_text(descriptor, "\n")) {
            (void)close(descriptor);
            return 23;
        }
    }
    return close(descriptor) == 0 ? 0 : 24;
}

static int create_marker(const char *path) {
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, (mode_t)0600);

    if (descriptor < 0) {
        return 30;
    }
    return close(descriptor) == 0 ? 0 : 31;
}

static int read_one(int descriptor, unsigned char *value) {
    for (;;) {
        const ssize_t count = read(descriptor, value, 1U);

        if (count == (ssize_t)1) {
            return 1;
        }
        if (count < (ssize_t)0 && errno == EINTR) {
            continue;
        }
        return 0;
    }
}

static int pipe_triplet(void) {
    unsigned char input;
    unsigned char error;

    if (!read_one(STDIN_FILENO, &input)) {
        return 40;
    }
    error = input ^ 0x20U;
    if (!write_all(STDOUT_FILENO, (const char *)&input, 1U)) {
        return 41;
    }
    if (!write_all(STDERR_FILENO, (const char *)&error, 1U)) {
        return 42;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc >= 3 && strcmp(argv[1], "--report") == 0) {
        return write_report(argv[2], argc, argv);
    }
    if (argc == 3 && strcmp(argv[1], "--marker") == 0) {
        return create_marker(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "--hold") == 0) {
        int result = create_marker(argv[2]);

        if (result != 0) {
            return result;
        }
        for (;;) {
            (void)pause();
        }
    }
    if (argc == 2 && strcmp(argv[1], "--pause") == 0) {
        for (;;) {
            (void)pause();
        }
    }
    if (argc == 2 && strcmp(argv[1], "--exit-zero") == 0) {
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--pipe-triplet") == 0) {
        return pipe_triplet();
    }
    return 10;
}
