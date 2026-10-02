module test.codegen.fixed_array_move;

/* Fixed arrays of Move elements are automatic objects and Move values in their own right. */
thread_local i64 dropped = 0;

struct Node {
    i64 value;
    (own Node*?)[2] children;
};

drop(Node* self) {
    dropped += self->value;
}

protected (own Node*?)[2] pair(own Node*? left, own Node*? right) {
    (own Node*?)[2] children = {};
    children[0] = move left;
    children[1] = move right;
    return move children;
}

i32 main() {
    own Node*? leaf = new Node { .value = 1, .children = {} };
    own Node*? other = new Node { .value = 3, .children = {} };
    (own Node*?)[2] children = pair(move leaf, move other);
    (own Node*?)[2] taken = move children;
    own Node*? fresh = new Node { .value = 10, .children = move taken };
    (own Node*?)[2] spare = {};
    spare[1] = new Node { .value = 100, .children = {} };
    drop fresh;
    if (dropped != 14) { return 1; }
    drop spare;
    if (dropped != 114) { return 2; }
    return 0;
}
