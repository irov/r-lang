#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/* The C mirror of bench/async_fs_read_into.r: blocking read(2) calls of 4 KiB from a regular
   file, rewinding with lseek(2) at end of file. */
int main(void) {
    const size_t iterations = (size_t)20000u;
    static unsigned char buffer[4096];
    size_t total = 0u;
    size_t index;
    int descriptor = open("/usr/share/dict/words", O_RDONLY);

    if (descriptor < 0) {
        return 66;
    }
    for (index = 0u; index < iterations; index += 1u) {
        ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count < 0) {
            (void)close(descriptor);
            return 74;
        }
        if (count == 0 && lseek(descriptor, 0, SEEK_SET) < 0) {
            (void)close(descriptor);
            return 74;
        }
        total += (size_t)count;
    }
    (void)close(descriptor);
    return (int)(total % 109u);
}
