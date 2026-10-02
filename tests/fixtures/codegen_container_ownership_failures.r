module test.codegen.container_ownership_failures;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


protected i32 check_alloc_failure() {
    try {
        own i32* value = new i32(11);
        own (own i32*)* stored = std.alloc::try_new(move value);
        test_observe(&stored);
        return 1;
    } catch (std.alloc::new_error<own i32*> failure) {
        if (*(failure.value) != 11) { return 2; }
    }
    return 0;
}

protected i32 check_list_failure() {
    list<own i32*> values = std.list::create::<own i32*>();
    try {
        own i32* value = new i32(22);
        (own i32*)* inserted = std.list::push_back(&values, move value);
        test_observe(&inserted);
        return 3;
    } catch (std.list::push_error<own i32*> failure) {
        switch (move failure) {
            case variant std.list::push_error::allocation_failed(move payload):
                if (*(payload.value) != 22) { return 4; }
                break;
        }
    }
    return 0;
}

protected i32 check_dict_failure() {
    dict<i32, own i32*> values = std.dict::create::<i32, own i32*>();
    try {
        own i32* value = new i32(33);
        o<own i32*> previous = std.dict::insert(&values, 7, move value);
        test_observe(&previous);
        return 6;
    } catch (std.dict::insert_error<i32, own i32*> failure) {
        switch (move failure) {
            case variant std.dict::insert_error::allocation_failed(move payload):
                if ((payload.key != 7) || (*(payload.value) != 33)) { return 7; }
                break;
        }
    }
    return 0;
}

i32 main() {
    i32 status = check_alloc_failure();
    if (status != 0) { return status; }
    i32 status_2 = check_list_failure();
    if (status_2 != 0) { return status_2; }
    i32 status_3 = check_dict_failure();
    return status_3;
}
