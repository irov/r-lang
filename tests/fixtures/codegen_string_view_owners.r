module test.codegen.string_view_owners;

/* R-EXPR-0015, R-STMT-0006, R-EXPR-0031 (L43): a place of std.string::string or
   std.format::builder is its str view wherever str or const u8[] is expected, and a switch or a
   match selects by that view; the place is borrowed, not moved, and stays usable. */

struct Record {
    std.string::string name;
    array<std.string::string> tags;
};

struct Packet {
    const u8[] data;
};

usize measure(str text) { return len(text); }

usize count_bytes(const u8[] data) { return len(data); }

/* A byte view of a parameter's pointee, returned through the implicit conversion. */
const u8[] name_bytes(const Record* record) { return record->name; }

u32 kind_of(const std.string::string* word) {
    switch (*word) {
    case "alpha": return 1u32;
    case "beta": return 2u32;
    default: return 0u32;
    }
}

u32 built_kind(const std.format::builder* built) {
    switch (*built) {
    case "alpha beta": return 3u32;
    default: return 0u32;
    }
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
    std.string::string word = std.string::from_str("bytes");
    if (count_bytes(word) != 5usize) { return 1; }
    const u8[] viewed = word;
    if (len(viewed) != 5usize) { return 2; }
    if (std.bytes::equal(word, "bytes") == false) { return 3; }
    if (std.hash::crc32(word) != std.hash::crc32("bytes")) { return 4; }
    /* The place stays usable: append after the view's last use. */
    word.append("!");
    if (count_bytes(word) != 6usize) { return 5; }

    std.format::builder builder = std.format::create();
    builder.append("alpha");
    builder.append(' ');
    builder.append("beta");
    if (measure(builder) != 10usize) { return 6; }
    str text = builder;
    if (len(text) != 10usize) { return 7; }
    if (std.bytes::equal(builder, "alpha beta") == false) { return 8; }
    std.string::string copied = std.string::from_str(builder);
    if (std.bytes::equal(copied, "alpha beta") == false) { return 9; }

    /* A builder or string appends another builder through its view. */
    std.format::builder other = std.format::create();
    other.append(builder);
    other.append(word);
    copied.append(builder);
    if (std.bytes::equal(other, "alpha betabytes!") == false) { return 10; }
    if (count_bytes(copied) != 20usize) { return 11; }

    /* A byte array appends a string through the byte-slice overload. */
    bytes packet = std.alloc::bytes(1usize, 1u8);
    packet.append(word);
    if (len(packet) != 7usize || packet[1] != 98u8) { return 12; }

    std.string::string alpha = std.string::from_str("alpha");
    if (kind_of(&alpha) != 1u32) { return 13; }
    if (built_kind(&builder) != 3u32) { return 14; }
    switch (word) {
    case "bytes!": break;
    default: return 15;
    }
    switch (builder) {
    case "alpha": return 16;
    case "alpha beta": break;
    default: return 17;
    }

    Record record = {.name = std.string::from_str("record"), .tags = []};
    add(&record.tags, std.string::from_str("ab"));
    if (count_bytes(record.name) != 6usize) { return 18; }
    if (count_bytes(record.tags[0]) != 2usize) { return 19; }
    const Record* pointer = &record;
    if (std.bytes::equal(pointer->name, "record") == false) { return 20; }
    if (std.bytes::equal(name_bytes(&record), "record") == false) { return 21; }

    Packet framed = {.data = word};
    if (len(framed.data) != 6usize) { return 22; }

    /* A match takes the str view of a string or builder place (R-EXPR-0031). */
    i32 matched = match (word) {
        case "bytes!": 1;
        default: 0;
    };
    i32 built_matched = match (builder) {
        case "alpha": 0;
        case "alpha beta": 2;
        default: 0;
    };
    if (matched + built_matched != 3) { return 25; }

    /* The explicit forms stay available and give the same views. */
    str explicit_text = word.as_str();
    const u8[] explicit_bytes = word.as_bytes();
    if (std.bytes::equal(explicit_text, explicit_bytes) == false) { return 23; }
    str explicit_built = builder.as_str();
    if (std.bytes::equal(explicit_built, builder) == false) { return 24; }
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
