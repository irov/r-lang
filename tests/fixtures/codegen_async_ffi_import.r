module test.codegen.async_ffi_import;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    c_int probe_increment(c_int value);

    @safety("PROBE-APPLY", "callback shall be a valid C function pointer for the whole call")
    c_int probe_apply(raw fn(c_int) -> c_int callback, c_int value);

    @safety("PROBE-NAME", "The returned pointer addresses an immutable NUL-terminated string")
    raw const c_char* probe_name();

    @safety("PROBE-TEXT-LENGTH", "text shall address a NUL-terminated string")
    c_size probe_text_length(raw const c_char* text);
}

@callback
@safety("PROBE-DOUBLE", "The runtime is initialized and the caller may enter R")
extern "C" c_int probe_double(c_int value) {
    return value + value;
}

protected async i32 probe_all() {
    i32 outcome = 0;
    unsafe {
        c_int incremented = probe_increment(41i32 as c_int);
        c_int applied = probe_apply(probe_double, 21i32 as c_int);
        raw const c_char* name = probe_name();
        c_size length = probe_text_length(name);
        if (incremented != 42i32 as c_int) {
            outcome = 1;
        }
        if (applied != 42i32 as c_int) {
            outcome = 2;
        }
        if (length != 5usize as c_size) {
            outcome = 3;
        }
    }
    return outcome;
}

async i32 main() {
    try {
        i32 outcome = await probe_all();
        return outcome;
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
}
