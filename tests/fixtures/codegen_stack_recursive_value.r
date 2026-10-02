module test.codegen.stack_recursive_value;

/* R-FUNC-0004: mutual self-nesting through a by-value member with two owner slots. */
thread_local i64 dropped = 0;
thread_local i64 last_value = -1;

struct Pair {
    own Node*? left;
    own Node*? right;
};

struct Node {
    i64 value;
    Pair pair;
};

drop(Node* self) {
    last_value = self->value;
    dropped += 1;
}

i32 main() {
    own Node*? spine = null;
    i64 index = 0;
    while (index < 250000) {
        own Node*? leaf = new Node { .value = 250000 + index, .pair = Pair { .left = null, .right = null } };
        own Node*? fresh = new Node { .value = index, .pair = Pair { .left = move spine, .right = move leaf } };
        spine = move fresh;
        index += 1;
    }
    drop spine;
    if (dropped != 500000) { return 1; }
    if (last_value != 250000) { return 2; }
    return 0;
}
