module test.codegen.stack_recursive_enum;

/* R-FUNC-0004: self-nesting through a tagged enum payload held by value. */
thread_local i64 dropped = 0;
thread_local i64 last_value = -1;
thread_local bool ordered = true;

enum Link { Nil, Cons(own Node*), };

struct Node {
    i64 value;
    Link next;
};

drop(Node* self) {
    if (dropped != 0 && self->value + 1 != last_value) { ordered = false; }
    last_value = self->value;
    dropped += 1;
}

i32 main() {
    Link head = Link::Nil;
    i64 index = 0;
    while (index < 1000000) {
        own Node* fresh = new Node { .value = index, .next = move head };
        head = Link::Cons(move fresh);
        index += 1;
    }
    drop head;
    if (dropped != 1000000) { return 1; }
    if (last_value != 0) { return 2; }
    if (ordered == false) { return 3; }
    return 0;
}
