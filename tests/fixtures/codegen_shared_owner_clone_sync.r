module test.codegen.shared_owner_clone_sync;

struct Item {
    i32 value;
};

i32 main() {
    arc Item shared = new arc Item {
        .value = 11,
    };
    arc Item shared_clone = std.arc::clone(&shared);
    rc Item local = new rc Item {
        .value = 13,
    };
    rc Item local_clone = std.rc::clone(&local);
    i32 total = shared_clone->value + local_clone->value;
    return total - 24;
}
