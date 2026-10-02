module test.regression.async_variant_call_argument;

/* M32-2: an async function passes variants built in the step, o::none and o::some(move text),
   and the value of a call that may throw as Move arguments to synchronous calls, also inside a
   nested call whose first argument is a borrowed parameter. */
struct note { u64 number; usize length; bool marked; };

protected note describe(const u64* number, str text, o<std.string::string> label, o<std.json::value> data) {
    usize length = 0usize;
    switch (move label) {
    case variant o::some(move value): length = std.string::len(&value);
    case variant o::none: break;
    }
    bool marked = false;
    switch (move data) {
    case variant o::some(move value):
        drop value;
        marked = true;
    case variant o::none: break;
    }
    const u8[] bytes = text;
    return note {.number = *number, .length = length + len(bytes), .marked = marked};
}

protected void keep(array<note>* notes, note item) throws std.alloc::alloc_error {
    try {
        notes->push(item);
    } catch (std.array::push_error<note> rejected) {
        rejected as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* A note of the length of a JSON string: the Move argument is the value of a call that may
   throw. */
protected note from_value(std.json::value value) {
    const u8[] bytes = std.json::text(&value);
    usize length = len(bytes);
    drop value;
    return note {.number = 0u64, .length = length, .marked = false};
}

protected async u32 later(u32 value) { return value + 1u32; }

@scoped
protected async usize collect(const u64* number) throws std.error::fault {
    array<note> notes = std.array::create::<note>();
    u32 step = 0u32;
    task_scope(1) io { step += await later(1u32); }
    try {
        keep(&notes, describe(number, "ab", o::none, o::none));
        std.string::string label = std.string::from_str("four");
        keep(&notes, describe(number, "c", o::some(move label), o::none));
        keep(&notes, describe(number, "", o::none, o::some(std.json::from_bool(true))));
        keep(&notes, from_value(std.json::from_string("tail")));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    task_scope(1) io { step += await later(step); }
    usize total = (step as usize) * 1000usize;
    for (usize index = 0usize; index < len(notes); index += 1usize) {
        total += notes[index].length;
        if (notes[index].marked == true) { total += 100usize; }
    }
    return total;
}

async i32 main() {
    u64 number = 7u64;
    usize total = 0usize;
    task_scope(1) io { total += await collect(&number); }
    if (total == 5111usize) { return 0; }
    return 1;
}
