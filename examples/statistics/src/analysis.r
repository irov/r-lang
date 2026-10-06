module example.statistics.analysis;
import std.iter;

protected void append_optional(std.string::string* output, str label, o<usize> value) throws std.alloc::alloc_error {
    switch (value) {
    case variant o::some(index):
        usize found = *index;
        std.string::string row = f"{label}={found}\n";
        str row_text = row;
        output->append(row_text);
        break;
    case variant o::none:
        output->append(label);
        output->append("=none\n");
        break;
    }
}

protected void count_and_sum(const i32[] readings, out usize count, out i64 sum) {
    fn i64 add(i64 total, const i32* value) { return total + (*value as i64); }
    std.iter::slice_iter<i32> count_source = std.iter::of_slice(readings);
    count = std.iter::count(move count_source);
    auto sum_source = std.iter::of_slice(readings);
    sum = std.iter::fold(move sum_source, 0i64, &add);
}

std.string::string summary(const i32[] readings) throws std.alloc::alloc_error {
    fn bool negative(const i32* value) { return *value < 0; }
    fn bool positive(const i32* value) { return *value > 0; }
    fn o<i32> keep_negative(const i32* value) {
        if (*value < 0) { return o::some(*value); }
        return o::none;
    }
    usize count = 0usize;
    i64 sum = 0i64;
    count_and_sum(readings, out count, out sum);
    auto negative_source = std.iter::of_slice(readings);
    bool has_negative = std.iter::any(move negative_source, &negative);
    auto positive_source = std.iter::of_slice(readings);
    bool all_positive = std.iter::all(move positive_source, &positive);
    std.string::string output = f"count={count} sum={sum} any_negative={has_negative} all_positive={all_positive}\n";
    auto position_source = std.iter::of_slice(readings);
    o<usize> position = std.iter::position(move position_source, &negative);
    append_optional(&output, "negative_index", position);
    auto find_source = std.iter::of_slice(readings);
    o<i32> first_negative = std.iter::find_map(move find_source, &keep_negative);
    switch (first_negative) {
    case variant o::some(value):
        i32 found = *value;
        std.string::string row = f"first_negative={found}\n";
        str row_text = row;
        output.append(row_text);
        break;
    case variant o::none: output.append("first_negative=none\n"); break;
    }
    auto middle_source = std.iter::of_slice(readings);
    usize middle = count / 2usize;
    o<const i32*> middle_value = std.iter::nth(move middle_source, middle);
    switch (middle_value) {
    case variant o::some(value):
        i32 found = **value;
        std.string::string row = f"middle_input={found}\n";
        str row_text = row;
        output.append(row_text);
        break;
    case variant o::none: break;
    }
    auto last_source = std.iter::of_slice(readings);
    o<const i32*> final = std.iter::last(move last_source);
    switch (final) {
    case variant o::some(value):
        i32 found = **value;
        std.string::string row = f"last_input={found}\n";
        str row_text = row;
        output.append(row_text);
        break;
    case variant o::none: break;
    }
    return move output;
}

std.string::string page(const i32[] readings, usize offset, usize limit)
    throws std.alloc::alloc_error, std.array::push_error<i64>, std.list::push_error<i32> {
    fn i64 square(const i32* value) { i64 wide = *value as i64; return wide * wide; }
    auto source = std.iter::of_slice(readings);
    auto skipped = std.iter::skip(move source, offset);
    auto taken = std.iter::take(move skipped, limit);
    auto mapped = std.iter::map(move taken, &square);
    array<i64> squares = std.iter::collect_array(move mapped);
    std.string::string output = std.string::from_str("index square\n");
    const i64[] square_view = squares.as_slice();
    auto square_source = std.iter::of_slice(square_view);
    auto indexed = std.iter::enumerate(move square_source);
    for (std.iter::indexed<const i64*> row in &indexed) {
        usize index = offset + row.index;
        i64 squared = *row.value;
        std.string::string line = f"{index} {squared}\n";
        str row_text = line;
        output.append(row_text);
    }
    // A separate lazy filter produces a reusable collection of rejected readings.
    fn o<i32> rejected(const i32* value) {
        if (*value < 0) { return o::some(*value); }
        return o::none;
    }
    auto reject_source = std.iter::of_slice(readings);
    auto rejected_values = std.iter::filter_map(move reject_source, &rejected);
    list<i32> rejected_list = std.iter::collect_list(move rejected_values);
    output.append("rejected");
    for (const i32* value in &rejected_list) {
        i32 reading = *value;
        std.string::string cell = f" {reading}";
        str cell_text = cell;
        output.append(cell_text);
    }
    output.append("\n");
    return move output;
}

std.string::string schedule(i32 low, i32 high) throws std.alloc::alloc_error {
    // Two consecutive time windows share one iterator and monotonically increasing row IDs.
    i32 middle = low + (high - low) / 2;
    std.iter::range_i32 morning = std.iter::range(low, middle);
    std.iter::range_i32 afternoon = std.iter::range(middle, high);
    auto hours = std.iter::chain(move morning, move afternoon);
    usize count = (high - low) as usize;
    std.iter::range_usize numbers = std.iter::range_of_usize(1usize, count + 1usize);
    auto slots = std.iter::zip(move numbers, move hours);
    std.string::string output = std.string::from_str("slot hour\n");
    for (std.iter::pair<usize, i32> row in &slots) {
        std.string::string line = f"{row.left} {row.right}\n";
        str row_text = line;
        output.append(row_text);
    }
    return move output;
}
