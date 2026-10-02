module test.codegen.view_container_failures;

/* R-BORROW-0025, R-LIB-0022, R-STMT-0010: an insertion error hands the staged element back. A
   staged borrow is read by the handler while its referent exists, and a Copy element is copied
   out of the payload. The wrapper fails the first list node allocation of each element size. */

struct Point { i32 x; i32 y; };

i32 check_borrow_failure() {
    Point point = {.x = 41, .y = 2};
    list<const Point*> points = std.list::create::<const Point*>();
    try {
        std.list::push_back(&points, &point) as void;
        return 1;
    } catch (std.list::push_error<const Point*> failure) {
        switch (move failure) {
        case variant std.list::push_error::allocation_failed(move payload):
            if ((payload.value->x != 41) || (len(points) != 0usize)) {
                return 2;
            }
            break;
        }
    }
    return 0;
}

i32 check_value_failure() {
    list<i32> values = std.list::create::<i32>();
    try {
        std.list::push_back(&values, 7) as void;
        return 3;
    } catch (std.list::push_error<i32> failure) {
        switch (move failure) {
        case variant std.list::push_error::allocation_failed(move payload):
            if ((payload.value != 7) || (len(values) != 0usize)) {
                return 4;
            }
            break;
        }
    }
    return 0;
}

i32 main() {
    i32 status = check_borrow_failure();
    if (status != 0) {
        return status;
    }
    return check_value_failure();
}
