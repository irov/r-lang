module ffi.object_bad_type;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    i32 probe_counter;
}

i32 main() {
    return 0;
}
