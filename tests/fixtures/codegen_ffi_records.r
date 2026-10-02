module test.codegen.ffi_records;

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @repr(C)
    @c_type(name = "probe_pair", kind = "struct")
    struct Pair {
        c_int left;
        c_int right;
    };

    @repr(C)
    @c_type(name = "probe_rect", kind = "typedef")
    struct Rect {
        Pair origin;
        c_uchar flag;
        c_double scale;
    };

    @repr(C)
    @c_type(name = "probe_status", kind = "enum")
    enum Status : c_uint {
        PROBE_STATUS_READY = 3,
    };

    @repr(C)
    @c_type(name = "probe_mode", kind = "enum")
    enum Mode : c_int {
        PROBE_MODE_IDLE = 0,
        PROBE_MODE_RUN = 2,
        PROBE_MODE_HALT = -1,
    };

    Pair probe_origin;

    @safety("PROBE-PAIR-SUM", "The function has no preconditions")
    c_int probe_pair_sum(Pair pair);

    @safety("PROBE-PAIR-MAKE", "The function has no preconditions")
    Pair probe_pair_make(c_int left, c_int right);

    @safety("PROBE-RECT-AREA", "The function has no preconditions")
    c_double probe_rect_area(Rect rect);

    @safety("PROBE-RECT-SCALE", "The function has no preconditions")
    Rect probe_rect_scale(Rect rect, c_double factor);

    @safety("PROBE-STATUS-NEXT", "The function has no preconditions")
    Status probe_status_next(Status status);

    @safety("PROBE-MODE-AFTER", "The function has no preconditions")
    Mode probe_mode_after(Mode mode);
}

i32 main() {
    i32 outcome = 0;
    Pair pair = Pair { .left = 20i32 as c_int, .right = 22i32 as c_int };
    Rect rect = Rect { .origin = pair, .flag = 0u8 as c_uchar, .scale = 0.5f64 as c_double };
    unsafe {
        Pair made = probe_pair_make(3i32 as c_int, 4i32 as c_int);
        Rect scaled = probe_rect_scale(rect, 4.0f64 as c_double);
        Status status = probe_status_next(Status::PROBE_STATUS_READY);
        Mode mode = probe_mode_after(Mode::PROBE_MODE_RUN);
        if (probe_pair_sum(pair) != 42i32 as c_int) {
            outcome = 1;
        }
        if (made.left != 3i32 as c_int) {
            outcome = 2;
        }
        if (probe_rect_area(scaled) != 880.0f64 as c_double) {
            outcome = 3;
        }
        if (scaled.flag != 1u8 as c_uchar) {
            outcome = 4;
        }
        if (status != Status::PROBE_STATUS_READY) {
            outcome = 5;
        }
        if (mode != Mode::PROBE_MODE_HALT) {
            outcome = 6;
        }
        if (probe_origin.left + probe_origin.right != 9i32 as c_int) {
            outcome = 7;
        }
    }
    return outcome;
}
