module test.codegen.async_ffi_object_enum_ingress;

/* R-FFI-0056: an async frame validates a read of an imported enum object and an enum a C
   function returns; C stored 9 into the object. */

@repr(C)
enum Mode : c_int {
    PROBE_MODE_IDLE = 0,
    PROBE_MODE_RUN = 2,
    PROBE_MODE_HALT = -1,
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
extern "C" {
    Mode probe_current_mode;

    @safety("PROBE-MODE-RAW", "The function has no preconditions")
    Mode probe_mode_raw(c_int value);

    @safety("PROBE-MODE-CORRUPT", "The function has no preconditions")
    void probe_mode_corrupt(c_int value);
}

protected async i32 probe_all() {
    unsafe {
        if (probe_mode_raw(-1i32 as c_int) != Mode::PROBE_MODE_HALT) {
            return 1;
        }
        probe_mode_corrupt(9i32 as c_int);
        Mode corrupted = probe_current_mode;
        return (corrupted as c_int) as i32;
    }
}

async i32 main() {
    try {
        i32 outcome = await probe_all();
        return outcome;
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
}
