module test.codegen.stack_recursive_dict;

/* R-FUNC-0004: self-nesting through dict values; entries drop in reverse insertion order. */
thread_local i64 dropped = 0;
thread_local i64 last_value = -1;

struct Node {
    i64 value;
    dict<i32, Node> children;
};

drop(Node* self) {
    last_value = self->value;
    dropped += 1;
}

i32 main() {
    try {
        Node spine = { .value = 0, .children = std.dict::create::<i32, Node>() };
        i64 index = 1;
        while (index < 200000) {
            Node leaf = { .value = 200000 + index, .children = std.dict::create::<i32, Node>() };
            Node node = { .value = index, .children = std.dict::create::<i32, Node>() };
            o<Node> first = std.dict::insert(&node.children, 1, move leaf);
            drop first;
            o<Node> second = std.dict::insert(&node.children, 2, move spine);
            drop second;
            spine = move node;
            index += 1;
        }
        drop spine;
        if (dropped != 399999) { return 1; }
        if (last_value != 399999) { return 2; }
        return 0;
    } catch (std.dict::insert_error<i32, Node> failure) {
        drop failure;
        return 3;
    }
}
