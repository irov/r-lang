module tests.std.slice;
import std.test;
import std.cmp;
import std.slice;

// The tests of std.slice (Library R-SLIB-SLICE-0001..0002), run in test mode (Core R-FUNC-0025).

/* An element ordered and compared by its key only, so that a test can tell which of equal
   elements a search returned. */
struct entry { i32 key; i32 tag; };

impl std.cmp::Ordered for entry {
    std.cmp::ordering cmp(const entry* this, const entry* other) {
        return this->key.cmp(&other->key);
    }
};

impl std.cmp::Equal for entry {
    bool eq(const entry* this, const entry* other) {
        return this->key == other->key;
    }
};

/* The found index, or -1 for none. */
protected i64 index(o<usize> found) {
    switch (found) {
    case variant o::some(value): return *value as i64;
    case variant o::none: return -1i64;
    }
}

/* The tag of the found element, or -1 for none. */
protected i32 tag(o<const entry*> found) {
    switch (found) {
    case variant o::some(value): return (*value)->tag;
    case variant o::none: return -1;
    }
}

/* The found number, or the fallback for none. */
protected i32 number(o<const i32*> found, i32 fallback) {
    switch (found) {
    case variant o::some(value): return **value;
    case variant o::none: return fallback;
    }
}

/* The numbers, each followed by a space. */
protected std.string::string listing(const i32[] items) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    for (const i32* item in &items) {
        i32 value = *item;
        std.string::string piece = f"{value} ";
        text.append(piece);
    }
    return move text;
}

/* The words, each followed by a space. */
protected std.string::string words_of(const std.string::string[] items)
    throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    for (const std.string::string* item in &items) {
        text.append(*item);
        text.append(" ");
    }
    return move text;
}

/* Appends a copy of the text, turning a failed growth into its allocation error. */
protected void push(array<std.string::string>* target, str text) throws std.alloc::alloc_error {
    std.string::string value = std.string::from_str(text);
    try {
        target->push(move value);
    } catch (std.array::push_error<std.string::string> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

@test
void searches_shared_slices() throws std.test::failure, std.alloc::alloc_error {
    i32[6] values = {4, 8, 15, 16, 8, 42};
    const i32[] view = &values;
    const i32[] nothing = values[0usize..0usize];
    i32 eight = 8;
    i32 nine = 9;
    i32 forty_two = 42;
    std.test::check(std.slice::contains(view, &eight), "8 is present");
    std.test::check(std.slice::contains(view, &nine) == false, "9 is absent");
    std.test::check(std.slice::contains(nothing, &eight) == false, "an empty slice holds nothing");
    std.test::equal(index(std.slice::index_of(view, &eight)), 1i64);
    std.test::equal(index(std.slice::index_of(view, &forty_two)), 5i64);
    std.test::equal(index(std.slice::index_of(view, &nine)), -1i64);
    std.test::equal(index(std.slice::index_of(nothing, &eight)), -1i64);
    std.test::equal(number(std.slice::first(view), 0), 4);
    std.test::equal(number(std.slice::last(view), 0), 42);
    std.test::equal(number(std.slice::first(nothing), -7), -7);
    std.test::equal(number(std.slice::last(nothing), -7), -7);
    std.test::equal(number(std.slice::last(values[0usize..1usize]), 0), 4);
    str[3] words = {"b", "a", "b"};
    str wanted = "b";
    std.test::equal(index(std.slice::index_of(words, &wanted)), 0i64);
}

@test
void finds_the_first_extreme() throws std.test::failure, std.alloc::alloc_error {
    entry[5] entries = {entry {.key = 3, .tag = 1}, entry {.key = 1, .tag = 2},
                        entry {.key = 7, .tag = 3}, entry {.key = 1, .tag = 4},
                        entry {.key = 7, .tag = 5}};
    std.test::equal(tag(std.slice::min_of(entries)), 2);
    std.test::equal(tag(std.slice::max_of(entries)), 3);
    std.test::equal(tag(std.slice::min_of(entries[0usize..0usize])), -1);
    std.test::equal(tag(std.slice::max_of(entries[0usize..0usize])), -1);
    std.test::equal(tag(std.slice::min_of(entries[2usize..3usize])), 3);
    entry probe = {.key = 7, .tag = 0};
    std.test::equal(index(std.slice::index_of(entries, &probe)), 2i64);
    f64[4] measures = {2.5, 0.0 / 0.0, -1.0, 9.0};
    o<const f64*> largest = std.slice::max_of(measures);
    switch (largest) {
    case variant o::some(value):
        f64 found = **value;
        std.test::check(found != found, "NaN orders after every number");
    case variant o::none: std.test::fail("four measures have a greatest one");
    }
    o<const f64*> smallest = std.slice::min_of(measures);
    switch (smallest) {
    case variant o::some(value): std.test::equal(**value, -1.0);
    case variant o::none: std.test::fail("four measures have a least one");
    }
}

@test
void checks_order_and_searches_sorted_slices() throws std.test::failure, std.alloc::alloc_error {
    i32[7] sorted = {-5, 0, 2, 2, 9, 11, 30};
    i32[4] unsorted = {1, 3, 2, 4};
    std.test::check(std.slice::is_sorted(sorted), "ascending with a repeated value");
    std.test::check(std.slice::is_sorted(unsorted) == false, "3 before 2");
    std.test::check(std.slice::is_sorted(sorted[0usize..0usize]), "the empty slice is sorted");
    std.test::check(std.slice::is_sorted(unsorted[2usize..3usize]), "one element is sorted");
    i32 minus_five = -5;
    i32 nine = 9;
    i32 thirty = 30;
    i32 ten = 10;
    i32 huge = 1000;
    i32 tiny = -1000;
    std.test::equal(index(std.slice::binary_search(sorted, &minus_five)), 0i64);
    std.test::equal(index(std.slice::binary_search(sorted, &nine)), 4i64);
    std.test::equal(index(std.slice::binary_search(sorted, &thirty)), 6i64);
    std.test::equal(index(std.slice::binary_search(sorted, &ten)), -1i64);
    std.test::equal(index(std.slice::binary_search(sorted, &huge)), -1i64);
    std.test::equal(index(std.slice::binary_search(sorted, &tiny)), -1i64);
    std.test::equal(index(std.slice::binary_search(sorted[0usize..0usize], &nine)), -1i64);
    i32 two = 2;
    i64 either = index(std.slice::binary_search(sorted, &two));
    std.test::check(either == 2i64 || either == 3i64, "one of the two 2s is found");
    str[4] words = {"apple", "fig", "kiwi", "pear"};
    str kiwi = "kiwi";
    std.test::check(std.slice::is_sorted(words), "words in byte order");
    std.test::equal(index(std.slice::binary_search(words, &kiwi)), 2i64);
}

@test
void swaps_reverses_and_rotates() throws std.test::failure, std.alloc::alloc_error {
    i32[5] values = {1, 2, 3, 4, 5};
    std.slice::swap(&values, 0usize, 4usize);
    std.slice::swap(&values, 2usize, 2usize);
    std.string::string swapped = listing(&values);
    std.test::equal_text(swapped, "5 2 3 4 1 ");
    std.slice::reverse(&values);
    std.string::string reversed = listing(&values);
    std.test::equal_text(reversed, "1 4 3 2 5 ");
    i32[4] even = {1, 2, 3, 4};
    std.slice::reverse(&even);
    std.string::string even_reversed = listing(&even);
    std.test::equal_text(even_reversed, "4 3 2 1 ");
    std.slice::reverse(even[0usize..0usize]);
    std.slice::reverse(even[1usize..2usize]);
    std.string::string unchanged = listing(&even);
    std.test::equal_text(unchanged, "4 3 2 1 ");
    i32[6] ring = {0, 1, 2, 3, 4, 5};
    std.slice::rotate_left(&ring, 2usize);
    std.string::string left = listing(&ring);
    std.test::equal_text(left, "2 3 4 5 0 1 ");
    std.slice::rotate_right(&ring, 2usize);
    std.string::string back = listing(&ring);
    std.test::equal_text(back, "0 1 2 3 4 5 ");
    std.slice::rotate_right(&ring, 1usize);
    std.string::string right = listing(&ring);
    std.test::equal_text(right, "5 0 1 2 3 4 ");
    std.slice::rotate_left(&ring, 0usize);
    std.slice::rotate_left(&ring, 6usize);
    std.slice::rotate_right(&ring, 6usize);
    std.string::string whole = listing(&ring);
    std.test::equal_text(whole, "5 0 1 2 3 4 ");
    std.slice::rotate_left(ring[0usize..0usize], 0usize);
}

@test
void sorts_in_place() throws std.test::failure, std.alloc::alloc_error {
    i32[9] values = {5, -3, 9, 0, 5, 12, -3, 7, 1};
    std.slice::sort(&values);
    std.string::string sorted = listing(&values);
    std.test::equal_text(sorted, "-3 -3 0 1 5 5 7 9 12 ");
    std.test::check(std.slice::is_sorted(values), "sort leaves the slice sorted");
    std.slice::sort(&values);
    std.string::string again = listing(&values);
    std.test::equal_text(again, "-3 -3 0 1 5 5 7 9 12 ");
    i32[5] descending = {5, 4, 3, 2, 1};
    std.slice::sort(&descending);
    std.string::string ascending = listing(&descending);
    std.test::equal_text(ascending, "1 2 3 4 5 ");
    i32[1] single = {42};
    std.slice::sort(&single);
    std.test::equal(single[0usize], 42);
    std.slice::sort(values[0usize..0usize]);
    i32[6] partly = {9, 8, 7, 3, 2, 1};
    std.slice::sort(partly[0usize..3usize]);
    std.string::string prefix = listing(&partly);
    std.test::equal_text(prefix, "7 8 9 3 2 1 ");
    str[4] words = {"pear", "apple", "fig", "Banana"};
    std.slice::sort(&words);
    std.test::equal_text(words[0usize], "Banana");
    std.test::equal_text(words[1usize], "apple");
    std.test::equal_text(words[3usize], "pear");
    f64[5] measures = {2.5, 0.0 / 0.0, -1.0, 0.0, -7.5};
    std.slice::sort(&measures);
    std.test::equal(measures[0usize], -7.5);
    std.test::equal(measures[3usize], 2.5);
    std.test::check(measures[4usize] != measures[4usize], "NaN sorts last");
}

@test
void sorts_by_a_comparison_and_sifts() throws std.test::failure, std.alloc::alloc_error {
    fn std.cmp::ordering descending(const i32* left, const i32* right) {
        return right->cmp(left);
    }
    i32[6] values = {3, 11, -2, 8, 0, 5};
    std.slice::sort_by(&values, &descending);
    std.string::string down = listing(&values);
    std.test::equal_text(down, "11 8 5 3 0 -2 ");
    i32 pivot = 10;
    fn std.cmp::ordering by_distance(const i32* left, const i32* right) {
        i32 left_distance = *left - pivot;
        if (left_distance < 0) { left_distance = -left_distance; }
        i32 right_distance = *right - pivot;
        if (right_distance < 0) { right_distance = -right_distance; }
        return left_distance.cmp(&right_distance);
    }
    i32[5] near = {0, 13, 9, 30, 4};
    std.slice::sort_by(&near, &by_distance);
    std.string::string closest = listing(&near);
    std.test::equal_text(closest, "9 13 4 0 30 ");
    std.slice::sort_by(near[0usize..0usize], &by_distance);
    i32[5] heap = {1, 9, 8, 3, 2};
    std.slice::sift_down(&heap, 0usize, 5usize);
    std.string::string sifted = listing(&heap);
    std.test::equal_text(sifted, "9 3 8 1 2 ");
    std.slice::sift_down(&heap, 1usize, 3usize);
    std.string::string bounded = listing(&heap);
    std.test::equal_text(bounded, "9 3 8 1 2 ");
    i32[3] small = {1, 5, 7};
    std.slice::sift_down(&small, 0usize, 2usize);
    std.string::string limited = listing(&small);
    std.test::equal_text(limited, "5 1 7 ");
}

@test(allocations)
void rearranges_owned_values() throws std.test::failure, std.alloc::alloc_error {
    array<std.string::string> words = std.array::create::<std.string::string>();
    push(&words, "pear");
    push(&words, "apple");
    push(&words, "fig");
    push(&words, "kiwi");
    std.string::string[] to_sort = words.as_slice_mut();
    std.slice::sort(to_sort);
    std.string::string sorted = words_of(words.as_slice());
    std.test::equal_text(sorted, "apple fig kiwi pear ");
    std.string::string[] to_rotate = words.as_slice_mut();
    std.slice::rotate_left(to_rotate, 1usize);
    std.string::string rotated = words_of(words.as_slice());
    std.test::equal_text(rotated, "fig kiwi pear apple ");
    std.string::string[] to_turn = words.as_slice_mut();
    std.slice::reverse(to_turn);
    std.slice::swap(to_turn, 0usize, 3usize);
    std.string::string swapped = words_of(words.as_slice());
    std.test::equal_text(swapped, "fig pear kiwi apple ");
    fn std.cmp::ordering shorter(const std.string::string* left,
                                 const std.string::string* right) {
        usize left_length = left->len();
        usize right_length = right->len();
        std.cmp::ordering by_length = left_length.cmp(&right_length);
        if (by_length != std.cmp::ordering::equal) { return by_length; }
        return left->cmp(right);
    }
    std.string::string[] to_order = words.as_slice_mut();
    std.slice::sort_by(to_order, &shorter);
    std.string::string by_length = words_of(words.as_slice());
    std.test::equal_text(by_length, "fig kiwi pear apple ");
    std.string::string wanted = std.string::from_str("pear");
    const std.string::string[] view = words.as_slice();
    std.test::check(std.slice::contains(view, &wanted), "pear is a word");
    std.test::equal(index(std.slice::index_of(view, &wanted)), 2i64);
    std.string::string[] to_resort = words.as_slice_mut();
    std.slice::sort(to_resort);
    const std.string::string[] sorted_view = words.as_slice();
    std.test::equal(index(std.slice::binary_search(sorted_view, &wanted)), 3i64);
}
