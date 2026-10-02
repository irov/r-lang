module ffi.variadic_bad_argument;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-SUM", "count shall equal the number of int arguments that follow")
    c_int probe_sum(c_int count, ...);
}

i32 main() {
    c_int total = 0i32 as c_int;
    unsafe {
        total = probe_sum(1i32 as c_int, 7i16 as c_short);
    }
    return total as i32;
}
