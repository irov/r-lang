module test.codegen.ffi_callback_struct_ingress;

/* R-FFI-0056, R-CMAP-0020: a C-origin entry validates every member of a struct argument before
   the R body runs; probe_marker_visit passes a mode of 7, which names no variant. */

@repr(C)
enum Mode : c_int {
    PROBE_MODE_IDLE = 0,
    PROBE_MODE_RUN = 2,
    PROBE_MODE_HALT = -1,
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
    @safety("PROBE-MARKER-VISIT", "visit shall be a valid C function pointer for the whole call")
    c_int probe_marker_visit(raw fn(Marker) -> c_int visit, c_int mode);
}

@callback
@safety("PROBE-MARKER-TAKE", "The runtime is initialized; the marker is validated on entry")
extern "C" c_int probe_marker_take(Marker marker) {
    if (marker.mode == Mode::PROBE_MODE_HALT) {
        return 3i32 as c_int;
    }
    return 1i32 as c_int;
}

i32 main() {
    unsafe {
        if (probe_marker_visit(probe_marker_take, -1i32 as c_int) != 3i32 as c_int) {
            return 1;
        }
        c_int invalid = probe_marker_visit(probe_marker_take, 7i32 as c_int);
        return invalid as i32;
    }
}
