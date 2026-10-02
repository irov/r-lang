module test.codegen.shared_owner_operations;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


struct Item {
    i32 value;
};

i32 check_arc() {
    arc Item owner = new arc Item { .value = 7 };
    Item*? unique = std.arc::get_mut(&owner);
    if (unique == null) {
        return 1;
    }
    unique->value = 11;

    weak arc Item weak_owner = std.arc::downgrade(&owner);
    weak arc Item weak_clone = std.arc::clone_weak(&weak_owner);
    test_observe(&weak_clone);
    arc Item owner_clone = std.arc::clone(&owner);
    test_observe(&owner_clone);
    o<arc Item> upgraded = std.arc::upgrade(&weak_owner);
    test_observe(&upgraded);
    if ((std.arc::strong_count(&owner) != 3) ||
        (std.arc::weak_count(&owner) != 2) ||
        (std.arc::ptr_eq(&owner, &owner_clone) == false) ||
        (std.arc::get_mut(&owner) != null)) {
        return 2;
    }
    drop upgraded;
    drop weak_clone;
    drop weak_owner;
    drop owner_clone;
    if ((std.arc::strong_count(&owner) != 1) || (std.arc::weak_count(&owner) != 0)) {
        return 3;
    }

    arc Item raw_owner = new arc Item { .value = 13 };
    raw const Item* token = std.arc::into_raw(move raw_owner);
    unsafe {
        arc Item restored = std.arc::from_raw(token);
        if (restored->value != 13) {
            return 4;
        }
    }
    i32 selected = owner->value == 11 ? 0 : 5;
    return selected;
}

i32 check_rc() {
    rc Item owner = new rc Item { .value = 17 };
    Item*? unique = std.rc::get_mut(&owner);
    if (unique == null) {
        return 1;
    }
    unique->value = 19;

    weak rc Item weak_owner = std.rc::downgrade(&owner);
    weak rc Item weak_clone = std.rc::clone_weak(&weak_owner);
    test_observe(&weak_clone);
    rc Item owner_clone = std.rc::clone(&owner);
    test_observe(&owner_clone);
    o<rc Item> upgraded = std.rc::upgrade(&weak_owner);
    test_observe(&upgraded);
    if ((std.rc::strong_count(&owner) != 3) ||
        (std.rc::weak_count(&owner) != 2) ||
        (std.rc::ptr_eq(&owner, &owner_clone) == false) ||
        (std.rc::get_mut(&owner) != null)) {
        return 2;
    }
    drop upgraded;
    drop weak_clone;
    drop weak_owner;
    drop owner_clone;
    if ((std.rc::strong_count(&owner) != 1) || (std.rc::weak_count(&owner) != 0)) {
        return 3;
    }

    rc Item raw_owner = new rc Item { .value = 23 };
    raw const Item* token = std.rc::into_raw(move raw_owner);
    unsafe {
        rc Item restored = std.rc::from_raw(token);
        if (restored->value != 23) {
            return 4;
        }
    }
    i32 selected = owner->value == 19 ? 0 : 5;
    return selected;
}

i32 check_arc_unwrap() {
    arc Item owner = new arc Item { .value = 47 };
    std.arc::try_unwrap_result<Item> outcome = std.arc::try_unwrap(move owner);
    switch (move outcome) {
        case variant std.arc::try_unwrap_result::unwrapped(move value):
            i32 selected = value.value == 47 ? 0 : 1;
            return selected;
        case variant std.arc::try_unwrap_result::shared(move returned):
            drop returned;
            return 2;
    }
}

i32 check_arc_shared_unwrap() {
    arc Item owner = new arc Item { .value = 53 };
    arc Item alias = std.arc::clone(&owner);
    test_observe(&alias);
    std.arc::try_unwrap_result<Item> outcome = std.arc::try_unwrap(move owner);
    switch (move outcome) {
        case variant std.arc::try_unwrap_result::unwrapped(move value):
            value as void;
            return 1;
        case variant std.arc::try_unwrap_result::shared(move returned):
            if ((std.arc::ptr_eq(&returned, &alias) == false) ||
                (std.arc::strong_count(&returned) != 2)) {
                return 2;
            }
            drop alias;
            i32 selected = (std.arc::strong_count(&returned) == 1) && (returned->value == 53) ? 0 : 3;
            return selected;
    }
}

i32 check_rc_unwrap() {
    rc Item owner = new rc Item { .value = 59 };
    std.rc::try_unwrap_result<Item> outcome = std.rc::try_unwrap(move owner);
    switch (move outcome) {
        case variant std.rc::try_unwrap_result::unwrapped(move value):
            i32 selected = value.value == 59 ? 0 : 1;
            return selected;
        case variant std.rc::try_unwrap_result::shared(move returned):
            drop returned;
            return 2;
    }
}

i32 check_rc_shared_unwrap() {
    rc Item owner = new rc Item { .value = 61 };
    rc Item alias = std.rc::clone(&owner);
    test_observe(&alias);
    std.rc::try_unwrap_result<Item> outcome = std.rc::try_unwrap(move owner);
    switch (move outcome) {
        case variant std.rc::try_unwrap_result::unwrapped(move value):
            value as void;
            return 1;
        case variant std.rc::try_unwrap_result::shared(move returned):
            if ((std.rc::ptr_eq(&returned, &alias) == false) ||
                (std.rc::strong_count(&returned) != 2)) {
                return 2;
            }
            drop alias;
            i32 selected = (std.rc::strong_count(&returned) == 1) && (returned->value == 61) ? 0 : 3;
            return selected;
    }
}

i32 main() {
    i32 arc_status = check_arc();
    if (arc_status != 0) {
        return arc_status;
    }
    i32 rc_status = check_rc();
    if (rc_status != 0) {
        return rc_status + 10;
    }
    i32 arc_unwrap_status = check_arc_unwrap();
    if (arc_unwrap_status != 0) {
        return arc_unwrap_status + 20;
    }
    i32 arc_shared_status = check_arc_shared_unwrap();
    if (arc_shared_status != 0) {
        return arc_shared_status + 30;
    }
    i32 rc_unwrap_status = check_rc_unwrap();
    if (rc_unwrap_status != 0) {
        return rc_unwrap_status + 40;
    }
    i32 rc_shared_status = check_rc_shared_unwrap();
    i32 selected = rc_shared_status == 0 ? 0 : rc_shared_status + 50;
    return selected;
}
