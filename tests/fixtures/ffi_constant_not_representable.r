module ffi.constant_not_representable;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_constant(name = "PROBE_LIMIT")
    const c_uint PROBE_LIMIT = -1i32 as c_uint;
}

i32 main() {
    return 0;
}
