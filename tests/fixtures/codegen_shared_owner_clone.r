module test.codegen.shared_owner_clone;

struct Item {
    i32 value;
};

protected i32 clone_sync() {
    arc Item shared = new arc Item {
        .value = 11,
    };
    arc Item shared_clone = std.arc::clone(&shared);
    rc Item local = new rc Item {
        .value = 13,
    };
    rc Item local_clone = std.rc::clone(&local);
    i32 total = shared_clone->value + local_clone->value;
    return total;
}

protected async i32 clone_async() {
    arc Item shared = new arc Item {
        .value = 17,
    };
    arc Item shared_clone = std.arc::clone(&shared);
    rc Item local = new rc Item {
        .value = 19,
    };
    rc Item local_clone = std.rc::clone(&local);
    return shared_clone->value + local_clone->value;
}

async i32 main() {
    try {
        task<i32> operation = clone_async();
        i32 async_total = await move operation;
        if (async_total != 36) {
            return 1;
        }
        return 0;
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
}
