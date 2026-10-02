module ffi.object_safe_access;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    const c_int probe_version;
}

i32 main() {
    c_int version = probe_version;
    return version as i32;
}
