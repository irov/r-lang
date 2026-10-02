module test.codegen.stack_recursive_array;

/* R-FUNC-0004: self-nesting through array elements held by value; elements drop last to first. */
thread_local i64 dropped = 0;
thread_local i64 last_value = -1;

struct Node {
    i64 value;
    array<Node> children;
};

drop(Node* self) {
    last_value = self->value;
    dropped += 1;
}

i32 main() {
    try {
        Node spine = { .value = 0, .children = std.array::with_capacity::<Node>(0) };
        i64 index = 1;
        while (index < 200000) {
            Node leaf = { .value = 200000 + index, .children = std.array::with_capacity::<Node>(0) };
            Node node = { .value = index, .children = std.array::with_capacity::<Node>(2) };
            std.array::push(&node.children, move leaf);
            std.array::push(&node.children, move spine);
            spine = move node;
            index += 1;
        }
        drop spine;
        if (dropped != 399999) { return 1; }
        if (last_value != 399999) { return 2; }
        return 0;
    } catch (std.array::push_error<Node> failure) {
        drop failure;
        return 3;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 4;
    }
}
