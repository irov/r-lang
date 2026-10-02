module ffi.opaque_deref;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_type(name = "probe_handle", kind = "struct")
    opaque struct Handle;

    @safety("PROBE-OPEN", "The result is a live handle owned by the caller")
    raw Handle* probe_open(c_int seed);
}

i32 main() {
    unsafe {
        raw Handle* handle = probe_open(1i32 as c_int);
        Handle copy = *handle;
    }
    return 0;
}
