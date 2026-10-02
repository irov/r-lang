module ffi.record_hidden_member;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    @c_type(name = "probe_padded", kind = "struct")
    struct Padded {
        c_int left;
        c_double scale;
    };

    @safety("PROBE-PADDED-SCALE", "The function has no preconditions")
    c_double probe_padded_scale(Padded padded);
}

i32 main() {
    return 0;
}
