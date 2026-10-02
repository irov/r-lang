module test.codegen.async_ffi_repr_c;

/* R-FFI-0021, R-FFI-0022: R-declared @repr(C) structs through imports and imported objects in
   an async frame, with the validation of every value that enters from C (R-FFI-0056). */

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

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    Point probe_corner;
    c_int total_count;
    Mode probe_current_mode;

    @safety("PROBE-POINT-SUM", "The function has no preconditions")
    c_int probe_point_sum(Point point);

    @safety("PROBE-POINT-MAKE", "The function has no preconditions")
    Point probe_point_make(c_int x, c_int y);

    @safety("PROBE-POINT-SCALE", "point shall address a valid Point")
    void probe_point_scale(raw Point* point, c_int factor);

    @safety("PROBE-MODE-AFTER", "The function has no preconditions")
    Mode probe_mode_after(Mode mode);
}

protected async i32 probe_all() {
    unsafe {
        Point made = probe_point_make(2i32 as c_int, 3i32 as c_int);
        probe_point_scale(&made as raw Point*, 3i32 as c_int);
        if (probe_point_sum(made) != 15i32 as c_int) {
            return 1;
        }
        if (probe_corner.y != 9i32 as c_int) {
            return 2;
        }
        Point corner = probe_corner;
        total_count = probe_point_sum(corner);
        if (total_count != 16i32 as c_int) {
            return 3;
        }
        if (probe_mode_after(probe_current_mode) != Mode::PROBE_MODE_RUN) {
            return 4;
        }
    }
    return 0;
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
