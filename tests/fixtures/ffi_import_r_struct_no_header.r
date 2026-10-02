module ffi.import_r_struct_no_header;

/* R-FFI-0057: the bridge that converts an R-declared struct includes the block's headers. */
@repr(C)
struct Point {
    c_int x;
    c_int y;
};

@link(name = "probe", kind = "static")
@abi("probe-abi")
extern "C" {
    @safety("PROBE-POINT-SUM", "The function has no preconditions")
    c_int probe_point_sum(Point point);
}

i32 main() {
    return 0;
}
