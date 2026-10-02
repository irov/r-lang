module ffi.verify_mismatch;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    c_long probe_increment(c_long value);
}

i32 main() {
    unsafe {
        c_long value = probe_increment(1i64 as c_long);
        return value as i32 - 2;
    }
}
