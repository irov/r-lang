module test.codegen.ffi_conformance;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    @c_type(name = "probe_padded", kind = "struct")
    struct Padded {
        c_int left;
        c_char hidden;
        c_double scale;
    };

    @c_constant(name = "WEOF")
    const c_wint PROBE_WEOF = -1i32 as c_wint;

    @safety("PROBE-PADDED-SCALE", "The function has no preconditions")
    c_double probe_padded_scale(Padded padded);

    @safety("PROBE-ROUNDING", "The function has no preconditions")
    c_int probe_rounding_is_default();

    @safety("PROBE-FLAGS", "The function has no preconditions")
    c_int probe_flags_raised();

    @safety("PROBE-SET-UPWARD", "The function has no preconditions")
    void probe_set_rounding_upward();

    @safety("PROBE-RAISE", "The function has no preconditions")
    void probe_raise_division_by_zero();

    @safety("PROBE-CALL-UPWARD", "callback shall be a valid C function pointer for the whole call")
    c_int probe_call_with_upward(raw fn(c_int) -> c_int callback, c_int value);

    @safety("PROBE-APPLY", "callback shall be a valid C function pointer for the whole call")
    c_int probe_apply(raw fn(c_int) -> c_int callback, c_int value);

    @safety("PROBE-CALL-ON-THREAD", "callback shall be a valid C function pointer for the whole call")
    c_int probe_call_on_thread(raw fn(c_int) -> c_int callback, c_int value);

    @safety("PROBE-WINT-ECHO", "The function has no preconditions")
    c_wint probe_wint_echo(c_wint value);
}

@callback
@safety("PROBE-OBSERVE-ROUNDING", "The runtime is initialized and the caller may enter R")
extern "C" c_int probe_observe_rounding(c_int value) {
    unsafe {
        if (probe_rounding_is_default() != 1i32 as c_int) {
            return -5i32 as c_int;
        }
    }
    return value + 1i32 as c_int;
}

@callback
@safety("PROBE-LEAF", "The runtime is initialized and the caller may enter R")
extern "C" c_int probe_leaf(c_int value) {
    return value * 2i32 as c_int;
}

@callback
@safety("PROBE-OUTER", "The runtime is initialized and the caller may enter R")
extern "C" c_int probe_outer(c_int value) {
    unsafe {
        c_int inner = probe_apply(probe_leaf, value);
        return inner + 1i32 as c_int;
    }
}

i32 main() {
    i32 outcome = 0;
    Padded padded = Padded { .left = 3i32 as c_int, .hidden = 1i8 as c_char, .scale = 2.0f64 as c_double };
    unsafe {
        probe_set_rounding_upward();
        if (probe_rounding_is_default() != 1i32 as c_int) {
            outcome = 1;
        }
        probe_raise_division_by_zero();
        if (probe_flags_raised() != 0i32 as c_int) {
            outcome = 2;
        }
        if (probe_call_with_upward(probe_observe_rounding, 10i32 as c_int) != 11i32 as c_int) {
            outcome = 3;
        }
        if (probe_apply(probe_outer, 5i32 as c_int) != 11i32 as c_int) {
            outcome = 4;
        }
        if (probe_call_on_thread(probe_outer, 6i32 as c_int) != 13i32 as c_int) {
            outcome = 5;
        }
        if (probe_wint_echo(PROBE_WEOF) != PROBE_WEOF) {
            outcome = 6;
        }
        if (probe_padded_scale(padded) != 7.0f64 as c_double) {
            outcome = 7;
        }
    }
    return outcome;
}
