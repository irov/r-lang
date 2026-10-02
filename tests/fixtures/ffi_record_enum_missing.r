module ffi.record_enum_missing;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    @c_type(name = "probe_mode", kind = "enum")
    enum Mode : c_int {
        PROBE_MODE_IDLE = 0,
        PROBE_MODE_RUN = 2,
    };
}

i32 main() {
    return 0;
}
