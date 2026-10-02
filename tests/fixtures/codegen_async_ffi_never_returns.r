module test.codegen.async_ffi_never_returns;

/* R-TYPE-0007, R-FFI-0021: an async never function whose C never import returns violates its
   contract; the panic is re-raised at the await of the call. */

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    @safety("PROBE-RETURNS", "The caller asserts that the function does not return")
    never probe_returns();
}

protected async never halt() {
    unsafe {
        probe_returns();
    }
}

async i32 main() {
    try {
        await halt();
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
}
