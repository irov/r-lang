module test.codegen.stack_recursive_owner_array;

/* R-FUNC-0004: self-nesting through an array of owners; each element is an owner slot. */
thread_local i64 dropped = 0;
thread_local i64 last_value = -1;

struct Node {
    i64 value;
    array<own Node*> children;
};

drop(Node* self) {
    last_value = self->value;
    dropped += 1;
}

i32 main() {
    try {
        own Node* spine = new Node { .value = 0, .children = std.array::with_capacity::<own Node*>(0) };
        i64 index = 1;
        while (index < 250000) {
            own Node* leaf = new Node { .value = 250000 + index, .children = std.array::with_capacity::<own Node*>(0) };
            own Node* node = new Node { .value = index, .children = std.array::with_capacity::<own Node*>(2) };
            std.array::push(&node->children, move leaf);
            std.array::push(&node->children, move spine);
            spine = move node;
            index += 1;
        }
        drop spine;
        if (dropped != 499999) { return 1; }
        if (last_value != 499999) { return 2; }
        return 0;
    } catch (std.array::push_error<own Node*> failure) {
        drop failure;
        return 3;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 4;
    }
}
