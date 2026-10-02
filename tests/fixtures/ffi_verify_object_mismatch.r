module ffi.verify_object_mismatch;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    c_long probe_counter;
}

i32 main() {
    unsafe {
        c_long value = probe_counter;
        return value as i32;
    }
}
