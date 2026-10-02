module test.semantic.impl_duplicate;

/* R-TYPE-0042: one implementation per trait and target type. */
struct Point {
    i32 x;
};

trait Valued {
    i32 amount(const Self* this);
};

impl Valued for Point {
    i32 amount(const Point* this) {
        return this->x;
    }
};

impl Valued for Point {
    i32 amount(const Point* this) {
        return 0;
    }
};

i32 main() {
    return 0;
}
