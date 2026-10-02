module ffi.variadic_no_fixed;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-SUM", "count shall equal the number of int arguments that follow")
    c_int probe_sum(...);
}

i32 main() {
    return 0;
}
