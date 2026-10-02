module test.codegen.async_shared_owner_operations;

struct Item {
    i32 value;
};

async i32 main() {
    try {
        arc Item arc_owner = new arc Item { .value = 29 };
        weak arc Item arc_weak = std.arc::downgrade(&arc_owner);
        weak arc Item arc_weak_clone = std.arc::clone_weak(&arc_weak);
        o<arc Item> arc_upgraded = std.arc::upgrade(&arc_weak);
        if ((std.arc::strong_count(&arc_owner) != 2) ||
            (std.arc::weak_count(&arc_owner) != 2)) {
            throw TestAssertionFailed {.code = 1};
        }
        drop arc_upgraded;
        drop arc_weak_clone;
        drop arc_weak;

        rc Item rc_owner = new rc Item { .value = 31 };
        Item*? unique = std.rc::get_mut(&rc_owner);
        if (unique == null) {
            throw TestAssertionFailed {.code = 2};
        }
        unique->value = 37;
        rc Item rc_clone = std.rc::clone(&rc_owner);
        if ((std.rc::ptr_eq(&rc_owner, &rc_clone) == false) ||
            (std.rc::get_mut(&rc_owner) != null)) {
            throw TestAssertionFailed {.code = 3};
        }
        drop rc_clone;

        arc Item arc_raw_owner = new arc Item { .value = 41 };
        raw const Item* arc_token = std.arc::into_raw(move arc_raw_owner);
        unsafe {
            arc Item restored = std.arc::from_raw(arc_token);
            if (restored->value != 41) {
                throw TestAssertionFailed {.code = 4};
            }
        }

        rc Item rc_raw_owner = new rc Item { .value = 43 };
        raw const Item* rc_token = std.rc::into_raw(move rc_raw_owner);
        unsafe {
            rc Item restored = std.rc::from_raw(rc_token);
            if (restored->value != 43) {
                throw TestAssertionFailed {.code = 5};
            }
        }

        arc Item unique_arc = new arc Item { .value = 47 };
        std.arc::try_unwrap_result<Item> arc_outcome = std.arc::try_unwrap(move unique_arc);
        switch (move arc_outcome) {
            case variant std.arc::try_unwrap_result::unwrapped(move value):
                if (value.value != 47) {
                    throw TestAssertionFailed {.code = 6};
                }
                break;
            case variant std.arc::try_unwrap_result::shared(move returned):
                drop returned;
                throw TestAssertionFailed {.code = 7};
        }

        rc Item shared_rc = new rc Item { .value = 53 };
        rc Item shared_rc_alias = std.rc::clone(&shared_rc);
        std.rc::try_unwrap_result<Item> rc_outcome = std.rc::try_unwrap(move shared_rc);
        switch (move rc_outcome) {
            case variant std.rc::try_unwrap_result::unwrapped(move value):
                value as void;
                throw TestAssertionFailed {.code = 8};
            case variant std.rc::try_unwrap_result::shared(move returned):
                if ((std.rc::ptr_eq(&returned, &shared_rc_alias) == false) ||
                    (std.rc::strong_count(&returned) != 2)) {
                    throw TestAssertionFailed {.code = 9};
                }
                drop shared_rc_alias;
                if ((std.rc::strong_count(&returned) != 1) || (returned->value != 53)) {
                    throw TestAssertionFailed {.code = 10};
                }
                break;
        }
        i32 selected = rc_owner->value == 37 ? 0 : 11;
        return selected;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
