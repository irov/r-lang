module test.codegen.traits;

/* R-TYPE-0041 through R-TYPE-0043: traits, implementations and statically dispatched bounds. */
struct Point {
    i32 x;
    i32 y;
};

struct Tag {
    u64 code;
};

trait Measured {
    u64 measure(const Self* this);
    u64 doubled(const Self* this);
};

impl Measured for Point {
    u64 measure(const Point* this) {
        i32 total = this->x + this->y;
        return total as u64;
    }
    u64 doubled(const Point* this) {
        u64 base = this->measure();
        return base + base;
    }
};

impl Measured for Tag {
    u64 measure(const Tag* this) {
        return this->code;
    }
    u64 doubled(const Tag* this) {
        return this->code + this->code;
    }
};

impl Measured for i32 {
    u64 measure(const i32* this) {
        return *this as u64;
    }
    u64 doubled(const i32* this) {
        i32 value = *this;
        return (value + value) as u64;
    }
};

@generic<T: Measured>
u64 total_of(const T* left, const T* right) {
    u64 first = left->measure();
    u64 second = right->doubled();
    return first + second;
}

i32 main() {
    Point left = Point { .x = 2, .y = 3 };
    Point right = Point { .x = 4, .y = 1 };
    u64 points = total_of(&left, &right);
    if (points != 15) { return 1; }

    Tag first = Tag { .code = 7 };
    Tag second = Tag { .code = 5 };
    u64 tags = total_of(&first, &second);
    if (tags != 17) { return 2; }

    i32 one = 3;
    i32 other = 4;
    u64 scalars = total_of(&one, &other);
    if (scalars != 11) { return 3; }

    u64 direct = left.measure();
    if (direct != 5) { return 4; }
    u64 twice = left.doubled();
    if (twice != 10) { return 5; }
    return 0;
}
