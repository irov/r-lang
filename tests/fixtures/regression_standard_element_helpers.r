module test.regression.standard_element_helpers;

/* M32-6: arrays of two standard types without an ordinal, std.string::string and
   std.json::value, each push through a helper of their own type; a shared helper moved a JSON
   value as a string. */
protected void fill(array<std.string::string>* names, array<std.json::value>* values)
    throws std.json::error, std.alloc::alloc_error {
    try {
        names->push(std.string::from_str("first name"));
        names->push(std.string::from_str("second name"));
    } catch (std.array::push_error<std.string::string> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
    try {
        values->push(std.json::from_string("one"));
        values->push(std.json::from_string("two"));
        values->push(std.json::from_string("three"));
    } catch (std.array::push_error<std.json::value> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

i32 main() {
    array<std.string::string> names = std.array::create::<std.string::string>();
    array<std.json::value> values = std.array::create::<std.json::value>();
    try {
        fill(&names, &values);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    u32 checks = 0u32;
    if (len(names) == 2usize && len(values) == 3usize) { checks += 1u32; }
    if (std.bytes::equal(names[1usize].as_bytes(), "second name") == true) { checks += 1u32; }
    if (std.bytes::equal(std.json::text(&values[2usize]), "three") == true) { checks += 1u32; }
    if (std.bytes::equal(std.json::text(&values[0usize]), "one") == true) { checks += 1u32; }
    if (checks == 4u32) { return 0; }
    return 1;
}
