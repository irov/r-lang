#include "r_runtime_0_1.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

enum {
    /* The line of a report: its fixed part and up to R_RUNTIME_PANIC_TEXT_CAPACITY bytes of
       text. */
    R_RUNTIME_PANIC_BUFFER_CAPACITY = 128 + 2 + R_RUNTIME_PANIC_TEXT_CAPACITY + 1,
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

static void r_runtime_panic_buffer_append_place(RRuntimePanicBuffer *buffer,
                                                RRuntimePanicCategory category,
                                                RRuntimeSourceSpan span) {
    r_runtime_panic_buffer_append_text(buffer, "R panic: ");
    r_runtime_panic_buffer_append_text(buffer, r_runtime_panic_category_name(category));
    r_runtime_panic_buffer_append_text(buffer, " at module ");
    r_runtime_panic_buffer_append_u32(buffer, span.module);
    r_runtime_panic_buffer_append_text(buffer, " bytes [");
    r_runtime_panic_buffer_append_u32(buffer, span.start);
    r_runtime_panic_buffer_append_byte(buffer, ',');
    r_runtime_panic_buffer_append_u32(buffer, span.end);
    r_runtime_panic_buffer_append_byte(buffer, ')');
}

static void r_runtime_panic_write_line(RRuntimePanicCategory category, RRuntimeSourceSpan span) {
    RRuntimePanicBuffer buffer;

    buffer.length = 0U;
    r_runtime_panic_buffer_append_place(&buffer, category, span);
    r_runtime_panic_buffer_append_byte(&buffer, '\n');
    r_runtime_panic_write_diagnostic(&buffer);
}

/* The line of a report: the place and, for an explicit panic, `: ` and its message. */
static void r_runtime_panic_write_report(const RRuntimePanicReportData *report) {
    RRuntimePanicBuffer buffer;
    uint32_t index;

    buffer.length = 0U;
    r_runtime_panic_buffer_append_place(&buffer, report->category, report->span);
    if (report->text_length != 0U) {
        r_runtime_panic_buffer_append_text(&buffer, ": ");
        for (index = 0U; index < report->text_length; ++index) {
            r_runtime_panic_buffer_append_byte(&buffer, report->text[index]);
        }
    }
    r_runtime_panic_buffer_append_byte(&buffer, '\n');
    r_runtime_panic_write_diagnostic(&buffer);
}

_Noreturn void r_runtime_panic(RRuntimePanicCategory category, RRuntimeSourceSpan span) {
    r_runtime_panic_write_line(category, span);
    abort();
}

/* The unwind state of a thread (Core R-ERR-0005): no panic, a pending panic that propagates to the
   callers, or the cleanup of a frame that runs drops and finally blocks for it. */
typedef enum RRuntimeUnwindPhase {
    R_RUNTIME_UNWIND_NONE = 0,
    R_RUNTIME_UNWIND_PENDING,
    R_RUNTIME_UNWIND_CLEANUP
} RRuntimeUnwindPhase;

typedef struct RRuntimeUnwindState {
    RRuntimeUnwindPhase phase;
    RRuntimePanicReportData report;
} RRuntimeUnwindState;

_Atomic uint32_t r_runtime_unwinding_threads = 0U;

_Noreturn void r_runtime_panic_second(const RRuntimePanicReportData *first,
                                      RRuntimePanicCategory category,
                                      RRuntimeSourceSpan span) {
    r_runtime_panic_write_report(first);
    r_runtime_panic_write_line(category, span);
    abort();
}

static _Thread_local RRuntimeUnwindState r_runtime_unwind_state;

_Bool r_runtime_unwinding_current_thread(void) {
    return r_runtime_unwind_state.phase == R_RUNTIME_UNWIND_PENDING;
}

_Bool r_runtime_panicking(void) {
    return r_runtime_unwind_state.phase != R_RUNTIME_UNWIND_NONE;
}

static void r_runtime_unwind_set_pending(void) {
    r_runtime_unwind_state.phase = R_RUNTIME_UNWIND_PENDING;
    atomic_fetch_add_explicit(&r_runtime_unwinding_threads, 1U, memory_order_relaxed);
}

static void r_runtime_unwind_clear_pending(void) {
    r_runtime_unwind_state.phase = R_RUNTIME_UNWIND_NONE;
    atomic_fetch_sub_explicit(&r_runtime_unwinding_threads, 1U, memory_order_relaxed);
}

void r_runtime_raise_text(RRuntimePanicCategory category,
                          RRuntimeSourceSpan span,
                          const uint8_t *text,
                          size_t length) {
    RRuntimePanicReportData report;
    size_t index;

    report.category = category;
    report.span = span;
    if (length > (size_t)R_RUNTIME_PANIC_TEXT_CAPACITY) {
        /* The text stays valid UTF-8: the cut moves back to the start of a scalar. */
        length = (size_t)R_RUNTIME_PANIC_TEXT_CAPACITY;
        while ((length != 0U) && ((text[length] & 0xC0U) == 0x80U)) {
            length -= 1U;
        }
    }
    for (index = 0U; index < length; ++index) {
        report.text[index] = (char)text[index];
    }
    report.text_length = (uint32_t)length;
    if (r_runtime_unwind_state.phase != R_RUNTIME_UNWIND_NONE) {
        /* R-ERR-0008: a panic during cleanup is a second panic; a panic while one is pending
           means generated code missed the test after a call, which is a panic-runtime failure
           (R-ERR-0006). Both abort with the diagnostics of the first and the new panic. */
        r_runtime_panic_write_report(&r_runtime_unwind_state.report);
        r_runtime_panic_write_report(&report);
        abort();
    }
    r_runtime_unwind_state.report = report;
    r_runtime_unwind_set_pending();
}

void r_runtime_raise(RRuntimePanicCategory category, RRuntimeSourceSpan span) {
    r_runtime_raise_text(category, span, NULL, 0U);
}

void r_runtime_unwind_cleanup_enter(void) {
    if (r_runtime_unwind_state.phase != R_RUNTIME_UNWIND_PENDING) {
        r_runtime_panic_write_line(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0});
        abort();
    }
    r_runtime_unwind_state.phase = R_RUNTIME_UNWIND_CLEANUP;
    atomic_fetch_sub_explicit(&r_runtime_unwinding_threads, 1U, memory_order_relaxed);
}

void r_runtime_unwind_cleanup_leave(void) {
    if (r_runtime_unwind_state.phase != R_RUNTIME_UNWIND_CLEANUP) {
        r_runtime_panic_write_line(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0});
        abort();
    }
    r_runtime_unwind_set_pending();
}

_Bool r_runtime_panic_take(RRuntimePanicReportData *report) {
    if (r_runtime_unwind_state.phase != R_RUNTIME_UNWIND_PENDING) {
        return 0;
    }
    *report = r_runtime_unwind_state.report;
    r_runtime_unwind_clear_pending();
    return 1;
}

void r_runtime_panic_resume(const RRuntimePanicReportData *report) {
    if (r_runtime_unwind_state.phase != R_RUNTIME_UNWIND_NONE) {
        r_runtime_panic_write_report(report);
        abort();
    }
    r_runtime_unwind_state.report = *report;
    r_runtime_unwind_set_pending();
}

_Noreturn void r_runtime_panic_terminate(const RRuntimePanicReportData *report) {
    r_runtime_panic_write_report(report);
    abort();
}

_Noreturn void r_runtime_unwind_terminate(void) {
    r_runtime_panic_terminate(&r_runtime_unwind_state.report);
}

static _Atomic(RRuntimePanicSink) r_runtime_panic_sink;

void r_runtime_panic_set_sink(RRuntimePanicSink sink) {
    atomic_store_explicit(&r_runtime_panic_sink, sink, memory_order_release);
}

void r_runtime_panic_deliver(const RRuntimePanicReportData *report) {
    const RRuntimePanicSink sink =
        atomic_load_explicit(&r_runtime_panic_sink, memory_order_acquire);

    if ((sink != NULL) && sink(report)) {
        return;
    }
    r_runtime_panic_write_report(report);
}
