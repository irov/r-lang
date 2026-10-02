module ffi.import_missing_symbol;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-MISSING", "The function has no preconditions")
    c_int probe_missing(c_int value);
}

i32 main() {
    unsafe {
        c_int value = probe_missing(1i32 as c_int);
        return value as i32;
    }
}
