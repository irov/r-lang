module ffi.record_r_struct_shape;

/* R-FFI-0021: probe_point_sum takes struct probe_point by value, not a pointer to it. */
@repr(C)
struct Point {
    c_int x;
    c_int y;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @safety("PROBE-POINT-SUM", "The function has no preconditions")
    c_int probe_point_sum(raw Point* point);
}

i32 main() {
    return 0;
}
