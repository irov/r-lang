module ffi.verify_constant_type_mismatch;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_constant(name = "PROBE_LIMIT")
    const c_ulong PROBE_LIMIT = 4000000000u64 as c_ulong;
}

i32 main() {
    return 0;
}
