module ffi.import_missing_safety;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    c_int probe_increment(c_int value);
}

i32 main() {
    return 0;
}
