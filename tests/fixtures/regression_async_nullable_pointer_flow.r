module regression.async_nullable_pointer_flow;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { bool value; };

struct OwnerDefault {
    own i32*? owner;
    raw i32*? raw_pointer;
};

async i32 main() {
    i32 source = 11;
    const i32*? pointer = null;
    const i32*? pointer_2 = &source;
    i32 result = pointer_2 != null ? *pointer_2 : 0;

    if (pointer_2 != null && *pointer_2 == 11) {
        result += 1;
    }
    while (pointer_2 != null) {
        result += *pointer_2;
        pointer_2 = null;
    }

    OwnerDefault defaults = {};
    TestStorage1 storage_raw_default = {.value = false};
    unsafe {
        storage_raw_default.value = defaults.raw_pointer == null;
    }
    if (defaults.owner == null && storage_raw_default.value == true) {
        result += 2;
    }

    own i32*? owner = new i32(5);
    own i32*? empty = null;
    bool same = owner == owner;
    if (owner != null) {
        result += *owner;
    }
    if (empty == null && same == true) {
        result += 4;
    }

    drop owner;
    drop empty;
    drop defaults;
    i32 selected = result == 34 ? 0 : 1;
    return selected;
}
