module ffi.verify_constant_mismatch;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_constant(name = "PROBE_OK")
    const c_int PROBE_OK = 1i32 as c_int;
}

i32 main() {
    return PROBE_OK as i32;
}
