module tests.std.iter;
import std.test;
import std.cmp;
import std.iter;

// The tests of std.iter (Library R-SLIB-ITER-0001..0005), run in test mode (Core R-FUNC-0025).

/* An item ordered by its key only, so that a test can tell which of equal items a reduction
   kept. */
struct entry { i32 key; i32 tag; };

impl std.cmp::Ordered for entry {
    std.cmp::ordering cmp(const entry* this, const entry* other) {
        return this->key.cmp(&other->key);
    }
};

/* A source of the numbers from next_value up to but excluding limit that counts the items it
   produced, so that a test can see where a consumer stopped. */
struct pulls { usize value; };

struct counted {
    i32 next_value;
    i32 limit;
    pulls* produced;
};

impl core::Iterator for counted {
    type Item = i32;
    o<i32> next(counted* this) {
        if (this->next_value >= this->limit) { return o::none; }
        this->produced->value += 1usize;
        i32 value = this->next_value;
        this->next_value += 1;
        return o::some(value);
    }
};

/* Appends `value ` to the text. */
protected void note(std.string::string* text, i32 value) throws std.alloc::alloc_error {
    std.string::string piece = f"{value} ";
    text->append(piece.as_str());
}

@test
void yields_slices_and_ranges() throws std.test::failure, std.alloc::alloc_error {
    i32[4] values = {7, -1, 3, 0};
    std.string::string items = std.string::create();
    for (const i32* value in std.iter::of_slice(values)) { note(&items, *value); }
    std.test::equal_text(items.as_str(), "7 -1 3 0 ");
    std.test::equal(std.iter::count(std.iter::of_slice(values[0usize..0usize])), 0usize);
    std.string::string numbers = std.string::create();
    for (i32 value in std.iter::range(-2, 3)) { note(&numbers, value); }
    std.test::equal_text(numbers.as_str(), "-2 -1 0 1 2 ");
    std.test::equal(std.iter::count(std.iter::range(4, 4)), 0usize);
    std.test::equal(std.iter::count(std.iter::range(5, 1)), 0usize);
    usize total = 0usize;
    for (usize index in std.iter::range_of_usize(2usize, 5usize)) { total += index; }
    std.test::equal(total, 9usize);
    std.test::equal(std.iter::count(std.iter::range_of_usize(3usize, 3usize)), 0usize);
}

@test
void maps_and_filters() throws std.test::failure, std.alloc::alloc_error {
    fn i32 square(i32 value) { return value * value; }
    std.string::string squares = std.string::create();
    for (i32 value in std.iter::map(std.iter::range(1, 5), &square)) { note(&squares, value); }
    std.test::equal_text(squares.as_str(), "1 4 9 16 ");
    fn bool even(const i32* value) { return *value % 2 == 0; }
    std.string::string evens = std.string::create();
    for (i32 value in std.iter::filter(std.iter::range(-3, 4), &even)) { note(&evens, value); }
    std.test::equal_text(evens.as_str(), "-2 0 2 ");
    i32 limit = 100;
    fn bool above(const i32* value) { return *value > limit; }
    std.test::equal(std.iter::count(std.iter::filter(std.iter::range(0, 10), &above)), 0usize);
    fn o<u32> small_positive(i32 value) {
        if (value > 0 && value < 3) { return o::some(value as u32); }
        return o::none;
    }
    u32 kept = 0u32;
    for (u32 value in std.iter::filter_map(std.iter::range(-2, 6), &small_positive)) {
        kept = kept * 10u32 + value;
    }
    std.test::equal(kept, 12u32);
    i32[3] values = {4, -5, 6};
    fn i32 doubled(const i32* value) { return *value * 2; }
    std.string::string twice = std.string::create();
    for (i32 value in std.iter::map(std.iter::of_slice(values), &doubled)) { note(&twice, value); }
    std.test::equal_text(twice.as_str(), "8 -10 12 ");
}

@test
void takes_skips_chains_and_pairs() throws std.test::failure, std.alloc::alloc_error {
    std.test::equal(std.iter::count(std.iter::take(std.iter::range(0, 10), 3usize)), 3usize);
    std.test::equal(std.iter::count(std.iter::take(std.iter::range(0, 2), 5usize)), 2usize);
    std.test::equal(std.iter::count(std.iter::take(std.iter::range(0, 10), 0usize)), 0usize);
    std.string::string skipped = std.string::create();
    for (i32 value in std.iter::skip(std.iter::range(0, 5), 2usize)) { note(&skipped, value); }
    std.test::equal_text(skipped.as_str(), "2 3 4 ");
    std.test::equal(std.iter::count(std.iter::skip(std.iter::range(0, 3), 7usize)), 0usize);
    std.string::string window = std.string::create();
    for (i32 value in std.iter::take(std.iter::skip(std.iter::range(0, 10), 4usize), 3usize)) {
        note(&window, value);
    }
    std.test::equal_text(window.as_str(), "4 5 6 ");
    std.string::string joined = std.string::create();
    for (i32 value in std.iter::chain(std.iter::range(0, 2), std.iter::range(5, 7))) {
        note(&joined, value);
    }
    std.test::equal_text(joined.as_str(), "0 1 5 6 ");
    std.string::string second_only = std.string::create();
    for (i32 value in std.iter::chain(std.iter::range(3, 3), std.iter::range(1, 2))) {
        note(&second_only, value);
    }
    std.test::equal_text(second_only.as_str(), "1 ");
    i32[2] front = {1, 2};
    i32[1] back = {3};
    std.string::string slices = std.string::create();
    for (const i32* value in std.iter::chain(std.iter::of_slice(front), std.iter::of_slice(back))) {
        note(&slices, *value);
    }
    std.test::equal_text(slices.as_str(), "1 2 3 ");
    i32[3] values = {5, 6, 7};
    usize weighted = 0usize;
    usize last_index = 0usize;
    for (std.iter::indexed<const i32*> item in std.iter::enumerate(std.iter::of_slice(values))) {
        weighted += item.index * (*item.value as usize);
        last_index = item.index;
    }
    std.test::equal(weighted, 20usize);
    std.test::equal(last_index, 2usize);
    std.test::equal(std.iter::count(std.iter::enumerate(std.iter::range(0, 0))), 0usize);
    str[2] words = {"one", "two"};
    std.string::string pairs = std.string::create();
    for (std.iter::pair<i32, const str*> both in
         std.iter::zip(std.iter::range(1, 10), std.iter::of_slice(words))) {
        note(&pairs, both.left);
        pairs.append(*both.right);
        pairs.append(";");
    }
    std.test::equal_text(pairs.as_str(), "1 one;2 two;");
    i32 products = 0;
    for (std.iter::pair<i32, i32> both in
         std.iter::zip(std.iter::range(1, 3), std.iter::range(10, 20))) {
        products += both.left * both.right;
    }
    std.test::equal(products, 1 * 10 + 2 * 11);
}

@test
void folds_and_stops_at_the_decisive_item() throws std.test::failure, std.alloc::alloc_error {
    fn i32 add(i32 total, i32 value) { return total + value; }
    fn i32 digits(i32 total, i32 value) { return total * 10 + value; }
    std.test::equal(std.iter::fold(std.iter::range(1, 5), 0, &add), 10);
    std.test::equal(std.iter::fold(std.iter::range(0, 0), 42, &add), 42);
    std.test::equal(std.iter::fold(std.iter::range(1, 4), 0, &digits), 123);
    fn bool big(i32 value) { return value > 2; }
    fn bool small(i32 value) { return value < 2; }
    pulls any_pulls = {.value = 0usize};
    bool found =
        std.iter::any(counted {.next_value = 0, .limit = 10, .produced = &any_pulls}, &big);
    std.test::check(found, "3 is big");
    std.test::equal(any_pulls.value, 4usize);
    pulls all_pulls = {.value = 0usize};
    bool every =
        std.iter::all(counted {.next_value = 0, .limit = 10, .produced = &all_pulls}, &small);
    std.test::check(every == false, "2 is not small");
    std.test::equal(all_pulls.value, 3usize);
    std.test::check(std.iter::any(std.iter::range(0, 0), &big) == false, "no item is big");
    std.test::check(std.iter::all(std.iter::range(0, 0), &small), "every item of none is small");
    std.test::check(std.iter::all(std.iter::range(-5, 2), &small), "all below 2");
}

@test
void finds_positions_and_items() throws std.test::failure, std.alloc::alloc_error {
    fn bool seven(i32 value) { return value == 7; }
    o<usize> where = std.iter::position(std.iter::range(5, 10), &seven);
    switch (where) {
    case variant o::some(index): std.test::equal(*index, 2usize);
    case variant o::none: std.test::fail("7 is in 5..10");
    }
    o<usize> nowhere = std.iter::position(std.iter::range(0, 3), &seven);
    switch (nowhere) {
    case variant o::some(index): std.test::fail("7 is not in 0..3");
    case variant o::none: break;
    }
    fn o<i32> half_of_even(i32 value) {
        if (value > 0 && value % 2 == 0) { return o::some(value / 2); }
        return o::none;
    }
    pulls find_pulls = {.value = 0usize};
    o<i32> half = std.iter::find_map(
        counted {.next_value = 5, .limit = 20, .produced = &find_pulls}, &half_of_even);
    switch (half) {
    case variant o::some(value): std.test::equal(*value, 3);
    case variant o::none: std.test::fail("6 is the first even number from 5");
    }
    std.test::equal(find_pulls.value, 2usize);
    o<i32> none_found = std.iter::find_map(std.iter::range(-4, 0), &half_of_even);
    switch (none_found) {
    case variant o::some(value): std.test::fail("no positive even number below 0");
    case variant o::none: break;
    }
    o<i32> final = std.iter::last(std.iter::range(0, 4));
    switch (final) {
    case variant o::some(value): std.test::equal(*value, 3);
    case variant o::none: std.test::fail("0..4 has a last item");
    }
    o<i32> no_final = std.iter::last(std.iter::range(0, 0));
    switch (no_final) {
    case variant o::some(value): std.test::fail("an empty range has no last item");
    case variant o::none: break;
    }
    o<i32> third = std.iter::nth(std.iter::range(10, 15), 2usize);
    switch (third) {
    case variant o::some(value): std.test::equal(*value, 12);
    case variant o::none: std.test::fail("10..15 has a third item");
    }
    o<i32> first = std.iter::nth(std.iter::range(10, 15), 0usize);
    switch (first) {
    case variant o::some(value): std.test::equal(*value, 10);
    case variant o::none: std.test::fail("10..15 has a first item");
    }
    o<i32> beyond = std.iter::nth(std.iter::range(0, 3), 3usize);
    switch (beyond) {
    case variant o::some(value): std.test::fail("0..3 has no fourth item");
    case variant o::none: break;
    }
}

@test
void walks_slices_backwards_and_in_pieces() throws std.test::failure, std.alloc::alloc_error {
    i32[5] values = {1, 2, 3, 4, 5};
    std.string::string backwards = std.string::create();
    for (const i32* value in std.iter::reversed(values)) { note(&backwards, *value); }
    std.test::equal_text(backwards.as_str(), "5 4 3 2 1 ");
    std.test::equal(std.iter::count(std.iter::reversed(values[0usize..0usize])), 0usize);
    std.string::string pieces = std.string::create();
    for (const i32[] piece in std.iter::chunks(values, 2usize)) {
        pieces.append("[");
        for (const i32* value in std.iter::of_slice(piece)) { note(&pieces, *value); }
        pieces.append("]");
    }
    std.test::equal_text(pieces.as_str(), "[1 2 ][3 4 ][5 ]");
    std.test::equal(std.iter::count(std.iter::chunks(values, 5usize)), 1usize);
    std.test::equal(std.iter::count(std.iter::chunks(values, 9usize)), 1usize);
    std.test::equal(std.iter::count(std.iter::chunks(values, 0usize)), 0usize);
    std.test::equal(std.iter::count(std.iter::chunks(values[0usize..0usize], 2usize)), 0usize);
    std.string::string runs = std.string::create();
    for (const i32[] run in std.iter::windows(values, 3usize)) {
        runs.append("[");
        for (const i32* value in std.iter::of_slice(run)) { note(&runs, *value); }
        runs.append("]");
    }
    std.test::equal_text(runs.as_str(), "[1 2 3 ][2 3 4 ][3 4 5 ]");
    std.test::equal(std.iter::count(std.iter::windows(values, 5usize)), 1usize);
    std.test::equal(std.iter::count(std.iter::windows(values, 6usize)), 0usize);
    std.test::equal(std.iter::count(std.iter::windows(values, 0usize)), 0usize);
    std.test::equal(std.iter::count(std.iter::windows(values[0usize..0usize], 1usize)), 0usize);
}

@test
void reduces_to_extremes_and_sums() throws std.test::failure, std.alloc::alloc_error {
    o<i32> least = std.iter::min(std.iter::range(3, 9));
    switch (least) {
    case variant o::some(value): std.test::equal(*value, 3);
    case variant o::none: std.test::fail("3..9 has a least item");
    }
    o<i32> most = std.iter::max(std.iter::range(3, 9));
    switch (most) {
    case variant o::some(value): std.test::equal(*value, 8);
    case variant o::none: std.test::fail("3..9 has a greatest item");
    }
    o<i32> none_least = std.iter::min(std.iter::range(5, 5));
    switch (none_least) {
    case variant o::some(value): std.test::fail("an empty range has no least item");
    case variant o::none: break;
    }
    o<i32> none_most = std.iter::max(std.iter::range(5, 5));
    switch (none_most) {
    case variant o::some(value): std.test::fail("an empty range has no greatest item");
    case variant o::none: break;
    }
    fn entry copy_entry(const entry* value) { return *value; }
    entry[5] entries = {entry {.key = 4, .tag = 1}, entry {.key = 2, .tag = 2},
                        entry {.key = 9, .tag = 3}, entry {.key = 2, .tag = 4},
                        entry {.key = 9, .tag = 5}};
    o<entry> first_least = std.iter::min(std.iter::map(std.iter::of_slice(entries), &copy_entry));
    switch (first_least) {
    case variant o::some(value): std.test::equal(value->tag, 2);
    case variant o::none: std.test::fail("the entries have a least one");
    }
    o<entry> first_most = std.iter::max(std.iter::map(std.iter::of_slice(entries), &copy_entry));
    switch (first_most) {
    case variant o::some(value): std.test::equal(value->tag, 3);
    case variant o::none: std.test::fail("the entries have a greatest one");
    }
    str[3] words = {"pear", "apple", "fig"};
    fn str word(const str* value) { return *value; }
    o<str> first_word = std.iter::min(std.iter::map(std.iter::of_slice(words), &word));
    switch (first_word) {
    case variant o::some(value): std.test::equal_text(*value, "apple");
    case variant o::none: std.test::fail("the words have a least one");
    }
    std.test::equal(std.iter::sum(std.iter::range(1, 5), 0), 10);
    std.test::equal(std.iter::sum(std.iter::range(0, 0), 7), 7);
    std.test::equal(std.iter::sum(std.iter::range(-3, 1), 0), -6);
    std.test::equal(std.iter::sum(std.iter::range_of_usize(1usize, 4usize), 1usize), 7usize);
    fn f64 quarter(i32 value) { return (value as f64) / 4.0; }
    std.test::equal(std.iter::sum(std.iter::map(std.iter::range(0, 4), &quarter), 0.5), 2.0);
    fn u64 wide(i32 value) { return (value as u64) * 4000000000u64; }
    std.test::equal(std.iter::sum(std.iter::map(std.iter::range(1, 3), &wide), 0u64),
                    12000000000u64);
}

@test(allocations)
void collects_into_containers() throws std.test::failure, std.alloc::alloc_error {
    fn i32 square(i32 value) { return value * value; }
    fn bool odd(const i32* value) { return *value % 2 != 0; }
    try {
        array<i32> squares = std.iter::collect_array(std.iter::map(std.iter::range(0, 5), &square));
        std.test::equal(len(squares), 5usize);
        std.test::equal(squares[0usize], 0);
        std.test::equal(squares[4usize], 16);
        array<i32> nothing = std.iter::collect_array(std.iter::range(3, 3));
        std.test::equal(len(nothing), 0usize);
        list<i32> odds = std.iter::collect_list(std.iter::filter(std.iter::range(0, 8), &odd));
        std.test::equal(len(odds), 4usize);
        o<const i32*> first = std.list::front(&odds);
        switch (first) {
        case variant o::some(value): std.test::equal(**value, 1);
        case variant o::none: std.test::fail("the odd numbers have a first one");
        }
        o<const i32*> final = std.list::back(&odds);
        switch (final) {
        case variant o::some(value): std.test::equal(**value, 7);
        case variant o::none: std.test::fail("the odd numbers have a last one");
        }
    } catch (std.array::push_error<i32> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    } catch (std.list::push_error<i32> failure) {
        switch (move failure) {
        case variant std.list::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}
