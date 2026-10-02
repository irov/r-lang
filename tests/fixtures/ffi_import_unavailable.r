module ffi.import_unavailable;

@link(name = "probe-optional", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-OPTIONAL", "The function has no preconditions")
    c_int probe_optional_call();
}

i32 main() {
    return 0;
}
