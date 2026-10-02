module test.codegen.library_iter_reductions;

import std.cmp;
import std.iter;

// R-SLIB-ITER-0004..0005 (M19): filter, reversed, chunks and windows; min, max and sum under
// associated type constraints (Core R-TYPE-0043), also from a generic function whose own header
// states the constraint.

struct entry { i32 key; i32 tag; };

impl std.cmp::Ordered for entry {
    std.cmp::ordering cmp(const entry* this, const entry* other) {
        if (this->key < other->key) { return std.cmp::ordering::less; }
        if (this->key > other->key) { return std.cmp::ordering::greater; }
        return std.cmp::ordering::equal;
    }
};

trait Counted : core::Iterator {};
impl Counted for std.iter::range_i32 {};

@generic<I: Counted, I::Item: std.cmp::Ordered & copy>
o<I::Item> smallest(I inner) { return std.iter::min(move inner); }

protected i32 check_adapters() {
    i32[7] values = {5, 1, 4, 1, 5, 9, 2};
    fn bool odd(const i32* value) { return *value % 2 != 0; }
    i32 odd_total = 0;
    usize odd_count = 0usize;
    for (i32 value in std.iter::filter(std.iter::range(0, 10), &odd)) {
        odd_total += value;
        odd_count += 1usize;
    }
    if (odd_total != 25 || odd_count != 5usize) { return 1; }
    fn bool huge(const i32* value) { return *value > 100; }
    usize kept = std.iter::count(std.iter::filter(std.iter::range(0, 10), &huge));
    if (kept != 0usize) { return 2; }
    i32 order = 0;
    for (const i32* value in std.iter::reversed(values)) { order = order * 10 + *value; }
    if (order != 2951415) { return 3; }
    usize nothing = std.iter::count(std.iter::reversed(values[0usize..0usize]));
    if (nothing != 0usize) { return 4; }
    usize pieces = 0usize;
    usize last_length = 0usize;
    for (const i32[] piece in std.iter::chunks(values, 3usize)) {
        pieces += 1usize;
        last_length = len(piece);
        if (pieces == 2usize && piece[0] != 1) { return 5; }
    }
    if (pieces != 3usize || last_length != 1usize) { return 6; }
    if (std.iter::count(std.iter::chunks(values, 0usize)) != 0usize) { return 7; }
    usize windows = 0usize;
    i32 sums = 0;
    for (const i32[] window in std.iter::windows(values, 3usize)) {
        windows += 1usize;
        sums += window[0] + window[1] + window[2];
    }
    if (windows != 5usize || sums != 10 + 6 + 10 + 15 + 16) { return 8; }
    if (std.iter::count(std.iter::windows(values, 8usize)) != 0usize) { return 9; }
    if (std.iter::count(std.iter::windows(values, 0usize)) != 0usize) { return 10; }
    if (std.iter::count(std.iter::windows(values, 7usize)) != 1usize) { return 11; }
    return 0;
}

protected i32 check_reductions() {
    o<i32> least = std.iter::min(std.iter::range(3, 9));
    switch (least) {
    case variant o::some(value): if (*value != 3) { return 20; }
    case variant o::none: return 21;
    }
    o<i32> most = std.iter::max(std.iter::range(3, 9));
    switch (most) {
    case variant o::some(value): if (*value != 8) { return 22; }
    case variant o::none: return 23;
    }
    o<i32> empty = std.iter::min(std.iter::range(5, 5));
    switch (empty) {
    case variant o::some(value): return 24;
    case variant o::none: break;
    }
    fn entry take_entry(const entry* value) { return *value; }
    entry[4] entries = {entry {.key = 2, .tag = 1}, entry {.key = 1, .tag = 2},
                        entry {.key = 3, .tag = 3}, entry {.key = 1, .tag = 4}};
    o<entry> first_least =
        std.iter::min(std.iter::map(std.iter::of_slice(entries), &take_entry));
    switch (first_least) {
    case variant o::some(value): if (value->tag != 2) { return 25; }
    case variant o::none: return 26;
    }
    entry[3] ties = {entry {.key = 7, .tag = 1}, entry {.key = 7, .tag = 2},
                     entry {.key = 5, .tag = 3}};
    o<entry> first_most = std.iter::max(std.iter::map(std.iter::of_slice(ties), &take_entry));
    switch (first_most) {
    case variant o::some(value): if (value->tag != 1) { return 27; }
    case variant o::none: return 28;
    }
    o<i32> through = smallest(std.iter::range(4, 6));
    switch (through) {
    case variant o::some(value): if (*value != 4) { return 29; }
    case variant o::none: return 30;
    }
    if (std.iter::sum(std.iter::range(1, 5), 0) != 10) { return 31; }
    if (std.iter::sum(std.iter::range(0, 0), 7) != 7) { return 32; }
    if (std.iter::sum(std.iter::range_of_usize(1usize, 4usize), 1usize) != 7usize) { return 33; }
    fn i64 wide(i32 value) { return (value as i64) * 1000000000000i64; }
    if (std.iter::sum(std.iter::map(std.iter::range(1, 3), &wide), 0i64) != 3000000000000i64) {
        return 34;
    }
    fn isize signed(i32 value) { return (value - 2) as isize; }
    if (std.iter::sum(std.iter::map(std.iter::range(0, 3), &signed), 0isize) != -3isize) {
        return 35;
    }
    fn u32 narrow(i32 value) { return value as u32; }
    if (std.iter::sum(std.iter::map(std.iter::range(0, 4), &narrow), 0u32) != 6u32) { return 36; }
    fn u64 big(i32 value) { return value as u64; }
    if (std.iter::sum(std.iter::map(std.iter::range(0, 4), &big), 4u64) != 10u64) { return 37; }
    fn f32 half(i32 value) { return (value as f32) / 2.0f32; }
    if (std.iter::sum(std.iter::map(std.iter::range(0, 4), &half), 0.0f32) != 3.0f32) {
        return 38;
    }
    fn f64 quarter(i32 value) { return (value as f64) / 4.0; }
    if (std.iter::sum(std.iter::map(std.iter::range(0, 4), &quarter), 0.5) != 2.0) { return 39; }
    return 0;
}

i32 main() {
    i32 adapters = check_adapters();
    if (adapters != 0) { return adapters; }
    return check_reductions();
}
