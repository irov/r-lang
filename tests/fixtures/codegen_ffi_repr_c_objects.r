module test.codegen.ffi_repr_c_objects;

/* R-FFI-0022, R-FFI-0057, R-CMAP-0026: an imported object of an R-declared struct type, one that
   points to such a struct and one whose C name is reserved are reached through the address the
   accessor bridge returns; no storage is mirrored. A read of an imported object is validated
   like a C result (R-FFI-0056). */

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
struct Node {
    c_int value;
    raw const Node*? next;
};

@link(name = "probe", kind = "static")
@header("probe_library.h")
@abi("probe-abi")
extern "C" {
    Point probe_corner;
    raw Node* probe_chain;
    c_int total_count;
    Mode probe_current_mode;

    @safety("PROBE-NODE-TOTAL", "head shall address a valid list or be null")
    c_int probe_node_total(raw const Node*? head);
}

i32 main() {
    unsafe {
        if ((probe_corner.x != 7i32 as c_int) || (probe_corner.y != 9i32 as c_int)) {
            return 1;
        }
        probe_corner.x = 70i32 as c_int;
        Point copy = probe_corner;
        if (copy.x != 70i32 as c_int) {
            return 2;
        }
        raw Node* head = probe_chain;
        if (probe_node_total(head as raw const Node*) != 42i32 as c_int) {
            return 3;
        }
        total_count = 5i32 as c_int;
        total_count += 1i32 as c_int;
        if (total_count != 6i32 as c_int) {
            return 4;
        }
        if (probe_current_mode != Mode::PROBE_MODE_IDLE) {
            return 5;
        }
        probe_current_mode = Mode::PROBE_MODE_RUN;
        if (probe_current_mode != Mode::PROBE_MODE_RUN) {
            return 6;
        }
    }
    return 0;
}
