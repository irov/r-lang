module tests.std.heap;
import std.test;
import std.cmp;
import std.heap;

// The tests of std.heap (Library R-SLIB-HEAP-0001), run in test mode (Core R-FUNC-0025).

/* A job ordered by its level first and then by its number (Core R-AGG-0012). */
@derive(equal, ordered)
struct job { u8 level; u32 number; };

/* A number that orders in reverse, so that the max-heap yields the least number first. */
struct least { i32 value; };

impl std.cmp::Ordered for least {
    std.cmp::ordering cmp(const least* this, const least* other) {
        return other->value.cmp(&this->value);
    }
};

/* Pushes the number, turning a failed growth into its allocation error. */
protected void push(std.heap::heap<i32>* target, i32 value) throws std.alloc::alloc_error {
    try {
        target->push(value);
    } catch (std.array::push_error<i32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* Pops every element and lists them in the order of the pops. */
protected std.string::string drain(std.heap::heap<i32>* target) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    while (true) {
        o<i32> next = target->pop();
        switch (next) {
        case variant o::some(value):
            i32 number = *value;
            std.string::string piece = f"{number} ";
            text.append(piece);
        case variant o::none:
            return move text;
        }
    }
    return move text;
}

/* The top of the heap, or the fallback for none. */
protected i32 top(const std.heap::heap<i32>* source, i32 fallback) {
    o<const i32*> greatest = source->peek();
    switch (greatest) {
    case variant o::some(value): return **value;
    case variant o::none: return fallback;
    }
}

@test
void starts_empty() throws std.test::failure, std.alloc::alloc_error {
    std.heap::heap<i32> numbers = std.heap::heap<i32>::create();
    std.test::equal(numbers.count(), 0usize);
    std.test::check(numbers.is_empty(), "a new heap is empty");
    std.test::equal(top(&numbers, -1), -1);
    o<i32> nothing = numbers.pop();
    switch (nothing) {
    case variant o::some(value): std.test::fail("an empty heap has no greatest element");
    case variant o::none: break;
    }
    std.test::check(numbers.is_empty(), "popping an empty heap changes nothing");
}

@test
void pops_in_descending_order() throws std.test::failure, std.alloc::alloc_error {
    std.heap::heap<i32> numbers = std.heap::heap<i32>::create();
    push(&numbers, 5);
    push(&numbers, 1);
    push(&numbers, 9);
    push(&numbers, -3);
    push(&numbers, 7);
    push(&numbers, 5);
    std.test::equal(numbers.count(), 6usize);
    std.string::string order = drain(&numbers);
    std.test::equal_text(order, "9 7 5 5 1 -3 ");
    std.test::check(numbers.is_empty(), "every element was popped");
}

@test
void peeks_without_removing() throws std.test::failure, std.alloc::alloc_error {
    std.heap::heap<i32> numbers = std.heap::heap<i32>::create();
    push(&numbers, 2);
    std.test::equal(top(&numbers, 0), 2);
    push(&numbers, 8);
    std.test::equal(top(&numbers, 0), 8);
    push(&numbers, 4);
    std.test::equal(top(&numbers, 0), 8);
    std.test::equal(top(&numbers, 0), 8);
    std.test::equal(numbers.count(), 3usize);
    numbers.pop();
    std.test::equal(top(&numbers, 0), 4);
    std.test::equal(numbers.count(), 2usize);
}

@test
void interleaves_pushes_and_pops() throws std.test::failure, std.alloc::alloc_error {
    std.heap::heap<i32> numbers = std.heap::heap<i32>::create();
    push(&numbers, 4);
    push(&numbers, 2);
    o<i32> first = numbers.pop();
    switch (first) {
    case variant o::some(value): std.test::equal(*value, 4);
    case variant o::none: std.test::fail("4 is the greatest");
    }
    push(&numbers, 8);
    push(&numbers, 1);
    push(&numbers, 2);
    o<i32> second = numbers.pop();
    switch (second) {
    case variant o::some(value): std.test::equal(*value, 8);
    case variant o::none: std.test::fail("8 is the greatest");
    }
    push(&numbers, 3);
    std.string::string rest = drain(&numbers);
    std.test::equal_text(rest, "3 2 2 1 ");
}

@test
void orders_program_types() throws std.test::failure, std.alloc::alloc_error,
    std.array::push_error<job>, std.array::push_error<least> {
    std.heap::heap<job> jobs = std.heap::heap<job>::create();
    jobs.push(job {.level = 1u8, .number = 1u32});
    jobs.push(job {.level = 3u8, .number = 2u32});
    jobs.push(job {.level = 3u8, .number = 7u32});
    jobs.push(job {.level = 2u8, .number = 4u32});
    o<job> urgent = jobs.pop();
    switch (urgent) {
    case variant o::some(value):
        std.test::equal(value->level, 3u8);
        std.test::equal(value->number, 7u32);
    case variant o::none: std.test::fail("four jobs were pushed");
    }
    o<job> next = jobs.pop();
    switch (next) {
    case variant o::some(value): std.test::equal(value->number, 2u32);
    case variant o::none: std.test::fail("three jobs are left");
    }
    std.heap::heap<least> numbers = std.heap::heap<least>::create();
    numbers.push(least {.value = 6});
    numbers.push(least {.value = -2});
    numbers.push(least {.value = 11});
    i32 order = 0;
    while (numbers.is_empty() == false) {
        o<least> smallest = numbers.pop();
        switch (smallest) {
        case variant o::some(value): order = order * 100 + value->value + 50;
        case variant o::none: std.test::fail("a heap that is not empty has a top");
        }
    }
    std.test::equal(order, 48 * 10000 + 56 * 100 + 61);
}

@test
void orders_floats_and_characters() throws std.test::failure, std.alloc::alloc_error,
    std.array::push_error<f64>, std.array::push_error<char> {
    std.heap::heap<f64> measures = std.heap::heap<f64>::create();
    measures.push(1.5);
    measures.push(0.0 / 0.0);
    measures.push(-4.0);
    measures.push(9.25);
    o<f64> first = measures.pop();
    switch (first) {
    case variant o::some(value):
        f64 found = *value;
        std.test::check(found != found, "NaN orders after every number");
    case variant o::none: std.test::fail("four measures were pushed");
    }
    o<f64> second = measures.pop();
    switch (second) {
    case variant o::some(value): std.test::equal(*value, 9.25);
    case variant o::none: std.test::fail("three measures are left");
    }
    std.heap::heap<char> letters = std.heap::heap<char>::create();
    letters.push('b');
    letters.push('é');
    letters.push('Z');
    o<const char*> highest = letters.peek();
    switch (highest) {
    case variant o::some(value): std.test::equal(**value, 'é');
    case variant o::none: std.test::fail("three letters were pushed");
    }
}

@test(allocations)
void sorts_many_values() throws std.test::failure, std.alloc::alloc_error {
    std.heap::heap<i32> numbers = std.heap::heap<i32>::create();
    for (i32 step in 0..50) { push(&numbers, (step * 37) % 50); }
    for (i32 step in 0..10) { push(&numbers, 25); }
    std.test::equal(numbers.count(), 60usize);
    std.test::equal(top(&numbers, -1), 49);
    i32 previous = 49;
    i32 total = 0;
    usize pops = 0usize;
    usize twenty_fives = 0usize;
    while (numbers.is_empty() == false) {
        o<i32> next = numbers.pop();
        switch (next) {
        case variant o::some(value):
            std.test::check(*value <= previous, "the pops never increase");
            previous = *value;
            total += *value;
            pops += 1usize;
            if (*value == 25) { twenty_fives += 1usize; }
        case variant o::none: std.test::fail("a heap that is not empty has a top");
        }
    }
    std.test::equal(pops, 60usize);
    std.test::equal(total, 49 * 50 / 2 + 10 * 25);
    std.test::equal(twenty_fives, 11usize);
    std.test::equal(previous, 0);
}
