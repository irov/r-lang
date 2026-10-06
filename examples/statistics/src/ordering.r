module example.statistics.ordering;
import std.slice;
import std.cmp;

protected void append_value(std.string::string* output, str label, i32 found)
    throws std.alloc::alloc_error {
    std.string::string row = f"{label}={found}\n";
    str row_text = row;
    output->append(row_text);
}

std.string::string describe(const i32[] readings, i32 wanted) throws std.alloc::alloc_error {
    std.string::string output = std.string::create();
    o<const i32*> first = std.slice::first(readings);
    o<const i32*> last = std.slice::last(readings);
    o<const i32*> minimum = std.slice::min_of(readings);
    o<const i32*> maximum = std.slice::max_of(readings);
    switch (first) {
    case variant o::some(value): append_value(&output, "first", **value); break;
    case variant o::none: output.append("first=none\n"); break;
    }
    switch (last) {
    case variant o::some(value): append_value(&output, "last", **value); break;
    case variant o::none: output.append("last=none\n"); break;
    }
    switch (minimum) {
    case variant o::some(value): append_value(&output, "minimum", **value); break;
    case variant o::none: output.append("minimum=none\n"); break;
    }
    switch (maximum) {
    case variant o::some(value): append_value(&output, "maximum", **value); break;
    case variant o::none: output.append("maximum=none\n"); break;
    }
    bool contains = std.slice::contains(readings, &wanted);
    bool ordered = std.slice::is_sorted(readings);
    std.string::string row = f"contains={contains} sorted={ordered}\n";
    str row_text = row;
    output.append(row_text);
    o<usize> found = std.slice::index_of(readings, &wanted);
    switch (found) {
    case variant o::some(index):
        usize position = *index;
        std.string::string line = f"index={position}\n";
        str line_text = line;
        output.append(line_text); break;
    case variant o::none: output.append("index=none\n"); break;
    }
    if (ordered == true) {
        o<usize> binary = std.slice::binary_search(readings, &wanted);
        switch (binary) {
        case variant o::some(index):
            usize position = *index;
            std.string::string line = f"binary_index={position}\n";
            str line_text = line;
            output.append(line_text); break;
        case variant o::none: output.append("binary_index=none\n"); break;
        }
    }
    return move output;
}

std.string::string sorted(array<i32>* readings, bool descending, i32 wanted) throws std.alloc::alloc_error {
    {
        i32[] values = readings->as_slice_mut();
        std.slice::sort(values);
        if (descending == true) { std.slice::reverse(values); }
    }
    const i32[] view = readings->as_slice();
    std.string::string output = describe(view, wanted);
    output.append("values");
    for (const i32* value in &view) {
        i32 reading = *value;
        std.string::string cell = f" {reading}";
        str cell_text = cell;
        output.append(cell_text);
    }
    output.append("\n");
    return move output;
}
