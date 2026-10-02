module ffi.verify_record_claim;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi-claim")
extern "C" {
    @repr(C)
    @c_type(name = "probe_pair", kind = "struct")
    struct Pair {
        c_int left;
        c_long right;
    };
}

i32 main() {
    return 0;
}
