module ffi.record_r_struct_field_name;

/* R-FFI-0041: an R-declared struct names the members of the C struct at its position in order;
   struct probe_point has x and y. */
@repr(C)
struct Point {
    c_int x;
    c_int z;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @safety("PROBE-POINT-SUM", "The function has no preconditions")
    c_int probe_point_sum(Point point);
}

i32 main() {
    return 0;
}
