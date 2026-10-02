module ffi.verify_never_mismatch;

/* R-FFI-0021: a never result is void in C; the verifier rejects it for a C function that returns
   a value. */

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-INCREMENT", "The function has no preconditions")
    never probe_increment(c_int value);
}

i32 main() {
    unsafe {
        probe_increment(1i32 as c_int);
    }
}
