module ffi.verify_opaque_mismatch;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_type(name = "probe_handle", kind = "union")
    opaque struct Handle;

    @safety("PROBE-OPEN", "The result is a live handle owned by the caller")
    raw Handle* probe_open(c_int seed);
}

i32 main() {
    unsafe {
        raw Handle* handle = probe_open(1i32 as c_int);
        handle as void;
    }
    return 0;
}
