module ffi.import_implicit_ok;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { c_int value; };

@header("probe_library.h")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    c_int probe_increment(c_int value);
}

i32 main() {
    TestStorage1 storage_value = {.value = 0i32 as c_int};
    unsafe {
        storage_value.value = probe_increment(1i32 as c_int);
    }
    return storage_value.value as i32 - 2;
}
