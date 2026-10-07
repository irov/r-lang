module tests.std.sorted;
import std.test;
import std.cmp;
import std.sorted;

// The tests of std.sorted (Library R-SLIB-SORTED-0001..0002), run in test mode
// (Core R-FUNC-0025).

/* A program key ordered by its fields in declaration order (Core R-AGG-0012). */
@derive(equal, ordered)
struct version { u16 major; u16 minor; };

/* The growth operations with a failed growth turned into its allocation error. */
protected bool insert(std.sorted::set<i32>* target, i32 key) throws std.alloc::alloc_error {
    try {
        bool added = target->insert(key);
        return added;
    } catch (std.array::push_error<i32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected o<u32> put(std.sorted::map<i32, u32>* target, i32 key, u32 value)
    throws std.alloc::alloc_error {
    try {
        o<u32> previous = target->insert(key, value);
        return previous;
    } catch (std.array::push_error<i32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    } catch (std.array::push_error<u32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* The keys of the set, each followed by a space. */
protected std.string::string listing(const std.sorted::set<i32>* source)
    throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    const i32[] keys = source->as_slice();
    for (const i32* key in &keys) {
        i32 value = *key;
        std.string::string piece = f"{value} ";
        text.append(piece);
    }
    return move text;
}

/* The value stored under the key, or -1 for none. */
protected i64 value_of(const std.sorted::map<i32, u32>* source, i32 key) {
    o<const u32*> found = source->get(&key);
    switch (found) {
    case variant o::some(value): return **value as i64;
    case variant o::none: return -1i64;
    }
}

/* The payload of an option, or -1 for none. */
protected i64 held(o<u32> value) {
    switch (value) {
    case variant o::some(stored): return *stored as i64;
    case variant o::none: return -1i64;
    }
}

@test
void set_keeps_keys_sorted_and_unique() throws std.test::failure, std.alloc::alloc_error {
    std.sorted::set<i32> keys = std.sorted::set<i32>::create();
    std.test::equal(keys.count(), 0usize);
    const i32[] nothing = keys.as_slice();
    std.test::equal(len(nothing), 0usize);
    std.test::check(insert(&keys, 5), "5 is new");
    std.test::check(insert(&keys, 1), "1 is new");
    std.test::check(insert(&keys, 3), "3 is new");
    std.test::check(insert(&keys, 3) == false, "3 is already present");
    std.test::check(insert(&keys, -2), "-2 is new and goes first");
    std.test::check(insert(&keys, 9), "9 is new and goes last");
    std.test::check(insert(&keys, 9) == false, "9 is already present");
    std.test::equal(keys.count(), 5usize);
    std.string::string text = listing(&keys);
    std.test::equal_text(text, "-2 1 3 5 9 ");
    i32 three = 3;
    i32 four = 4;
    std.test::check(keys.contains(&three), "contains 3");
    std.test::check(keys.contains(&four) == false, "does not contain 4");
}

@test
void set_finds_lower_bounds() throws std.test::failure, std.alloc::alloc_error {
    std.sorted::set<i32> keys = std.sorted::set<i32>::create();
    i32 any = 7;
    std.test::equal(keys.lower_bound(&any), 0usize);
    insert(&keys, 10) as void;
    insert(&keys, 20) as void;
    insert(&keys, 30) as void;
    i32 below = 5;
    i32 first = 10;
    i32 between = 25;
    i32 last = 30;
    i32 above = 31;
    std.test::equal(keys.lower_bound(&below), 0usize);
    std.test::equal(keys.lower_bound(&first), 0usize);
    std.test::equal(keys.lower_bound(&between), 2usize);
    std.test::equal(keys.lower_bound(&last), 2usize);
    std.test::equal(keys.lower_bound(&above), 3usize);
}

@test
void set_removes_keys() throws std.test::failure, std.alloc::alloc_error {
    std.sorted::set<i32> keys = std.sorted::set<i32>::create();
    for (i32 value in 1..6) { insert(&keys, value * 2) as void; }
    i32 six = 6;
    i32 seven = 7;
    i32 two = 2;
    i32 ten = 10;
    std.test::check(keys.remove(&six), "6 was present");
    std.test::check(keys.remove(&six) == false, "6 is gone");
    std.test::check(keys.remove(&seven) == false, "7 was never present");
    std.test::check(keys.remove(&two), "the first key is removed");
    std.test::check(keys.remove(&ten), "the last key is removed");
    std.string::string text = listing(&keys);
    std.test::equal_text(text, "4 8 ");
    std.test::check(keys.contains(&six) == false, "6 is no longer contained");
    std.test::check(insert(&keys, 6), "6 can be inserted again");
    std.string::string again = listing(&keys);
    std.test::equal_text(again, "4 6 8 ");
    std.sorted::set<i32> empty = std.sorted::set<i32>::create();
    std.test::check(empty.remove(&six) == false, "an empty set removes nothing");
}

@test
void map_inserts_and_replaces() throws std.test::failure, std.alloc::alloc_error {
    std.sorted::map<i32, u32> ages = std.sorted::map<i32, u32>::create();
    std.test::equal(ages.count(), 0usize);
    std.test::equal(held(put(&ages, 7, 70u32)), -1i64);
    std.test::equal(held(put(&ages, 3, 30u32)), -1i64);
    std.test::equal(held(put(&ages, 7, 71u32)), 70i64);
    std.test::equal(held(put(&ages, 7, 72u32)), 71i64);
    std.test::equal(ages.count(), 2usize);
    std.test::equal(value_of(&ages, 7), 72i64);
    std.test::equal(value_of(&ages, 3), 30i64);
    std.test::equal(value_of(&ages, 5), -1i64);
    i32 three = 3;
    i32 four = 4;
    std.test::check(ages.contains(&three), "contains 3");
    std.test::check(ages.contains(&four) == false, "does not contain 4");
}

@test
void map_keeps_values_with_their_keys() throws std.test::failure, std.alloc::alloc_error {
    std.sorted::map<i32, u32> squares = std.sorted::map<i32, u32>::create();
    for (i32 step in 0..8) {
        i32 key = 7 - step;
        put(&squares, key, (key * key) as u32) as void;
    }
    put(&squares, -3, 9u32) as void;
    put(&squares, 20, 400u32) as void;
    std.test::equal(squares.count(), 10usize);
    for (i32 key in 0..8) {
        std.test::equal(value_of(&squares, key), (key * key) as i64);
    }
    std.test::equal(value_of(&squares, -3), 9i64);
    std.test::equal(value_of(&squares, 20), 400i64);
    i32 four = 4;
    i32 eight = 8;
    std.test::equal(squares.lower_bound(&four), 5usize);
    std.test::equal(squares.lower_bound(&eight), 9usize);
    std.test::equal(held(squares.remove(&four)), 16i64);
    std.test::equal(held(squares.remove(&four)), -1i64);
    std.test::equal(squares.count(), 9usize);
    std.test::equal(value_of(&squares, 3), 9i64);
    std.test::equal(value_of(&squares, 5), 25i64);
    std.test::equal(value_of(&squares, 20), 400i64);
    std.test::equal(squares.lower_bound(&four), 5usize);
    std.test::equal(squares.lower_bound(&eight), 8usize);
}

@test
void map_handles_missing_keys() throws std.test::failure, std.alloc::alloc_error {
    std.sorted::map<i32, u32> empty = std.sorted::map<i32, u32>::create();
    i32 key = 1;
    std.test::equal(value_of(&empty, key), -1i64);
    std.test::equal(held(empty.remove(&key)), -1i64);
    std.test::check(empty.contains(&key) == false, "an empty map contains nothing");
    std.test::equal(empty.lower_bound(&key), 0usize);
    std.test::equal(empty.count(), 0usize);
}

@test
void orders_program_and_character_keys() throws std.test::failure, std.alloc::alloc_error,
    std.array::push_error<version>, std.array::push_error<char>, std.array::push_error<f64> {
    std.sorted::set<version> releases = std.sorted::set<version>::create();
    releases.insert(version {.major = 1u16, .minor = 10u16});
    releases.insert(version {.major = 1u16, .minor = 2u16});
    releases.insert(version {.major = 0u16, .minor = 99u16});
    std.test::check(releases.insert(version {.major = 1u16, .minor = 2u16}) == false,
                    "1.2 is already present");
    const version[] ordered = releases.as_slice();
    std.test::equal(ordered[0usize].major, 0u16);
    std.test::equal(ordered[1usize].minor, 2u16);
    std.test::equal(ordered[2usize].minor, 10u16);
    std.sorted::map<char, f64> weights = std.sorted::map<char, f64>::create();
    weights.insert('z', 1.5);
    weights.insert('a', 0.25);
    weights.insert('é', 3.0);
    char a = 'a';
    char accented = 'é';
    std.test::equal(weights.lower_bound(&a), 0usize);
    std.test::equal(weights.lower_bound(&accented), 2usize);
    o<const f64*> weight = weights.get(&accented);
    switch (weight) {
    case variant o::some(value): std.test::equal(**value, 3.0);
    case variant o::none: std.test::fail("é has a weight");
    }
}

@test(allocations)
void builds_large_collections() throws std.test::failure, std.alloc::alloc_error {
    std.sorted::set<i32> keys = std.sorted::set<i32>::create();
    for (i32 step in 0..40) { insert(&keys, (step * 17) % 40) as void; }
    std.test::equal(keys.count(), 40usize);
    const i32[] view = keys.as_slice();
    std.test::equal(len(view), 40usize);
    for (i32 position in 0..40) {
        std.test::equal(view[position as usize], position);
    }
    std.sorted::map<i32, u32> map = std.sorted::map<i32, u32>::create();
    std.test::equal(map.count(), 0usize);
    for (i32 step in 0..30) {
        i32 key = (step * 7) % 30;
        put(&map, key, (key + 100) as u32) as void;
    }
    for (i32 key in 0..30) {
        std.test::equal(value_of(&map, key), (key + 100) as i64);
    }
}

/* M24-4: an insert whose value finds no room takes its key out again, so keys and values stay
   in step and a retry succeeds. */
@test
void failed_insert_keeps_keys_and_values() throws std.test::failure, std.alloc::alloc_error,
    std.array::push_error<i32> {
    std.sorted::map<i32, i32> table = std.sorted::map<i32, i32>::create();
    table.insert(1, 10);
    table.insert(3, 30);
    table.insert(5, 50);
    table.insert(7, 70);
    bool failed = false;
    std.test::fail_allocation_at(2u64);
    try {
        o<i32> previous = table.insert(2, 20);
        previous as void;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        failed = true;
    }
    std.test::fail_allocation_at(0u64);
    std.test::check(failed == true, "the value push fails");
    std.test::equal(table.count(), 4usize);
    i32 two = 2;
    std.test::check(table.contains(&two) == false, "the failed key left again");
    o<i32> again = table.insert(2, 20);
    again as void;
    std.test::equal(table.count(), 5usize);
    o<const i32*> found = table.get(&two);
    switch (found) {
    case variant o::some(value): std.test::equal(**value, 20);
    case variant o::none: std.test::fail("key 2 after the retry");
    }
}
