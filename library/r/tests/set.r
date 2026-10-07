module tests.std.set;
import std.test;
import std.set;

// The tests of std.set (Library R-SLIB-SET-0001), run in test mode (Core R-FUNC-0025).

/* A program key type (Core R-AGG-0012). */
@derive(equal, key)
struct point { i32 x; i32 y; };

/* Inserts the number, turning a failed growth into its allocation error. */
protected bool add(std.set::set<i32>* target, i32 value) throws std.alloc::alloc_error {
    try {
        bool added = target->insert(value);
        return added;
    } catch (std.dict::insert_error<i32, bool> failure) {
        switch (move failure) {
        case variant std.dict::insert_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* The elements in iteration order, each followed by a space. */
protected std.string::string listing(const std.set::set<i32>* source)
    throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    for (const i32* value in source->iter()) {
        i32 number = *value;
        std.string::string piece = f"{number} ";
        text.append(piece);
    }
    return move text;
}

@test
void starts_empty() throws std.test::failure, std.alloc::alloc_error {
    std.set::set<i32> numbers = std.set::set<i32>::create();
    std.test::equal(numbers.count(), 0usize);
    std.test::check(numbers.is_empty(), "a new set is empty");
    i32 probe = 1;
    std.test::check(numbers.contains(&probe) == false, "an empty set contains nothing");
    std.test::check(numbers.remove(&probe) == false, "nothing to remove");
    usize seen = 0usize;
    for (const i32* value in numbers.iter()) { seen += 1usize; }
    std.test::equal(seen, 0usize);
}

@test
void inserts_each_value_once() throws std.test::failure, std.alloc::alloc_error {
    std.set::set<i32> numbers = std.set::set<i32>::create();
    std.test::check(add(&numbers, 3), "3 is new");
    std.test::check(add(&numbers, 3) == false, "3 is already present");
    std.test::check(add(&numbers, -3), "-3 is new");
    std.test::check(add(&numbers, 0), "0 is new");
    std.test::equal(numbers.count(), 3usize);
    std.test::check(numbers.is_empty() == false, "the set holds three numbers");
    i32 three = 3;
    i32 minus_three = -3;
    i32 four = 4;
    std.test::check(numbers.contains(&three), "contains 3");
    std.test::check(numbers.contains(&minus_three), "contains -3");
    std.test::check(numbers.contains(&four) == false, "does not contain 4");
}

@test
void removes_values() throws std.test::failure, std.alloc::alloc_error {
    std.set::set<i32> numbers = std.set::set<i32>::create();
    add(&numbers, 1) as void;
    add(&numbers, 2) as void;
    add(&numbers, 3) as void;
    i32 two = 2;
    i32 nine = 9;
    std.test::check(numbers.remove(&two), "2 was present");
    std.test::check(numbers.remove(&two) == false, "2 is gone");
    std.test::check(numbers.remove(&nine) == false, "9 was never present");
    std.test::check(numbers.contains(&two) == false, "2 is no longer contained");
    std.test::equal(numbers.count(), 2usize);
    std.test::check(add(&numbers, 2), "2 can be inserted again");
    std.test::equal(numbers.count(), 3usize);
}

@test
void iterates_in_insertion_order() throws std.test::failure, std.alloc::alloc_error {
    std.set::set<i32> numbers = std.set::set<i32>::create();
    add(&numbers, 30) as void;
    add(&numbers, 10) as void;
    add(&numbers, 20) as void;
    add(&numbers, 10) as void;
    std.string::string first = listing(&numbers);
    std.test::equal_text(first, "30 10 20 ");
    i32 ten = 10;
    numbers.remove(&ten);
    std.string::string second = listing(&numbers);
    std.test::equal_text(second, "30 20 ");
    add(&numbers, 10) as void;
    add(&numbers, 30) as void;
    std.string::string third = listing(&numbers);
    std.test::equal_text(third, "30 20 10 ");
}

@test
void clears_and_is_reused() throws std.test::failure, std.alloc::alloc_error {
    std.set::set<i32> numbers = std.set::set<i32>::create();
    add(&numbers, 5) as void;
    add(&numbers, 6) as void;
    numbers.clear();
    std.test::equal(numbers.count(), 0usize);
    std.test::check(numbers.is_empty(), "a cleared set is empty");
    i32 five = 5;
    std.test::check(numbers.contains(&five) == false, "5 was cleared");
    std.test::check(add(&numbers, 5), "5 is new after clearing");
    std.string::string text = listing(&numbers);
    std.test::equal_text(text, "5 ");
    numbers.clear();
    numbers.clear();
    std.test::check(numbers.is_empty(), "clearing twice is harmless");
}

@test
void holds_program_keys() throws std.test::failure, std.alloc::alloc_error {
    std.set::set<point> points = std.set::set<point>::create();
    try {
        std.test::check(points.insert(point {.x = 1, .y = 2}), "(1, 2) is new");
        std.test::check(points.insert(point {.x = 2, .y = 1}), "(2, 1) is new");
        std.test::check(points.insert(point {.x = 1, .y = 2}) == false, "(1, 2) again");
    } catch (std.dict::insert_error<point, bool> failure) {
        switch (move failure) {
        case variant std.dict::insert_error::allocation_failed(move payload): throw payload.reason;
        }
    }
    point probe = {.x = 2, .y = 1};
    point missing = {.x = 2, .y = 2};
    std.test::check(points.contains(&probe), "contains (2, 1)");
    std.test::check(points.contains(&missing) == false, "does not contain (2, 2)");
    i32 sum = 0;
    for (const point* value in points.iter()) { sum += value->x * 10 + value->y; }
    std.test::equal(sum, 12 + 21);
}

@test(allocations)
void grows_and_shrinks() throws std.test::failure, std.alloc::alloc_error {
    std.set::set<i32> numbers = std.set::set<i32>::create();
    for (i32 value in 0..40) {
        std.test::check(add(&numbers, value), "every number is new once");
    }
    for (i32 value in 0..40) {
        std.test::check(add(&numbers, value) == false, "every number is present");
    }
    std.test::equal(numbers.count(), 40usize);
    for (i32 value in 0..40) {
        if (value % 4 != 0) {
            std.test::check(numbers.remove(&value), "a present number is removed");
        }
    }
    std.test::equal(numbers.count(), 10usize);
    i32 twelve = 12;
    i32 thirteen = 13;
    std.test::check(numbers.contains(&twelve), "12 was kept");
    std.test::check(numbers.contains(&thirteen) == false, "13 was removed");
    std.string::string text = listing(&numbers);
    std.test::equal_text(text, "0 4 8 12 16 20 24 28 32 36 ");
}
