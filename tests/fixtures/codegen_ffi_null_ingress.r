module test.codegen.ffi_null_ingress;

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-CALL-NULL", "callback shall be a valid C function pointer for the whole call")
    c_int probe_call_null(raw fn(raw c_int*) -> c_int callback);
}

@callback
@safety("PROBE-TAKE", "The runtime is initialized; value is checked as non-null on entry")
extern "C" c_int probe_take(raw c_int* value) {
    unsafe {
        return *value;
    }
}

i32 main() {
    unsafe {
        c_int result = probe_call_null(probe_take);
        return result as i32;
    }
}
