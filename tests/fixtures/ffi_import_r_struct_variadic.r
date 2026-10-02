module ffi.import_r_struct_variadic;

/* R-FFI-0057: a forwarding function cannot re-pass a variable argument list. */
@repr(C)
struct Point {
    c_int x;
    c_int y;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @safety("PROBE-POINT-LOG", "The arguments are promoted C values")
    c_int probe_point_log(Point point, ...);
}

i32 main() {
    return 0;
}
