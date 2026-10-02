module test.codegen.stack_recursive_list;

/* R-FUNC-0004: self-nesting through list elements; nodes drop back to front. */
thread_local i64 dropped = 0;
thread_local i64 last_value = -1;

struct Node {
    i64 value;
    list<Node> children;
};

drop(Node* self) {
    last_value = self->value;
    dropped += 1;
}

protected void append(list<Node>* target, Node value) throws std.list::push_error<Node> {
    Node* stored = std.list::push_back(target, move value);
    if (stored->value < 0) { last_value = -2; }
}

i32 main() {
    try {
        Node spine = { .value = 0, .children = std.list::create::<Node>() };
        i64 index = 1;
        while (index < 200000) {
            Node leaf = { .value = 200000 + index, .children = std.list::create::<Node>() };
            Node node = { .value = index, .children = std.list::create::<Node>() };
            append(&node.children, move leaf);
            append(&node.children, move spine);
            spine = move node;
            index += 1;
        }
        drop spine;
        if (dropped != 399999) { return 1; }
        if (last_value != 399999) { return 2; }
        return 0;
    } catch (std.list::push_error<Node> failure) {
        drop failure;
        return 3;
    }
}
