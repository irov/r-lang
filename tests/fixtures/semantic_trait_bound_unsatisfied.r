module test.semantic.trait_bound_unsatisfied;

/* R-TYPE-0043: a type argument proves every trait constraint of its parameter. */
struct Point {
    i32 x;
};

struct Plain {
    i32 y;
};

trait Valued {
    i32 amount(const Self* this);
};

impl Valued for Point {
    i32 amount(const Point* this) {
        return this->x;
    }
};

@generic<T: Valued>
i32 amount_of(const T* value) {
    i32 result = value->amount();
    return result;
}

i32 main() {
    Plain plain = Plain { .y = 1 };
    i32 result = amount_of(&plain);
    return result;
}
