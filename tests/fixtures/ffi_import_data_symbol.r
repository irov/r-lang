module ffi.import_data_symbol;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-COUNTER", "The function has no preconditions")
    c_int probe_counter(c_int value);
}

i32 main() {
    unsafe {
        c_int value = probe_counter(1i32 as c_int);
        return value as i32;
    }
}
