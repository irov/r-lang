module ffi.record_r_struct_hidden_member;

/* R-FFI-0041: layout alone is never proof; struct probe_padded hides a char member in the
   padding before scale, so the member inventory disagrees. */
@repr(C)
struct Padded {
    c_int left;
    c_double scale;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @safety("PROBE-PADDED-SCALE", "The function has no preconditions")
    c_double probe_padded_scale(Padded padded);
}

i32 main() {
    return 0;
}
