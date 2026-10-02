module ffi.object_kind_mismatch;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    thread_local c_int probe_counter;
}

i32 main() {
    unsafe {
        c_int value = probe_counter;
        return value as i32;
    }
}
