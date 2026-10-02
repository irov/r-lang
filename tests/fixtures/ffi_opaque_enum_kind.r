module ffi.opaque_enum_kind;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @c_type(name = "probe_status", kind = "enum")
    opaque struct Status;
}

i32 main() {
    return 0;
}
