module ffi.record_r_enum_field;

/* R-FFI-0021: the mode member of probe_marker is enum probe_mode, compatible with int; an
   R-declared enum of another underlying type does not correspond to it. */
@repr(C)
enum Mode : c_uint {
    Idle = 0,
    Run = 2,
};

@repr(C)
struct Point {
    c_int x;
    c_int y;
};

@repr(C)
struct Marker {
    Point corner;
    c_uchar[4] tag;
    Mode mode;
    raw const c_char* label;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @safety("PROBE-MARKER-SHIFT", "The label shall stay valid")
    Marker probe_marker_shift(Marker marker, c_int dx);
}

i32 main() {
    return 0;
}
