module ffi.record_union_kind;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    @c_type(name = "probe_pair", kind = "union")
    struct Pair {
        c_int left;
        c_int right;
    };
}

i32 main() {
    return 0;
}
