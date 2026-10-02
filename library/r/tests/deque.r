module tests.std.deque;
import std.test;
import std.deque;

// The tests of std.deque (Library R-SLIB-DEQUE-0001), run in test mode (Core R-FUNC-0025).

/* The growth operations with a failed growth turned into its allocation error. */
protected void push_back(std.deque::deque<i32>* target, i32 value) throws std.alloc::alloc_error {
    try {
        target->push_back(value);
    } catch (std.array::push_error<i32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected void push_front(std.deque::deque<i32>* target, i32 value) throws std.alloc::alloc_error {
    try {
        target->push_front(value);
    } catch (std.array::push_error<i32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected o<i32> pop_front(std.deque::deque<i32>* target) throws std.alloc::alloc_error {
    try {
        o<i32> value = target->pop_front();
        return value;
    } catch (std.array::push_error<i32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected o<i32> pop_back(std.deque::deque<i32>* target) throws std.alloc::alloc_error {
    try {
        o<i32> value = target->pop_back();
        return value;
    } catch (std.array::push_error<i32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* Pops every element from the front, or from the back, and lists them. */
protected std.string::string drain(std.deque::deque<i32>* target, bool from_front)
    throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    while (true) {
        o<i32> next = o::none;
        if (from_front == true) {
            next = pop_front(target);
        } else {
            next = pop_back(target);
        }
        switch (next) {
        case variant o::some(value):
            i32 number = *value;
            std.string::string piece = f"{number} ";
            text.append(piece.as_str());
        case variant o::none:
            return move text;
        }
    }
    return move text;
}

/* The value an end holds, or -1 for none. */
protected i32 peek(o<const i32*> end) {
    switch (end) {
    case variant o::some(value): return **value;
    case variant o::none: return -1;
    }
}

@test
void starts_empty() throws std.test::failure, std.alloc::alloc_error {
    std.deque::deque<i32> queue = std.deque::deque<i32>::create();
    std.test::equal(queue.count(), 0usize);
    std.test::check(queue.is_empty(), "a new deque is empty");
    std.test::equal(peek(queue.front()), -1);
    std.test::equal(peek(queue.back()), -1);
    o<i32> front = pop_front(&queue);
    switch (front) {
    case variant o::some(value): std.test::fail("an empty deque has no front");
    case variant o::none: break;
    }
    o<i32> back = pop_back(&queue);
    switch (back) {
    case variant o::some(value): std.test::fail("an empty deque has no back");
    case variant o::none: break;
    }
    std.test::check(queue.is_empty(), "popping an empty deque changes nothing");
}

@test
void queues_first_in_first_out() throws std.test::failure, std.alloc::alloc_error {
    std.deque::deque<i32> queue = std.deque::deque<i32>::create();
    for (i32 value in 1..6) { push_back(&queue, value); }
    std.test::equal(queue.count(), 5usize);
    std.string::string order = drain(&queue, true);
    std.test::equal_text(order.as_str(), "1 2 3 4 5 ");
    std.test::check(queue.is_empty(), "every element was popped");
    for (i32 value in 1..4) { push_front(&queue, value); }
    std.string::string reversed = drain(&queue, false);
    std.test::equal_text(reversed.as_str(), "1 2 3 ");
}

@test
void stacks_at_either_end() throws std.test::failure, std.alloc::alloc_error {
    std.deque::deque<i32> back_stack = std.deque::deque<i32>::create();
    for (i32 value in 1..5) { push_back(&back_stack, value); }
    std.string::string from_back = drain(&back_stack, false);
    std.test::equal_text(from_back.as_str(), "4 3 2 1 ");
    std.deque::deque<i32> front_stack = std.deque::deque<i32>::create();
    for (i32 value in 1..5) { push_front(&front_stack, value); }
    std.string::string from_front = drain(&front_stack, true);
    std.test::equal_text(from_front.as_str(), "4 3 2 1 ");
}

@test
void peeks_at_both_ends() throws std.test::failure, std.alloc::alloc_error {
    std.deque::deque<i32> queue = std.deque::deque<i32>::create();
    push_front(&queue, 20);
    push_front(&queue, 10);
    std.test::equal(peek(queue.front()), 10);
    std.test::equal(peek(queue.back()), 20);
    push_back(&queue, 30);
    std.test::equal(peek(queue.back()), 30);
    std.test::equal(peek(queue.front()), 10);
    std.test::equal(queue.count(), 3usize);
    pop_back(&queue) as void;
    pop_back(&queue) as void;
    std.test::equal(peek(queue.front()), 10);
    std.test::equal(peek(queue.back()), 10);
    std.deque::deque<i32> tail_only = std.deque::deque<i32>::create();
    push_back(&tail_only, 7);
    push_back(&tail_only, 8);
    std.test::equal(peek(tail_only.front()), 7);
    std.test::equal(peek(tail_only.back()), 8);
    std.test::equal(tail_only.count(), 2usize);
}

@test
void mixes_both_ends() throws std.test::failure, std.alloc::alloc_error {
    std.deque::deque<i32> queue = std.deque::deque<i32>::create();
    push_front(&queue, 2);
    push_back(&queue, 3);
    push_front(&queue, 1);
    push_back(&queue, 4);
    o<i32> last = pop_back(&queue);
    switch (last) {
    case variant o::some(value): std.test::equal(*value, 4);
    case variant o::none: std.test::fail("4 is at the back");
    }
    o<i32> first = pop_front(&queue);
    switch (first) {
    case variant o::some(value): std.test::equal(*value, 1);
    case variant o::none: std.test::fail("1 is at the front");
    }
    push_back(&queue, 5);
    push_front(&queue, 0);
    std.string::string order = drain(&queue, true);
    std.test::equal_text(order.as_str(), "0 2 3 5 ");
    push_front(&queue, 9);
    push_front(&queue, 8);
    push_back(&queue, 10);
    std.string::string backwards = drain(&queue, false);
    std.test::equal_text(backwards.as_str(), "10 9 8 ");
}

@test
void clears_and_is_reused() throws std.test::failure, std.alloc::alloc_error {
    std.deque::deque<i32> queue = std.deque::deque<i32>::create();
    push_front(&queue, 1);
    push_back(&queue, 2);
    queue.clear();
    std.test::equal(queue.count(), 0usize);
    std.test::check(queue.is_empty(), "a cleared deque is empty");
    std.test::equal(peek(queue.front()), -1);
    push_back(&queue, 3);
    std.test::equal(peek(queue.front()), 3);
    std.test::equal(peek(queue.back()), 3);
}

@test
void moves_owned_values() throws std.test::failure, std.alloc::alloc_error,
    std.array::push_error<std.string::string> {
    std.deque::deque<std.string::string> words = std.deque::deque<std.string::string>::create();
    words.push_back(std.string::from_str("middle"));
    words.push_front(std.string::from_str("first"));
    std.string::string last = std.string::from_str("last");
    words.push_back(move last);
    o<const std.string::string*> front = words.front();
    switch (front) {
    case variant o::some(word): std.test::equal_text((*word)->as_str(), "first");
    case variant o::none: std.test::fail("the deque has a front");
    }
    o<std.string::string> back = words.pop_back();
    switch (move back) {
    case variant o::some(move word): std.test::equal_text(word.as_str(), "last");
    case variant o::none: std.test::fail("the deque has a back");
    }
    o<std.string::string> head = words.pop_front();
    switch (move head) {
    case variant o::some(move word): std.test::equal_text(word.as_str(), "first");
    case variant o::none: std.test::fail("the deque has a front");
    }
    std.test::equal(words.count(), 1usize);
}

@test(allocations)
void grows_at_both_ends() throws std.test::failure, std.alloc::alloc_error {
    std.deque::deque<i32> queue = std.deque::deque<i32>::create();
    for (i32 value in 0..20) {
        push_back(&queue, value);
        push_front(&queue, -1 - value);
    }
    std.test::equal(queue.count(), 40usize);
    std.test::equal(peek(queue.front()), -20);
    std.test::equal(peek(queue.back()), 19);
    i32 total = 0;
    for (i32 step in 0..10) {
        o<i32> front = pop_front(&queue);
        switch (front) {
        case variant o::some(value): total += *value;
        case variant o::none: std.test::fail("the front half holds twenty values");
        }
    }
    std.test::equal(total, -155);
    std.string::string rest = drain(&queue, false);
    std.test::equal_text(rest.as_str(),
        "19 18 17 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -10 ");
}

/* M24-3: a pop whose rebalancing finds no room takes the element in place instead, so the
   deque keeps every element in order. */
protected void drains_from(std.deque::deque<i32>* queue, i32 first, i32 last, bool front)
    throws std.test::failure, std.alloc::alloc_error, std.array::push_error<i32> {
    for (i32 expected = first; expected <= last; expected += 1) {
        o<i32> next = o::none;
        if (front == true) {
            o<i32> taken = queue->pop_front();
            next = taken;
        } else {
            o<i32> taken = queue->pop_back();
            next = taken;
        }
        switch (next) {
        case variant o::some(value): std.test::equal(*value, expected);
        case variant o::none: std.test::fail("the deque ended early");
        }
    }
    std.test::check(queue->is_empty(), "the deque is empty");
}

@test
void failed_front_rebalance_keeps_the_order() throws std.test::failure, std.alloc::alloc_error,
    std.array::push_error<i32> {
    std.deque::deque<i32> queue = std.deque::deque<i32>::create();
    for (i32 value in 1..7) { queue.push_back(value); }
    std.test::fail_allocation_at(1u64);
    o<i32> first = queue.pop_front();
    std.test::fail_allocation_at(0u64);
    switch (first) {
    case variant o::some(value): std.test::equal(*value, 1);
    case variant o::none: std.test::fail("a front element");
    }
    drains_from(&queue, 2, 6, true);
}

@test
void failed_back_rebalance_keeps_the_order() throws std.test::failure, std.alloc::alloc_error,
    std.array::push_error<i32> {
    std.deque::deque<i32> queue = std.deque::deque<i32>::create();
    for (i32 value in 1..7) { queue.push_front(value); }
    std.test::fail_allocation_at(1u64);
    o<i32> last = queue.pop_back();
    std.test::fail_allocation_at(0u64);
    switch (last) {
    case variant o::some(value): std.test::equal(*value, 1);
    case variant o::none: std.test::fail("a back element");
    }
    drains_from(&queue, 2, 6, false);
}

@test
void failed_public_rebalance_keeps_the_deque() throws std.test::failure, std.alloc::alloc_error,
    std.array::push_error<i32> {
    std.deque::deque<i32> queue = std.deque::deque<i32>::create();
    for (i32 value in 1..7) { queue.push_back(value); }
    std.test::fail_allocation_at(1u64);
    queue.rebalance_front();
    std.test::fail_allocation_at(0u64);
    std.test::equal(queue.count(), 6usize);
    drains_from(&queue, 1, 6, true);
    for (i32 value in 1..7) { queue.push_front(value); }
    std.test::fail_allocation_at(1u64);
    queue.rebalance_back();
    std.test::fail_allocation_at(0u64);
    drains_from(&queue, 1, 6, false);
}
