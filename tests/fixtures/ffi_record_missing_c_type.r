module ffi.record_missing_c_type;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    struct Pair {
        c_int left;
        c_int right;
    };
}

i32 main() {
    return 0;
}
