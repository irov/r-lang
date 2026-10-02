module ffi.object_r_struct_missing_abi;

/* R-FFI-0041: the C type of an imported object of an R-declared struct type comes from an
   ABI record. */
@repr(C)
struct Point {
    c_int x;
    c_int y;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    Point probe_corner;
}

i32 main() {
    return 0;
}
