module ffi.import_missing_header;

@link(name = "probe", kind = "static")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    c_int probe_increment(c_int value);
}

i32 main() {
    return 0;
}
