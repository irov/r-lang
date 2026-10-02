module example.methods.main;

import example.methods.shapes::{Point, Circle, Rectangle, Area, FenceOrder, total_area, perimeter, painting_quote, quote_range, discount};

i32 main() {
    Point center = Point::origin();
    center.shift(3.0, 4.0);
    f64 norm = center.norm_squared();
    if (norm != 25.0) { return 1; }
    auto measure = Point::norm_squared; // the receiver becomes the first parameter
    if (measure(&center) != norm) { return 10; }
    Point displacement = Point { .x = 1.0, .y = 2.0 };
    center.shift(displacement);

    Circle small = Circle { .center = center, .radius = 1.0 };
    Circle large = Circle { .center = center, .radius = 2.0 };
    f64 area = total_area(&small, &large);
    if (area < 15.7) { return 2; }
    if (area > 15.8) { return 3; }
    Rectangle plot = Rectangle { .width = 3.0, .height = 4.0 };
    f64 round_fence = perimeter(&large);
    if (round_fence < 12.56 || round_fence > 12.57) { return 4; }
    f64 rectangular_fence = perimeter(&plot);
    if (rectangular_fence != 14.0) { return 5; }
    f64 fence_cost = FenceOrder::around(rectangular_fence).with_height(1.5).with_gates(2u32).cost(10.0);
    if (fence_cost != 260.0) { return 11; }
    f64 vacant_fence = perimeter(null);
    if (vacant_fence != 0.0) { return 6; }
    f64 rectangular_quote = painting_quote(&plot, 3.5);
    if (rectangular_quote != 42.0) { return 7; }
    f64 round_quote = painting_quote(&large, 3.5);
    if (round_quote < 43.98 || round_quote > 43.99) { return 8; }
    auto quotes = quote_range(rectangular_quote, 1.0, 3usize);
    auto apply_discount = discount(0.1);
    f64 batch_total = 0.0;
    for (f64 quote in &quotes) { batch_total += apply_discount(quote); }
    apply_discount as void; // A range can be empty.
    if (batch_total < 116.09 || batch_total > 116.11) { return 9; }
    return 0;
}
