#include "r_runtime_0_1.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

enum {
    R_RUNTIME_PANIC_BUFFER_CAPACITY = 128,
    R_RUNTIME_PANIC_EXTRA_INTERRUPTED_WRITES = 8
};

typedef struct RRuntimePanicBuffer {
    char bytes[R_RUNTIME_PANIC_BUFFER_CAPACITY];
    size_t length;
} RRuntimePanicBuffer;

static void r_runtime_panic_buffer_append_byte(RRuntimePanicBuffer *buffer, char byte) {
    if (buffer->length < sizeof(buffer->bytes)) {
        buffer->bytes[buffer->length] = byte;
        buffer->length += 1U;
    }
}

static void r_runtime_panic_buffer_append_text(RRuntimePanicBuffer *buffer, const char *text) {
    while (*text != '\0' && buffer->length < sizeof(buffer->bytes)) {
        r_runtime_panic_buffer_append_byte(buffer, *text);
        text += 1;
    }
}

static void r_runtime_panic_buffer_append_u32(RRuntimePanicBuffer *buffer, uint32_t value) {
    char digits[10];
    size_t length = 0U;

    do {
        uint32_t digit = value % UINT32_C(10);

        digits[length] = (char)((uint32_t)'0' + digit);
        length += 1U;
        value /= UINT32_C(10);
    } while (value != UINT32_C(0));

    while (length != 0U) {
        length -= 1U;
        r_runtime_panic_buffer_append_byte(buffer, digits[length]);
    }
}

static void r_runtime_panic_write_diagnostic(const RRuntimePanicBuffer *buffer) {
    size_t attempt = 0U;
    size_t offset = 0U;
    const size_t attempt_limit =
        sizeof(buffer->bytes) + (size_t)R_RUNTIME_PANIC_EXTRA_INTERRUPTED_WRITES;

    while (offset < buffer->length && attempt < attempt_limit) {
        ssize_t written;

        attempt += 1U;
        written = write(STDERR_FILENO, buffer->bytes + offset, buffer->length - offset);
        if (written > 0) {
            offset += (size_t)written;
        } else if (written == 0 || errno != EINTR) {
            break;
        }
    }
}

const char *r_runtime_panic_category_name(RRuntimePanicCategory category) {
    switch (category) {
    case R_RUNTIME_PANIC_EXPLICIT:
        return "explicit";
    case R_RUNTIME_PANIC_BOUNDS:
        return "bounds";
    case R_RUNTIME_PANIC_INTEGER_OVERFLOW:
        return "integer_overflow";
    case R_RUNTIME_PANIC_DIVISION_BY_ZERO:
        return "division_by_zero";
    case R_RUNTIME_PANIC_INVALID_SHIFT:
        return "invalid_shift";
    case R_RUNTIME_PANIC_INVALID_CONVERSION:
        return "invalid_conversion";
    case R_RUNTIME_PANIC_ALLOCATION_FAILURE:
        return "allocation_failure";
    case R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW:
        return "reference_count_overflow";
    case R_RUNTIME_PANIC_SCOPED_THREAD_PANIC:
        return "scoped_thread_panic";
    case R_RUNTIME_PANIC_ONCE_POISONED:
        return "once_poisoned";
    case R_RUNTIME_PANIC_THREAD_LOCAL_LIFETIME:
        return "thread_local_lifetime";
    case R_RUNTIME_PANIC_STACK_EXHAUSTION:
        return "stack_exhaustion";
    case R_RUNTIME_PANIC_CONTRACT_VIOLATION:
        return "contract_violation";
    default:
        return "contract_violation";
    }
}

_Noreturn void r_runtime_panic(RRuntimePanicCategory category, RRuntimeSourceSpan span) {
    RRuntimePanicBuffer buffer;

    buffer.length = 0U;
    r_runtime_panic_buffer_append_text(&buffer, "R panic: ");
    r_runtime_panic_buffer_append_text(&buffer, r_runtime_panic_category_name(category));
    r_runtime_panic_buffer_append_text(&buffer, " at module ");
    r_runtime_panic_buffer_append_u32(&buffer, span.module);
    r_runtime_panic_buffer_append_text(&buffer, " bytes [");
    r_runtime_panic_buffer_append_u32(&buffer, span.start);
    r_runtime_panic_buffer_append_byte(&buffer, ',');
    r_runtime_panic_buffer_append_u32(&buffer, span.end);
    r_runtime_panic_buffer_append_text(&buffer, ")\n");
    r_runtime_panic_write_diagnostic(&buffer);
    abort();
}
