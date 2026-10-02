module ffi.record_omitted_member;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    @c_type(name = "probe_pair", kind = "struct")
    struct Pair {
        c_int left;
    };

    @safety("PROBE-PAIR-SUM", "The function has no preconditions")
    c_int probe_pair_sum(Pair pair);
}

i32 main() {
    return 0;
}
