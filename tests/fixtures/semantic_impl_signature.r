module test.semantic.impl_signature;

/* R-TYPE-0042: the implemented signature is the one the trait fixes for the target. */
struct Point {
    i32 x;
};

trait Valued {
    i32 amount(const Self* this);
};

impl Valued for Point {
    u64 amount(const Point* this) {
        return 0u64;
    }
};

i32 main() {
    return 0;
}
