module test.codegen.ffi_repr_c_imports;

/* R-FFI-0021, R-FFI-0041: @repr(C) structs and enums declared in R pass by value, through
   pointers and inside raw function types of extern "C" imports. Each struct is proven against
   the C struct at its position in the ABI record's prototype, member by member; an enum is its
   compatible integer type. Values entering R are validated (R-FFI-0056). */

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

@repr(C)
struct Node {
    c_int value;
    raw const Node*? next;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    @safety("PROBE-POINT-SUM", "The function has no preconditions")
    c_int probe_point_sum(Point point);

    @safety("PROBE-POINT-MAKE", "The function has no preconditions")
    Point probe_point_make(c_int x, c_int y);

    @safety("PROBE-POINT-SCALE", "point shall address a valid Point")
    void probe_point_scale(raw Point* point, c_int factor);

    @safety("PROBE-POINT-APPLY", "visit shall be a valid C function pointer for the whole call")
    c_int probe_point_apply(raw fn(Point) -> c_int visit, Point point);

    @safety("PROBE-MARKER-SHIFT", "The label shall stay valid")
    Marker probe_marker_shift(Marker marker, c_int dx);

    @safety("PROBE-NAME", "The function has no preconditions")
    raw const c_char* probe_name();

    @safety("PROBE-NODE-TOTAL", "head shall address a valid list or be null")
    c_int probe_node_total(raw const Node*? head);

    @safety("PROBE-MODE-AFTER", "The function has no preconditions")
    Mode probe_mode_after(Mode mode);

    @safety("PROBE-CALL-MODE", "callback shall be a valid C function pointer for the whole call")
    c_int probe_call_mode(raw fn(Mode) -> c_int callback, c_int raw_value);
}

@callback
@safety("PROBE-POINT-VISIT", "The runtime is initialized")
extern "C" c_int probe_point_visit(Point point) {
    return point.x * (10i32 as c_int) + point.y;
}

@callback
@safety("PROBE-MODE-TAKE", "The runtime is initialized; mode is validated on entry")
extern "C" c_int probe_mode_take(Mode mode) {
    if (mode == Mode::PROBE_MODE_HALT) {
        return 5i32 as c_int;
    }
    return 1i32 as c_int;
}

i32 main() {
    unsafe {
        Point point = Point { .x = 3i32 as c_int, .y = 4i32 as c_int };
        if (probe_point_sum(point) != 7i32 as c_int) {
            return 1;
        }
        Point made = probe_point_make(5i32 as c_int, 6i32 as c_int);
        if ((made.x != 5i32 as c_int) || (made.y != 6i32 as c_int)) {
            return 2;
        }
        probe_point_scale(&made as raw Point*, 2i32 as c_int);
        if ((made.x != 10i32 as c_int) || (made.y != 12i32 as c_int)) {
            return 3;
        }
        if (probe_point_apply(probe_point_visit, point) != 34i32 as c_int) {
            return 4;
        }
        Marker marker = Marker {
            .corner = point,
            .tag = {1u8 as c_uchar, 2u8 as c_uchar, 3u8 as c_uchar, 0u8 as c_uchar},
            .mode = Mode::PROBE_MODE_RUN,
            .label = probe_name(),
        };
        Marker shifted = probe_marker_shift(marker, 7i32 as c_int);
        if ((shifted.corner.x != 10i32 as c_int) || (shifted.tag[3] != 6u8 as c_uchar) ||
            (shifted.mode != Mode::PROBE_MODE_HALT)) {
            return 5;
        }
        Node last = Node { .value = 5i32 as c_int, .next = null };
        Node first = Node { .value = 2i32 as c_int, .next = &last as raw const Node* };
        if (probe_node_total(&first as raw const Node*) != 7i32 as c_int) {
            return 6;
        }
        if (probe_mode_after(Mode::PROBE_MODE_RUN) != Mode::PROBE_MODE_HALT) {
            return 7;
        }
        if (probe_call_mode(probe_mode_take, -1i32 as c_int) != 5i32 as c_int) {
            return 8;
        }
    }
    return 0;
}
