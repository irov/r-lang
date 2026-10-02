module ffi.record_wrong_provider;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi-wrong_provider")
extern "C" {
    @repr(C)
    @c_type(name = "probe_pair", kind = "struct")
    struct Pair {
        c_int left;
        c_int right;
    };
}

i32 main() {
    return 0;
}
