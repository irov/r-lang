module ffi.record_missing_abi;

@link(name = "probe", kind = "static")
@header("probe_library.h")
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
