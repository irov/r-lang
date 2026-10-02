module test.semantic.method_receiver_type;

/* R-FUNC-0013: the receiver shall be const Owner*, Owner* or Owner. */
struct Point {
    i32 x;
};

struct Other {
    i32 y;
};

i32 Point::wrong(const Other* this) {
    return this->y;
}

i32 main() {
    return 0;
}
