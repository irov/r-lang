module test.codegen.ffi_enum_result_ingress;

/* R-FFI-0056: an enum a C function returns shall name a declared variant; 7 does not. */

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
    Mode probe_current_mode;

    @safety("PROBE-MODE-RAW", "The function has no preconditions")
    Mode probe_mode_raw(c_int value);

    @safety("PROBE-MARKER-RAW", "The function has no preconditions")
    Marker probe_marker_raw(c_int mode, c_int labeled);

    @safety("PROBE-MODE-CORRUPT", "The function has no preconditions")
    void probe_mode_corrupt(c_int value);
}

i32 main() {
    unsafe {
        if (probe_mode_raw(2i32 as c_int) != Mode::PROBE_MODE_RUN) {
            return 1;
        }
        Mode invalid = probe_mode_raw(7i32 as c_int);
        return (invalid as c_int) as i32;
    }
}
