module ffi.record_wrong_field_type;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    @c_type(name = "probe_pair", kind = "struct")
    struct Pair {
        c_int left;
        c_uint right;
    };
}

i32 main() {
    return 0;
}
