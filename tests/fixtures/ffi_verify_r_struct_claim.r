module ffi.verify_r_struct_claim;

/* R-FFI-0042: the verifier compiles the prototype the record claims against the header; a record
   that names the wrong C struct at the position of an R-declared struct fails there. */
@repr(C)
struct Point {
    c_int x;
    c_int y;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi-r-struct-claim")
extern "C" {
    @safety("PROBE-POINT-SUM", "The function has no preconditions")
    c_int probe_point_sum(Point point);
}

i32 main() {
    return 0;
}
