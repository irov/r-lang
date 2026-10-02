module test.codegen.async_methods;

/* R-FUNC-0014: a method call inside an asynchronous frame is an ordinary direct call. */
struct Point {
    i32 x;
    i32 y;
};

trait Measured {
    i32 measure(const Self* this);
};

impl Measured for Point {
    i32 measure(const Point* this) {
        return this->x + this->y;
    }
};

protected i32 Point::doubled(const Point* this) {
    i32 base = this->measure();
    return base + base;
}

@generic<T: Measured>
i32 measured_of(const T* value) {
    i32 result = value->measure();
    return result;
}

async i32 compute(i32 seed) {
    Point point = Point { .x = seed, .y = 1 };
    i32 first = point.measure();
    i32 second = point.doubled();
    i32 third = measured_of(&point);
    return first + second + third;
}

async i32 main() {
    try {
        task<i32> work = compute(2);
        i32 total = await move work;
        if (total != 12) { return 1; }
    } catch (std.async::start_error failure) {
        return 90;
    }
    return 0;
}
