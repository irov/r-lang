module test.codegen.ffi_never_returns;

/* R-TYPE-0007, R-FFI-0021: a C function declared with a never result that returns violates its
   contract; the call site panics instead of continuing past the call. */

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-RETURNS", "The caller asserts that the function does not return")
    never probe_returns();
}

i32 main() {
    unsafe {
        probe_returns();
    }
}
