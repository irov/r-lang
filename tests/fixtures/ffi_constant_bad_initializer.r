module ffi.constant_bad_initializer;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_constant(name = "PROBE_OK")
    const c_int PROBE_OK = 0;
}

i32 main() {
    return 0;
}
