module runtime.panics;
import runtime.types;
import runtime.types::{PanicCategory, SourceSpan, PanicReport};

// Panics of the runtime (r_runtime_core.h, r_runtime_0_1.h): the bounded report line written to
// stderr, the unwind state of each thread (Core R-ERR-0005..0009) and the sink that takes an
// unobserved report first.
@header("r_runtime_core.h")
@header("r_runtime_0_1.h")
extern "C" {
}

@link(name = "system.libc", kind = "system")
@header("stdlib.h")
@header("unistd.h")
@header("errno.h")
protected extern "C" {
    @safety("R-RUNTIME-ABORT", "None")
    never abort();
    @safety("R-RUNTIME-WRITE", "bytes is valid for count bytes")
    c_long write(c_int descriptor, raw const void*? bytes, c_size count);
    @c_constant(name = "STDERR_FILENO")
    const c_int standard_error = 2i32 as c_int;
    @c_constant(name = "EINTR")
    const c_int interrupted = 4i32 as c_int;
}

// The place and up to the text capacity of a report: "R panic: " category " at module " N
// " bytes [" N "," N ")" ": " text "\n".
const usize text_capacity = 256usize;
const usize line_capacity = 128usize + 2usize + 256usize + 1usize;
const usize extra_interrupted_writes = 8usize;

struct Line {
    u8[387] bytes = {};
    usize length = 0usize;
};

protected void append_byte(Line* line, u8 byte) {
    if (line->length < line_capacity) {
        line->bytes[line->length] = byte;
        line->length += 1usize;
    }
}

protected void append_bytes(Line* line, const u8[] bytes) {
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        append_byte(line, bytes[index]);
    }
}

protected void append_text(Line* line, str text) {
    const u8[] bytes = text;
    append_bytes(line, bytes);
}

protected void append_number(Line* line, u32 value) {
    u8[10] digits = {};
    usize count = 0usize;
    u32 rest = value;
    while (true) {
        digits[count] = (48u32 + rest % 10u32) as u8;
        count += 1usize;
        rest /= 10u32;
        if (rest == 0u32) {
            break;
        }
    }
    while (count != 0usize) {
        count -= 1usize;
        append_byte(line, digits[count]);
    }
}

protected void write_line(const Line* line) {
    usize attempt = 0usize;
    usize offset = 0usize;
    while ((offset < line->length) && (attempt < line_capacity + extra_interrupted_writes)) {
        attempt += 1usize;
        unsafe {
            raw const void*? start = core::offset_bytes(&line->bytes[0] as raw const u8*, offset);
            c_long written = write(standard_error, start, (line->length - offset) as c_size);
            if (written > 0i64 as c_long) {
                offset += written as usize;
            } else {
                if ((written == 0i64 as c_long) || (core::c_errno() != interrupted)) {
                    break;
                }
            }
        }
    }
}

// The lowercase R-ERR-0004 name of a category, NUL-terminated for C. The reporting entries take
// the category as its integer: one that names no category is reported as a contract violation.
protected str category_text(u32 category) {
    switch (category) {
    case 1u32: return "explicit\0";
    case 2u32: return "bounds\0";
    case 3u32: return "integer_overflow\0";
    case 4u32: return "division_by_zero\0";
    case 5u32: return "invalid_shift\0";
    case 6u32: return "invalid_conversion\0";
    case 7u32: return "allocation_failure\0";
    case 8u32: return "reference_count_overflow\0";
    case 9u32: return "scoped_thread_panic\0";
    case 10u32: return "once_poisoned\0";
    case 11u32: return "thread_local_lifetime\0";
    case 12u32: return "stack_exhaustion\0";
    default: return "contract_violation\0";
    }
}

protected void append_place(Line* line, u32 category, SourceSpan span) {
    str name = category_text(category);
    const u8[] bytes = name;
    append_text(line, "R panic: ");
    append_bytes(line, bytes[0usize..len(bytes) - 1usize]);
    append_text(line, " at module ");
    append_number(line, span.module_id as u32);
    append_text(line, " bytes [");
    append_number(line, span.start as u32);
    append_byte(line, 44u8);
    append_number(line, span.end as u32);
    append_byte(line, 41u8);
}

protected void write_place(u32 category, SourceSpan span) {
    Line line = {};
    append_place(&line, category, span);
    append_byte(&line, 10u8);
    write_line(&line);
}

protected void write_report(raw const PanicReport* report) {
    Line line = {};
    unsafe {
        append_place(&line, report->category as u32, report->span);
        usize length = report->text_length as usize;
        if (length != 0usize) {
            // The text holds UTF-8 bytes in C chars, which may be negative: read them as bytes.
            raw const c_char* first = &report->text[0usize] as raw const c_char*;
            raw const u8* text = (first as raw const void*) as raw const u8*;
            append_text(&line, ": ");
            append_bytes(&line, core::slice_from_raw_parts(text, length));
        }
    }
    append_byte(&line, 10u8);
    write_line(&line);
}

// ---- The unwind state of a thread (Core R-ERR-0005) ----

const u32 phase_none = 0u32;
const u32 phase_pending = 1u32;
const u32 phase_cleanup = 2u32;

struct UnwindState {
    u32 phase;
    PanicReport report;
};

thread_local UnwindState unwind_state = {
    .phase = 0u32,
    .report = {.category = PanicCategory::R_RUNTIME_PANIC_EXPLICIT,
               .span = {.module_id = 0u32 as c_uint32, .start = 0u32 as c_uint32,
                        .end = 0u32 as c_uint32},
               .text_length = 0u32 as c_uint32, .text = {}}};

// The number of threads with a pending panic: the test after a call is one relaxed load of it
// until some thread panics.
@safety("R-RUNTIME-UNWINDING-THREADS", "read only, with relaxed atomic loads")
extern "C" atomic c_uint32 r_runtime_unwinding_threads = 0u32 as c_uint32;

atomic raw fn?(raw const PanicReport*) -> c_bool panic_sink = null;

protected SourceSpan no_span() {
    return SourceSpan {.module_id = 0u32 as c_uint32, .start = 0u32 as c_uint32,
                       .end = 0u32 as c_uint32};
}

protected void set_pending() {
    unwind_state.phase = phase_pending;
    core::atomic_fetch_add(&r_runtime_unwinding_threads, 1u32 as c_uint32,
                           core::memory_order::relaxed) as void;
}

protected void clear_pending() {
    unwind_state.phase = phase_none;
    core::atomic_fetch_sub(&r_runtime_unwinding_threads, 1u32 as c_uint32,
                           core::memory_order::relaxed) as void;
}

@safety("R-RUNTIME-PANIC", "None")
extern "C" never r_runtime_panic(c_uint category, SourceSpan span) {
    write_place(category as u32, span);
    unsafe {
        abort();
    }
}

@safety("R-RUNTIME-PANIC-CATEGORY-NAME", "None")
extern "C" raw const c_char* r_runtime_panic_category_name(c_uint category) {
    const u8[] bytes = category_text(category as u32);
    unsafe {
        raw const u8* first = &bytes[0usize] as raw const u8*;
        return (first as raw const void*) as raw const c_char*;
    }
}

@safety("R-RUNTIME-PANIC-SECOND", "first addresses a report")
extern "C" never r_runtime_panic_second(raw const PanicReport* first, c_uint category,
                                        SourceSpan span) {
    write_report(first);
    write_place(category as u32, span);
    unsafe {
        abort();
    }
}

@safety("R-RUNTIME-UNWINDING-CURRENT-THREAD", "None")
extern "C" c_bool r_runtime_unwinding_current_thread() {
    if (unwind_state.phase == phase_pending) {
        return 1i32 as c_bool;
    }
    return 0i32 as c_bool;
}

@safety("R-RUNTIME-PANICKING", "None")
extern "C" c_bool r_runtime_panicking() {
    if (unwind_state.phase != phase_none) {
        return 1i32 as c_bool;
    }
    return 0i32 as c_bool;
}

@safety("R-RUNTIME-RAISE-TEXT", "text is valid for length bytes of UTF-8, or length is zero")
extern "C" void r_runtime_raise_text(PanicCategory category, SourceSpan span,
                                     raw const c_uint8*? text, c_size length) {
    PanicReport report = {.category = category, .span = span, .text_length = 0u32 as c_uint32,
                          .text = {}};
    usize kept = length as usize;
    unsafe {
        if (kept > text_capacity) {
            // The text stays valid UTF-8: the cut moves back to the start of a scalar.
            const u8[] bytes =
                core::slice_from_raw_parts((text as raw const void*?) as raw const u8*?, kept);
            kept = text_capacity;
            while (((bytes[kept] as u32) & 0xC0u32) == 0x80u32) {
                kept -= 1usize;
                if (kept == 0usize) {
                    break;
                }
            }
        }
        if (kept != 0usize) {
            raw c_char* destination = &report.text[0usize] as raw c_char*;
            runtime.types::memcpy(destination as raw void*, text as raw const void*?,
                                  kept as c_size) as void;
        }
    }
    report.text_length = kept as c_uint32;
    if (unwind_state.phase != phase_none) {
        // R-ERR-0008: a panic during cleanup is a second panic; a panic while one is pending
        // means generated code missed the test after a call, which is a panic-runtime failure
        // (R-ERR-0006). Both abort with the diagnostics of the first and the new panic.
        unsafe {
            write_report(&unwind_state.report as raw const PanicReport*);
            write_report(&report as raw const PanicReport*);
            abort();
        }
    }
    unwind_state.report = report;
    set_pending();
}

@safety("R-RUNTIME-RAISE", "None")
extern "C" void r_runtime_raise(PanicCategory category, SourceSpan span) {
    r_runtime_raise_text(category, span, null, 0usize as c_size);
}

@safety("R-RUNTIME-UNWIND-CLEANUP-ENTER", "a panic of this thread is pending")
extern "C" void r_runtime_unwind_cleanup_enter() {
    if (unwind_state.phase != phase_pending) {
        r_runtime_panic(13u32 as c_uint, no_span());
    }
    unwind_state.phase = phase_cleanup;
    core::atomic_fetch_sub(&r_runtime_unwinding_threads, 1u32 as c_uint32,
                           core::memory_order::relaxed) as void;
}

@safety("R-RUNTIME-UNWIND-CLEANUP-LEAVE", "the cleanup of this thread's panic runs")
extern "C" void r_runtime_unwind_cleanup_leave() {
    if (unwind_state.phase != phase_cleanup) {
        r_runtime_panic(13u32 as c_uint, no_span());
    }
    set_pending();
}

@safety("R-RUNTIME-PANIC-TAKE", "report is writable")
extern "C" c_bool r_runtime_panic_take(raw PanicReport* report) {
    if (unwind_state.phase != phase_pending) {
        return 0i32 as c_bool;
    }
    unsafe {
        *report = unwind_state.report;
    }
    clear_pending();
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-PANIC-RESUME", "report addresses a report")
extern "C" void r_runtime_panic_resume(raw const PanicReport* report) {
    if (unwind_state.phase != phase_none) {
        write_report(report);
        unsafe {
            abort();
        }
    }
    unsafe {
        unwind_state.report = *report;
    }
    set_pending();
}

@safety("R-RUNTIME-PANIC-TERMINATE", "report addresses a report")
extern "C" never r_runtime_panic_terminate(raw const PanicReport* report) {
    write_report(report);
    unsafe {
        abort();
    }
}

@safety("R-RUNTIME-UNWIND-TERMINATE", "None")
extern "C" never r_runtime_unwind_terminate() {
    unsafe {
        r_runtime_panic_terminate(&unwind_state.report as raw const PanicReport*);
    }
}

@safety("R-RUNTIME-PANIC-SET-SINK", "sink is null or neither blocks nor runs R code")
extern "C" void r_runtime_panic_set_sink(raw fn?(raw const PanicReport*) -> c_bool sink) {
    core::atomic_store(&panic_sink, sink, core::memory_order::release);
}

@safety("R-RUNTIME-PANIC-DELIVER", "report addresses a report")
extern "C" void r_runtime_panic_deliver(raw const PanicReport* report) {
    raw fn?(raw const PanicReport*) -> c_bool sink =
        core::atomic_load(&panic_sink, core::memory_order::acquire);
    unsafe {
        if ((sink != null) && (sink(report) != 0i32 as c_bool)) {
            return;
        }
    }
    write_report(report);
}
