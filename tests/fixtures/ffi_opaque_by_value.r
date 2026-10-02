module ffi.opaque_by_value;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_type(name = "probe_handle", kind = "struct")
    opaque struct Handle;

    @safety("PROBE-OPEN", "The result is a live handle owned by the caller")
    raw Handle* probe_open(c_int seed);
}

i32 consume(Handle handle) {
    return 0;
}

i32 main() {
    return 0;
}
