module codegen.async_container_operations;

protected async i32 exercise_arrays()
    throws std.alloc::alloc_error, std.array::push_error<i32> {
    array<i32> values = std.array::with_capacity::<i32>(1);
    std.array::reserve(&values, 2);
    std.array::push(&values, 10);
    std.array::push(&values, 20);
    if (std.array::capacity(&values) < 2) { return 1; }
    {
        o<const i32*> item = std.array::get(&values, 0);
        switch (item) {
            case variant o::some(value): if (**value != 10) { return 2; } break;
            case variant o::none: return 3;
        }
    }
    {
        o<i32*> item = std.array::get_mut(&values, 1);
        switch (move item) {
            case variant o::some(value): **value = 21; break;
            case variant o::none: return 4;
        }
    }
    o<i32> removed = std.array::remove(&values, 0);
    switch (removed) {
        case variant o::some(value): if (*value != 10) { return 5; } break;
        case variant o::none: return 6;
    }
    o<i32> popped = std.array::pop(&values);
    switch (popped) {
        case variant o::some(value): if (*value != 21) { return 7; } break;
        case variant o::none: return 8;
    }
    std.array::clear(&values);
    return 0;
}

protected async i32 exercise_lists() throws std.list::push_error<i32> {
    list<i32> values = std.list::create::<i32>();
    i32* first = std.list::push_back(&values, 10);
    i32 removed = std.list::remove(&values, move first);
    if (removed != 10) { return 11; }
    {
        i32* inserted = std.list::push_front(&values, 20);
        if (*inserted != 20) { return 41; }
    }
    {
        i32* last = std.list::push_back(&values, 30);
        {
            i32* inserted = std.list::insert_before(&values, last, 25);
            if (*inserted != 25) { return 42; }
        }
        {
            i32* inserted = std.list::insert_after(&values, last, 35);
            if (*inserted != 35) { return 43; }
        }
    }
    {
        o<const i32*> front = std.list::front(&values);
        switch (front) {
            case variant o::some(value): if (**value != 20) { return 12; } break;
            case variant o::none: return 13;
        }
    }
    {
        o<const i32*> back = std.list::back(&values);
        switch (back) {
            case variant o::some(value): if (**value != 35) { return 14; } break;
            case variant o::none: return 15;
        }
    }
    {
        o<i32*> front = std.list::front_mut(&values);
        switch (move front) {
            case variant o::some(value): **value = 21; break;
            case variant o::none: return 16;
        }
    }
    {
        o<i32*> back = std.list::back_mut(&values);
        switch (move back) {
            case variant o::some(value): **value = 36; break;
            case variant o::none: return 17;
        }
    }
    {
        o<const i32*> item = std.list::get(&values, 1);
        switch (item) {
            case variant o::some(value): if (**value != 25) { return 18; } break;
            case variant o::none: return 19;
        }
    }
    {
        o<i32*> item = std.list::get_mut(&values, 2);
        switch (move item) {
            case variant o::some(value): **value = 31; break;
            case variant o::none: return 20;
        }
    }
    {
        std.list::iter<i32> iterator = std.list::iter(&values);
        o<const i32*> next = std.list::next(&iterator);
        switch (next) {
            case variant o::some(value): if (**value != 21) { return 21; } break;
            case variant o::none: return 22;
        }
    }
    o<i32> front_value = std.list::pop_front(&values);
    switch (front_value) {
        case variant o::some(value): if (*value != 21) { return 23; } break;
        case variant o::none: return 24;
    }
    o<i32> back_value = std.list::pop_back(&values);
    switch (back_value) {
        case variant o::some(value): if (*value != 36) { return 25; } break;
        case variant o::none: return 26;
    }
    std.list::clear(&values);
    return 0;
}

protected async i32 exercise_dicts()
    throws std.alloc::alloc_error, std.dict::insert_error<i32, i32> {
    dict<i32, i32> values = std.dict::with_capacity::<i32, i32>(1);
    std.dict::reserve(&values, 2);
    o<i32> previous = std.dict::insert(&values, 1, 10);
    switch (previous) {
        case variant o::some(value): return 31;
        case variant o::none: break;
    }
    o<i32> previous_2 = std.dict::insert(&values, 1, 11);
    switch (previous_2) {
        case variant o::some(value): if (*value != 10) { return 32; } break;
        case variant o::none: return 33;
    }
    i32 key = 1;
    if (std.dict::contains(&values, &key) == false) { return 34; }
    {
        o<const i32*> found = std.dict::get(&values, &key);
        switch (found) {
            case variant o::some(value): if (**value != 11) { return 35; } break;
            case variant o::none: return 36;
        }
    }
    {
        o<i32*> found = std.dict::get_mut(&values, &key);
        switch (move found) {
            case variant o::some(value): **value = 12; break;
            case variant o::none: return 37;
        }
    }
    {
        std.dict::iter<i32, i32> iterator = std.dict::iter(&values);
        o<std.dict::entry_ref<i32, i32>> entry = std.dict::next(&iterator);
        switch (entry) {
            case variant o::some(value): break;
            case variant o::none: return 38;
        }
    }
    o<i32> removed = std.dict::remove(&values, &key);
    switch (removed) {
        case variant o::some(value): if (*value != 12) { return 39; } break;
        case variant o::none: return 40;
    }
    std.dict::clear(&values);
    return 0;
}

async i32 main() {
    try {
        try {
            i32 arrays = await exercise_arrays();
            if (arrays != 0) { return arrays; }
            i32 lists = await exercise_lists();
            if (lists != 0) { return lists; }
            i32 dicts = await exercise_dicts();
            return dicts;
        } catch (std.async::start_error failure) {
            throw TestAssertionFailed {.code = 94};
        } catch (std.alloc::alloc_error failure) {
            throw TestAssertionFailed {.code = 90};
        } catch (std.array::push_error<i32> failure) {
            throw TestAssertionFailed {.code = 91};
        } catch (std.list::push_error<i32> failure) {
            throw TestAssertionFailed {.code = 92};
        } catch (std.dict::insert_error<i32, i32> failure) {
            throw TestAssertionFailed {.code = 93};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
