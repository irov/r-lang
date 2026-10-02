module test.codegen.view_holding_parameters;

import std.sync;

/* R-BORROW-0018: a fixed array, option, lock guard, lock outcome, container iterator or entry
   reference that holds views is an ordinary by-value parameter; its hidden regions follow the
   argument, and a result read through it keeps the argument's origins. */

usize total((str)[2] words) {
    return len(words[0]) + len(words[1]);
}

void bump((i32*)[2] cells) {
    *cells[0] += 1;
    *cells[1] += 2;
}

const i32* larger((const i32*)[2] items) {
    if (*items[0] > *items[1]) {
        return items[0];
    }
    return items[1];
}

usize width(o<str> name) {
    switch (name) {
        case variant o::some(text):
            return len(*text);
        case variant o::none:
            return 0usize;
    }
}

i32 through(o<i32*?> cell) {
    switch (move cell) {
        case variant o::some(move slot):
            if (slot == null) {
                return -1;
            }
            return 1;
        case variant o::none:
            return 0;
    }
}

i32 consume(std.sync::mutex_guard<i32> guard) {
    i32 value = *std.sync::mutex_guard_ref(&guard);
    std.sync::unlock(move guard);
    return value;
}

i32 settle(std.sync::lock_result<i32> outcome) {
    switch (move outcome) {
        case variant std.sync::lock_result::locked(move guard):
            return consume(move guard);
        case variant std.sync::lock_result::poisoned(move guard):
            std.sync::unlock(move guard);
            return -1;
        case variant std.sync::lock_result::would_deadlock:
            return -2;
    }
}

i32 rest(std.list::iter<i32> cursor) {
    i32 sum = 0;
    while (true) {
        o<const i32*> item = std.list::next(&cursor);
        switch (item) {
            case variant o::some(value):
                sum += **value;
                break;
            case variant o::none:
                return sum;
        }
    }
}

o<const i32*> head(std.list::iter<i32> cursor) {
    return std.list::next(&cursor);
}

i32 pair_sum(std.dict::entry_ref<i32, i32> entry) {
    return *entry.key + *entry.value;
}

i32 first_pair(std.dict::iter<i32, i32> cursor) {
    o<std.dict::entry_ref<i32, i32>> entry = std.dict::next(&cursor);
    switch (entry) {
        case variant o::some(found):
            return pair_sum(*found);
        case variant o::none:
            return -1;
    }
}

i32 maybe_rest(o<std.list::iter<i32>> cursor) {
    switch (move cursor) {
        case variant o::some(move inner):
            return rest(move inner);
        case variant o::none:
            return 0;
    }
}

i32 check_arrays() {
    (str)[2] words = {"abc", "de"};
    if (total(words) != 5usize) {
        return 1;
    }
    i32 a = 1;
    i32 b = 2;
    (i32*)[2] cells = {&a, &b};
    bump(move cells);
    if ((a != 2) || (b != 4)) {
        return 2;
    }
    (const i32*)[2] pair = {&a, &b};
    const i32* big = larger(pair);
    if (*big != 4) {
        return 3;
    }
    return 0;
}

i32 check_options() {
    str text = "hello";
    if (width(o::some(text)) != 5usize) {
        return 10;
    }
    if (width(o::none) != 0usize) {
        return 11;
    }
    i32 value = 3;
    i32*? slot = &value;
    if (through(o::some(slot)) != 1) {
        return 12;
    }
    return 0;
}

i32 check_guards() {
    std.sync::mutex<i32> mutex = std.sync::mutex_new(41);
    std.sync::lock_result<i32> first = std.sync::lock(&mutex);
    if (settle(move first) != 41) {
        return 20;
    }
    std.sync::lock_result<i32> second = std.sync::lock(&mutex);
    if (settle(move second) != 41) {
        return 21;
    }
    return 0;
}

i32 check_iterators() throws std.list::push_error<i32>, std.dict::insert_error<i32, i32> {
    list<i32> numbers = std.list::create::<i32>();
    std.list::push_back(&numbers, 4) as void;
    std.list::push_back(&numbers, 5) as void;
    std.list::push_back(&numbers, 6) as void;
    if (rest(std.list::iter(&numbers)) != 15) {
        return 30;
    }
    o<const i32*> front = head(std.list::iter(&numbers));
    switch (front) {
        case variant o::some(value):
            if (**value != 4) {
                return 31;
            }
            break;
        case variant o::none:
            return 32;
    }
    if (maybe_rest(o::some(std.list::iter(&numbers))) != 15) {
        return 33;
    }
    dict<i32, i32> table = std.dict::create::<i32, i32>();
    std.dict::insert(&table, 7, 30) as void;
    if (first_pair(std.dict::iter(&table)) != 37) {
        return 34;
    }
    return 0;
}

i32 main() {
    const i32 arrays = check_arrays();
    if (arrays != 0) {
        return arrays;
    }
    const i32 options = check_options();
    if (options != 0) {
        return options;
    }
    const i32 guards = check_guards();
    if (guards != 0) {
        return guards;
    }
    try {
        return check_iterators();
    } catch (std.list::push_error<i32> failure) {
        move failure as void;
        return 90;
    } catch (std.dict::insert_error<i32, i32> failure) {
        move failure as void;
        return 91;
    }
}
