module codegen.managed_owner_access;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };

struct Item {
    i32 value;
};

i32 main() {
    try {
        own Item* unique = new Item {
            .value = 3,
        };
        arc Item shared = new arc Item {
            .value = 5,
        };
        rc Item local = new rc Item {
            .value = 7,
        };
        unique->value = 11;
        i32 unique_value = unique->value;
        i32 shared_value = shared->value;
        i32 local_value = local->value;
        const Item* shared_view = &*shared;
        i32 view_value = shared_view->value;
        TestStorage1 storage_raw_value = {.value = 0};
        unsafe {
            own Item* raw_owner = new Item {
                .value = 0,
            };
            raw Item* raw_unique = core::release(move raw_owner);
            raw_unique->value = 13;
            own Item* recovered = core::adopt(raw_unique);
            storage_raw_value.value = recovered->value;
        }
        if (((unique_value != 11) || (shared_value != 5)) ||
            ((local_value != 7) || (view_value != 5))) {
            throw TestAssertionFailed {.code = 1};
        }
        if (storage_raw_value.value != 13) {
            throw TestAssertionFailed {.code = 2};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
