module test.codegen.async_shared_owner_clone_field;

struct Item {
    i32 value;
};

struct Box {
    arc Item owner;
};

protected async i32 clone_field() {
    own Box* box = new Box {
        .owner = new arc Item {
            .value = 29,
        },
    };
    arc Item clone = std.arc::clone(&box->owner);
    return clone->value - 29;
}

async i32 main() {
    try {
        task<i32> operation = clone_field();
        i32 status = await move operation;
        return status;
    } catch (std.async::start_error error) {
        error as void;
        return 1;
    }
}
