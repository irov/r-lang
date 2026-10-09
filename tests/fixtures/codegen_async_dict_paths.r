module test.codegen.async_dict_paths;

/* B7: codegen_dict_paths.r in async bodies, where the tables, keys and staged values live in the
   frame across the awaits between the phases. */

async i32 later(i32 value) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000u32));
    return value;
}

protected async i32 integers() throws std.error::fault, std.dict::insert_error<i32, i32> {
    dict<i32, i32> table = std.dict::create::<i32, i32>();
    for (i32 key = 0; key < 300; key += 1) {
        o<i32> previous = std.dict::insert(&table, key * 37, key);
        previous as void;
    }
    if (len(table) != 300usize) { return 1; }
    i32 step = await later(0);
    if (step != 0) { return 17; }
    for (i32 key = 0; key < 300; key += 1) {
        i32 wanted = key * 37;
        switch (std.dict::get(&table, &wanted)) {
            case variant o::some(value): if (**value != key) { return 2; } break;
            case variant o::none: return 3;
        }
        i32 absent = key * 37 + 1;
        if (std.dict::contains(&table, &absent) == true) { return 4; }
    }
    for (i32 key = 0; key < 300; key += 2) {
        i32 gone = key * 37;
        o<i32> removed = std.dict::remove(&table, &gone);
        switch (removed) {
            case variant o::some(value): if (*value != key) { return 5; } break;
            case variant o::none: return 6;
        }
    }
    if (len(table) != 150usize) { return 7; }
    i32 removed_step = await later(0);
    if (removed_step != 0) { return 18; }
    for (i32 key = 0; key < 300; key += 2) {
        i32 gone = key * 37;
        if (std.dict::contains(&table, &gone) == true) { return 8; }
        i32 kept = key * 37 + 37;
        if (std.dict::contains(&table, &kept) == false) { return 9; }
    }
    // The removed keys come back into tombstones of the same table.
    for (i32 key = 0; key < 300; key += 4) {
        o<i32> previous = std.dict::insert(&table, key * 37, key + 1000);
        switch (previous) {
            case variant o::some(value): return 10;
            case variant o::none: break;
        }
    }
    if (len(table) != 225usize) { return 11; }
    o<i32> replaced = std.dict::insert(&table, 37, 7);
    switch (replaced) {
        case variant o::some(value): if (*value != 1) { return 12; } break;
        case variant o::none: return 13;
    }
    i32 one = 37;
    {
        o<i32*> found = std.dict::get_mut(&table, &one);
        switch (move found) {
            case variant o::some(value): **value += 1; break;
            case variant o::none: return 14;
        }
    }
    // The runtime iterates what the program inserted.
    i64 sum = 0i64;
    usize count = 0usize;
    std.dict::iter<i32, i32> cursor = std.dict::iter(&table);
    while (true) {
        o<std.dict::entry_ref<i32, i32>> next = std.dict::next(&cursor);
        switch (next) {
            case variant o::some(entry):
                sum += (*entry->key as i64) * 3i64 + (*entry->value as i64);
                count += 1usize;
                break;
            case variant o::none:
                if (count != 225usize) { return 15; }
                // Keys 37 * k for odd k (value k, 8 for k = 1) and for k = 4 * m (k + 1000).
                if (sum != 3838207i64) { return 16; }
                return 0;
        }
    }
}

protected std.string::string text(str value) throws std.alloc::alloc_error {
    return std.string::from_str(value);
}

protected bool same(str left, str right) { return core::key_equal(&left, &right); }

protected async i32 owned() throws std.error::fault,
    std.dict::insert_error<std.string::string, std.string::string> {
    dict<std.string::string, std.string::string> names = std.dict::create();
    for (u32 index = 0u32; index < 40u32; index += 1u32) {
        o<std.string::string> previous =
            std.dict::insert(&names, f"key-{index}", f"value-{index}");
        switch (move previous) {
            case variant o::some(move value): drop value; return 21;
            case variant o::none: break;
        }
    }
    if (len(names) != 40usize) { return 34; }
    i32 step = await later(0);
    if (step != 0) { return 32; }
    // A replacement drops the staged key and hands the old value back.
    o<std.string::string> replaced = std.dict::insert(&names, text("key-7"), text("seven"));
    switch (move replaced) {
        case variant o::some(move value):
            if (same(value, "value-7") == false) { return 22; }
            drop value;
            break;
        case variant o::none: return 23;
    }
    std.string::string seven = text("key-7");
    switch (std.dict::get(&names, &seven)) {
        case variant o::some(value):
            if (same(**value, "seven") == false) { return 24; }
            break;
        case variant o::none: return 25;
    }
    std.string::string gone = text("key-3");
    o<std.string::string> removed = std.dict::remove(&names, &gone);
    switch (move removed) {
        case variant o::some(move value): drop value; break;
        case variant o::none: return 26;
    }
    o<std.string::string> back = std.dict::insert(&names, text("key-3"), text("three"));
    switch (move back) {
        case variant o::some(move value): drop value; return 27;
        case variant o::none: break;
    }
    switch (std.dict::get(&names, &gone)) {
        case variant o::some(value):
            if (same(**value, "three") == false) { return 28; }
            break;
        case variant o::none: return 29;
    }
    i32 last_step = await later(0);
    if (last_step != 0) { return 33; }
    std.string::string absent = text("key-40");
    if (std.dict::contains(&names, &absent) == true) { return 30; }
    if (len(names) != 40usize) { return 31; }
    return 0;
}

async i32 main() {
    try {
        i32 first = await integers();
        if (first != 0) { return first; }
        return await owned();
    } catch (std.error::fault failure) {
        return 89;
    } catch (std.dict::insert_error<i32, i32> failure) {
        return 91;
    } catch (std.dict::insert_error<std.string::string, std.string::string> failure) {
        return 92;
    }
}
