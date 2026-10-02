module test.codegen.ffi_enum_ingress;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    @c_type(name = "probe_mode", kind = "enum")
    enum Mode : c_int {
        PROBE_MODE_IDLE = 0,
        PROBE_MODE_RUN = 2,
        PROBE_MODE_HALT = -1,
    };

    @safety("PROBE-CALL-MODE", "callback shall be a valid C function pointer for the whole call")
    c_int probe_call_mode(raw fn(Mode) -> c_int callback, c_int raw_value);
}

@callback
@safety("PROBE-MODE-TAKE", "The runtime is initialized; mode is validated on entry")
extern "C" c_int probe_mode_take(Mode mode) {
    if (mode == Mode::PROBE_MODE_RUN) {
        return 1i32 as c_int;
    }
    return 0i32 as c_int;
}

i32 main() {
    unsafe {
        if (probe_call_mode(probe_mode_take, 2i32 as c_int) != 1i32 as c_int) {
            return 1;
        }
        c_int invalid = probe_call_mode(probe_mode_take, 7i32 as c_int);
        return invalid as i32;
    }
}
