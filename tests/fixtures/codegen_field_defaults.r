module test.codegen.field_defaults;

/* R-INIT-0004, R-INIT-0005, R-OWN-0019 (L33): field initializers and default values. Omitted
   fields take their initializers after the explicit field expressions, in declaration order,
   anew for every initialization; enums with a @default variant, structs of defaults and fixed
   arrays of them have default values, which `Type {}`, omitted array elements and core::take
   use; generic structs, record variants and initializers that allocate. */

protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = std.string::as_bytes(source);
    usize actual_length = len(actual);
    usize expected_length = len(expected);
    if (actual_length != expected_length) { return false; }
    usize index = 0;
    while (index < actual_length) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

thread_local u32 ticket = 0u32;

u32 next_ticket() {
    ticket += 1u32;
    return ticket;
}

enum Level { low, @default medium, high };

enum Mode { off, @default on(u32), manual { u32 step = 5; bool strict; } };

struct Retry {
    u32 attempts = 3;
    u32 first = next_ticket();
    u32 second = next_ticket();
    Level level;
    o<u32> limit;
};

struct Named {
    std.string::string name = std.string::from_str("anonymous");
    u32 order = next_ticket();
};

@generic<T>
struct Slot {
    o<T> value = o::none;
    u32 tag = 7;
};

struct Outer {
    Retry retry;
    Level[2] levels;
    u32 count;
};

i32 order_of_evaluation() {
    ticket = 0u32;
    Retry a = Retry {};
    if ((a.attempts != 3u32) || (a.first != 1u32) || (a.second != 2u32)) { return 1; }
    if (a.level != Level::medium) { return 2; }
    Retry b = Retry {.second = next_ticket(), .attempts = 5};
    if ((b.second != 3u32) || (b.first != 4u32) || (b.attempts != 5u32)) { return 3; }
    Outer outer = Outer {};
    if ((outer.retry.first != 5u32) || (outer.retry.second != 6u32) || (outer.count != 0u32)) {
        return 4;
    }
    if ((outer.levels[0] != Level::medium) || (outer.levels[1] != Level::medium)) { return 5; }
    Retry[2] pair = {Retry {.attempts = 1}};
    if ((pair[0].first != 7u32) || (pair[1].first != 9u32) || (pair[1].attempts != 3u32)) {
        return 6;
    }
    return 0;
}

i32 enums_and_generics() {
    Mode mode = Mode::off;
    Mode taken = core::take(&mode);
    switch (taken) {
    case variant Mode::off: {}
    default: return 11;
    }
    switch (mode) {
    case variant Mode::on(value): { if (*value != 0u32) { return 12; } }
    default: return 13;
    }
    Mode manual = Mode::manual {.strict = true};
    switch (manual) {
    case variant Mode::manual(fields): { if ((fields->step != 5u32) || (fields->strict == false)) { return 14; } }
    default: return 15;
    }
    Slot<i64> slot = Slot<i64> {};
    if (slot.tag != 7u32) { return 16; }
    switch (slot.value) {
    case variant o::none: {}
    default: return 17;
    }
    Slot<Level> levels = Slot<Level> {.tag = 1};
    if (levels.tag != 1u32) { return 18; }
    return 0;
}

i32 allocating_initializers() throws std.alloc::alloc_error {
    ticket = 0u32;
    Named first = Named {};
    if ((matches(&first.name, "anonymous") == false) || (first.order != 1u32)) { return 21; }
    Named second = Named {.name = std.string::from_str("given")};
    if ((matches(&second.name, "given") == false) || (second.order != 2u32)) { return 22; }
    Named previous = core::take(&second);
    if ((matches(&previous.name, "given") == false) || (previous.order != 2u32)) { return 23; }
    if ((matches(&second.name, "anonymous") == false) || (second.order != 3u32)) { return 24; }
    return 0;
}

i32 main() {
    i32 order = order_of_evaluation();
    if (order != 0) { return order; }
    i32 kinds = enums_and_generics();
    if (kinds != 0) { return kinds; }
    return allocating_initializers();
}
