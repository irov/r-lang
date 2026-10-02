module test.codegen.stack_recursive_fixed;

/* R-FUNC-0004: self-nesting through a fixed array of owners; elements drop last to first. */
thread_local i64 dropped = 0;
thread_local i64 last_value = -1;

struct Node {
    i64 value;
    (own Node*?)[2] children;
};

drop(Node* self) {
    last_value = self->value;
    dropped += 1;
}

i32 main() {
    own Node*? spine = null;
    i64 index = 1;
    while (index < 250000) {
        own Node*? leaf = new Node { .value = 250000 + index, .children = {} };
        own Node*? fresh = new Node { .value = index, .children = {move leaf, move spine} };
        spine = move fresh;
        index += 1;
    }
    drop spine;
    if (dropped != 499998) { return 1; }
    if (last_value != 499999) { return 2; }
    return 0;
}
