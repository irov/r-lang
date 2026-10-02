module regression.nullable_pointer_flow;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { bool value; };

struct PointerDefaults {
    const i32*? borrow;
    own i32*? owner;
    raw i32*? raw_pointer;
};

error PointerError {
    i32 observed;
};

const i32*? choose(const i32*? pointer, bool keep) {
    if (keep == true) {
        return pointer;
    }
    return null;
}

const i32*? missing() {
    return null;
}

i32 after_while(const i32*? pointer) {
    while (pointer == null) {
        return 0;
    }
    return *pointer;
}

i32 after_for(const i32*? pointer) {
    for (; pointer == null;) {
        return 0;
    }
    return *pointer;
}

protected i32 require_pointer(const i32*? pointer) throws PointerError {
    throw (pointer == null) PointerError {
        .observed = 0,
    };
    return *pointer;
}

protected void reject_pointer(const i32*? pointer) throws PointerError {
    throw (pointer != null) PointerError {
        .observed = *pointer,
    };
}

i32 main() {
    try {
        i32 source = 7;
        const i32*? present = &source;
        const i32*? absent = null;
        PointerDefaults defaults = {};
        i32 result = 0;
        TestStorage1 storage_raw_default = {.value = false};

        unsafe {
            storage_raw_default.value = defaults.raw_pointer == null;
        }
        if (defaults.borrow == null && defaults.owner == null && storage_raw_default.value == true) {
            result += 1;
        }
        if (present != null && *present == 7) {
            result += 2;
        }
        if (absent == null || *absent == 0) {
            result += 4;
        }

        i32 selected = present != null ? *present : 0;
        result += selected;

        while (present != null) {
            result += *present;
            present = null;
        }
        const i32*? present_2 = &source;
        for (; present_2 != null; present_2 = null) {
            result += *present_2;
        }

        const i32*? returned = choose(&source, true);
        if (returned != null) {
            result += *returned;
        }
        const i32*? none = choose(&source, false);
        if (none == null) {
            result += 8;
        }
        const i32*? always_none = missing();
        if (always_none == null) {
            result += 64;
        }
        result += after_while(&source);
        result += after_for(&source);

        own i32*? owner = new i32(5);
        own i32*? empty = null;
        bool same_owner = owner == owner;
        if (owner != null) {
            result += *owner;
        }
        if (empty == null && same_owner == true) {
            result += 16;
        }

        try {
            result += require_pointer(&source);
            reject_pointer(&source);
            throw TestAssertionFailed {.code = 2};
        } catch (PointerError error) {
            if (error.observed == 7) {
                result += 32;
            }
        }

        drop owner;
        drop empty;
        drop defaults;
        i32 chosen = result == 181 ? 0 : 1;
        return chosen;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
