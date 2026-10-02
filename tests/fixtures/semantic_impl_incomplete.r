module test.semantic.impl_incomplete;

/* R-TYPE-0042: an implementation defines every method its trait declares. */
struct Point {
    i32 x;
};

trait Pair {
    i32 first(const Self* this);
    i32 second(const Self* this);
};

impl Pair for Point {
    i32 first(const Point* this) {
        return this->x;
    }
};

i32 main() {
    return 0;
}
