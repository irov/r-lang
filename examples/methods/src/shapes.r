module example.methods.shapes;

struct Point {
    f64 x;
    f64 y;
};

struct Circle {
    Point center;
    f64 radius;
};

struct Rectangle { f64 width; f64 height; };

f64 perimeter(const Circle* shape) {
    return 2.0 * 3.141592653589793 * shape->radius;
}

f64 perimeter(const Rectangle* shape) {
    return 2.0 * (shape->width + shape->height);
}

// A vacant plot needs no fence.
f64 perimeter(null_t vacant) { return 0.0; }

trait Area {
    @noalloc @nonblocking
    f64 area(const Self* this);
};

// Every paintable shape supplies an area; the pricing calculation is shared statically.
trait Paintable : Area {
    @noalloc @nonblocking
    f64 paint_cost(const Self* this, f64 price_per_square_metre) {
        return this->area() * price_per_square_metre;
    }
};

Point Point::origin() {
    return Point { .x = 0.0, .y = 0.0 };
}

void Point::shift(Point* this, f64 dx, f64 dy) {
    this->x += dx;
    this->y += dy;
}

void Point::shift(Point* this, Point displacement) {
    this->x += displacement.x;
    this->y += displacement.y;
}

f64 Point::norm_squared(const Point* this) {
    f64 first = this->x * this->x;
    f64 second = this->y * this->y;
    return first + second;
}

impl Area for Circle {
    f64 area(const Circle* this) {
        f64 squared = this->radius * this->radius;
        return squared * 3.141592653589793;
    }
};

impl Area for Rectangle {
    f64 area(const Rectangle* this) { return this->width * this->height; }
};

impl Paintable for Circle {};
impl Paintable for Rectangle {};

@generic<T: Paintable>
@noalloc @nonblocking
f64 painting_quote(const T* shape, f64 price_per_square_metre) {
    return shape->paint_cost(price_per_square_metre);
}

@generic<T: Area>
f64 total_area(const T* first, const T* second) {
    f64 left = first->area();
    f64 right = second->area();
    return left + right;
}

protected struct QuoteRange {
    f64 next_quote;
    f64 step;
    usize remaining;
};

impl core::Iterator for QuoteRange {
    type Item = f64;
    o<f64> next(QuoteRange* this) {
        if (this->remaining == 0usize) { return o::none; }
        f64 quote = this->next_quote;
        this->next_quote += this->step;
        this->remaining -= 1usize;
        return o::some(quote);
    }
};

opaque(core::Iterator & Item = f64) quote_range(f64 first, f64 step, usize count) {
    return QuoteRange {.next_quote = first, .step = step, .remaining = count};
}

opaque(fn(f64) -> f64 & copy) discount(f64 fraction) {
    fn f64 apply(f64 quote) move(fraction) { return quote * (1.0 - fraction); }
    return apply;
}

/* A builder: each @chain method takes the order and returns it, so a call may continue after it
   (R-FUNC-0024); the chain ends with a method that computes the result. */
struct FenceOrder {
    f64 length;
    f64 height;
    u32 gates;
};

FenceOrder FenceOrder::around(f64 length) {
    return FenceOrder {.length = length, .height = 1.0, .gates = 0u32};
}

@chain FenceOrder FenceOrder::with_height(FenceOrder this, f64 height) {
    this.height = height;
    return move this;
}

@chain FenceOrder FenceOrder::with_gates(FenceOrder this, u32 gates) {
    this.gates = gates;
    return move this;
}

f64 FenceOrder::cost(FenceOrder this, f64 price) {
    return this.length * this.height * price + (this.gates as f64) * 25.0;
}
