module test.codegen.async_owner_views;

/* R-BORROW-0018 (L15.3): an async frame keeps owners that carry views between its awaits. Views
   end before each await (R-BORROW-0024), so the frame builds its owners after its awaits. */
struct Node {
    own Node*? next;
    str word;
};

struct Item {
    str name;
};

own Node* make(str word) {
    return new Node {.next = null, .word = word};
}

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async i32 work() throws std.async::start_error {
    i32 one = await tick(0);
    i32 two = await tick(one);
    if (two != 2) { return 6; }
    own Node* tail = make("beta");
    own Node* head = new Node {.next = move tail, .word = "alpha"};
    usize head_length = len(head->word);
    move head as void;
    if (head_length != 5usize) { return 1; }
    rc Item first = new rc Item {.name = "shared"};
    rc Item second = std.rc::clone(&first);
    move first as void;
    usize second_length = len(second->name);
    move second as void;
    if (second_length != 6usize) { return 2; }
    return 0;
}

async i32 main() {
    try {
        return await work();
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}
