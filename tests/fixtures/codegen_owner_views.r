module test.codegen.owner_views;

/* R-BORROW-0018 (L15.3): an owner carries the regions of the views its payload holds. The payload
   of a unique owner is storage of that owner; the handles of a shared owner share one region. */
struct Node {
    own Node*? next;
    str word;
};

struct Holder {
    own Node* node;
};

struct Item {
    str name;
};

own Node* make(str word) {
    return new Node {.next = null, .word = word};
}

/* The store reaches storage the holder parameter designates through its owner (L15.2). */
void rename(Holder* holder, str word) {
    holder->node->word = word;
}

usize size(own Node* node) {
    return len(node->word);
}

@generic<T>
own T* boxed(T value) {
    return new T(move value);
}

i32 main() {
    try {
        try {
            own Node* tail = make("beta");
            own Node* head = new Node {.next = move tail, .word = "alpha"};
            if (len(head->word) != 5usize) { throw TestAssertionFailed {.code = 1}; }
            if (size(move head) != 5usize) { throw TestAssertionFailed {.code = 2}; }

            Holder holder = Holder {.node = make("one")};
            rename(&holder, "three");
            if (len(holder.node->word) != 5usize) { throw TestAssertionFailed {.code = 3}; }

            rc Item first = new rc Item {.name = "shared"};
            rc Item second = std.rc::clone(&first);
            Item*? unique = std.rc::get_mut(&first);
            if (unique != null) { throw TestAssertionFailed {.code = 4}; }
            move first as void;
            if (len(second->name) != 6usize) { throw TestAssertionFailed {.code = 5}; }

            arc str text = new arc str("words");
            if (len(*text) != 5usize) { throw TestAssertionFailed {.code = 6}; }

            array<own Node*> nodes = std.array::create::<own Node*>();
            std.array::push(&nodes, make("x"));
            std.array::push(&nodes, make("yz"));
            if ((len(nodes[0usize]->word) + len(nodes[1usize]->word)) != 3usize) {
                throw TestAssertionFailed {.code = 7};
            }

            own str* word = boxed::<str>("boxed");
            if (len(*word) != 5usize) { throw TestAssertionFailed {.code = 8}; }
        } catch (std.array::push_error<own Node*> failure) {
            move failure as void;
            throw TestAssertionFailed {.code = 20};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
