module ffi.constant_missing_attribute;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    const c_int PROBE_OK = 0i32 as c_int;
}

i32 main() {
    return 0;
}
