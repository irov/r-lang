module example.textkit.numbers;
import std.cmp;
import std.iter;

/* How many items equal the greatest one. The header constrains the items of I (Core
   R-TYPE-0043), so the body compares them and copies the best one. */
@generic<I: core::Iterator, I::Item: std.cmp::Ordered & copy>
usize count_of_greatest(I inner) {
    o<I::Item> best = o::none;
    usize count = 0usize;
    for (I::Item value in move inner) {
        switch (best) {
        case variant o::some(current):
            std.cmp::ordering order = value.cmp(current);
            if (order == std.cmp::ordering::greater) {
                best = o::some(value);
                count = 1usize;
            } else {
                if (order == std.cmp::ordering::equal) { count += 1usize; }
            }
        case variant o::none:
            best = o::some(value);
            count = 1usize;
        }
    }
    return count;
}

/* The items added to start, from any iterator whose items add: the header constrains the items
   of I with std.iter::Summable, which std.iter::sum requires as well. */
@generic<I: core::Iterator, I::Item: std.iter::Summable>
I::Item total_of(I inner, I::Item start) {
    return std.iter::sum(move inner, start);
}

/* textkit stats NUMBER...: least, greatest and sum, the even values, the values in reverse,
   the sums of chunks of three and the steps between neighbours. */
std.string::string statistics(const i64[] values) throws std.alloc::alloc_error {
    fn i64 value_of(const i64* item) { return *item; }
    fn bool even(const i64* item) { return *item % 2i64 == 0i64; }
    o<i64> least = std.iter::min(std.iter::map(std.iter::of_slice(values), &value_of));
    o<i64> greatest = std.iter::max(std.iter::map(std.iter::of_slice(values), &value_of));
    i64 total = total_of(std.iter::map(std.iter::of_slice(values), &value_of), 0i64);
    usize evens = std.iter::count(
        std.iter::filter(std.iter::map(std.iter::of_slice(values), &value_of), &even));
    usize tied = count_of_greatest(std.iter::map(std.iter::of_slice(values), &value_of));
    std.string::string out = f"sum {total} even {evens} greatest {tied}x";
    switch (least) {
    case variant o::some(low):
        i64 low_value = *low;
        std.string::string row = f" min {low_value}";
        std.string::append_str(&out, row.as_str());
    case variant o::none:
        std.string::append_str(&out, " min -");
    }
    switch (greatest) {
    case variant o::some(high):
        i64 high_value = *high;
        std.string::string row = f" max {high_value}";
        std.string::append_str(&out, row.as_str());
    case variant o::none:
        std.string::append_str(&out, " max -");
    }
    std.string::append_str(&out, "\nreversed:");
    for (const i64* value in std.iter::reversed(values)) {
        i64 shown = *value;
        std.string::string item = f" {shown}";
        std.string::append_str(&out, item.as_str());
    }
    std.string::append_str(&out, "\nchunks:");
    for (const i64[] chunk in std.iter::chunks(values, 3usize)) {
        i64 part = 0i64;
        for (usize index = 0usize; index < len(chunk); index += 1usize) { part += chunk[index]; }
        std.string::string item = f" {part}";
        std.string::append_str(&out, item.as_str());
    }
    std.string::append_str(&out, "\nsteps:");
    for (const i64[] pair in std.iter::windows(values, 2usize)) {
        i64 step = pair[1] - pair[0];
        std.string::string item = f" {step}";
        std.string::append_str(&out, item.as_str());
    }
    std.string::append_str(&out, "\n");
    return move out;
}
