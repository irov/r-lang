module ffi.record_enum_representation;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    @c_type(name = "probe_status", kind = "enum")
    enum Status : c_int {
        PROBE_STATUS_READY = 3,
    };
}

i32 main() {
    return 0;
}
