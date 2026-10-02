module ffi.import_aggregate;

@repr(C)
struct Pair {
    c_int left;
    c_int right;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-SUM", "The function has no preconditions")
    c_int probe_sum(Pair pair);
}

i32 main() {
    return 0;
}
