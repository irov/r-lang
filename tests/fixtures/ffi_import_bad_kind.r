module ffi.import_bad_kind;

@link(name = "probe", kind = "shared")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    c_int probe_increment(c_int value);
}

i32 main() {
    return 0;
}
