module test.codegen.repr_c;

@repr(C)
enum Status : c_int {
    Ok = 0,
    Failed = 1,
};

@repr(C)
struct Point {
    c_int x;
    c_int y;
};

@repr(C)
struct Packet {
    Point point;
    c_uint8[4] tag;
    raw Point*? next;
    Status status;
};

@safety("TEST-SWAP", "the value is initialized")
extern "C" Point swap_coordinates(Point value) {
    return Point { .x = value.y, .y = value.x };
}

i32 main() {
    Point value = Point { .x = 1i32 as c_int, .y = 2i32 as c_int };
    Point result = swap_coordinates(value);
    Status status = Status::Ok;
    if (result.x != (2i32 as c_int)) {
        return 1;
    }
    if (result.y != (1i32 as c_int)) {
        return 2;
    }
    if (status != Status::Ok) {
        return 3;
    }
    return 0;
}
