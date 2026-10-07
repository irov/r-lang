module test.codegen.string_place_views;

/* R-EXPR-0015 (L41): a place of type std.string::string where str is expected is the view
   place; the place is borrowed, not moved, and stays usable. */

struct Record {
    std.string::string name;
    array<std.string::string> tags;
};

struct Label {
    str text;
};

usize measure(str text) { return len(text); }

bool same(str left, str right) {
    const u8[] left_bytes = left;
    const u8[] right_bytes = right;
    return std.bytes::equal(left_bytes, right_bytes);
}

/* A view of a parameter's pointee, returned through the implicit conversion. */
str name_of(const Record* record) { return record->name; }

usize sum_tags(const Record* record) {
    usize total = 0usize;
    for (usize index = 0usize; index < len(record->tags); index += 1usize) {
        total += measure(record->tags[index]);
    }
    return total;
}

void add(array<std.string::string>* target, std.string::string item) throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<std.string::string> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

i32 run() throws std.alloc::alloc_error {
    std.string::string word = std.string::from_str("view");
    if (measure(word) != 4usize) { return 1; }
    /* The place stays usable: append after the view's last use. */
    word.append("s");
    if (measure(word) != 5usize) { return 2; }

    str local = word;
    if (same(local, "views") == false) { return 3; }

    Record record = {.name = std.string::from_str("record"), .tags = []};
    add(&record.tags, std.string::from_str("ab"));
    add(&record.tags, std.string::from_str("cde"));
    if (measure(record.name) != 6usize) { return 4; }
    if (sum_tags(&record) != 5usize) { return 5; }
    if (same(name_of(&record), "record") == false) { return 6; }

    const Record* pointer = &record;
    if (measure(pointer->name) != 6usize) { return 7; }
    const std.string::string* text = &record.name;
    if (measure(*text) != 6usize) { return 8; }

    Label label = {.text = word};
    if (len(label.text) != 5usize) { return 9; }

    std.string::string joined = std.string::create();
    joined.append(word);
    joined.append(record.name);
    if (same(joined, "viewsrecord") == false) { return 10; }

    /* A std.format builder appends a string through its str overload too. */
    std.format::builder builder = std.format::create();
    builder.append(word);
    builder.append(record.name);
    str built = builder;
    if (same(built, "viewsrecord") == false) { return 11; }
    return 0;
}

i32 main() {
    try {
        return run();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 99;
}
