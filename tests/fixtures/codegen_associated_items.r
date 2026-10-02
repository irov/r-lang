module test.codegen.associated_items;

import std.iter;

/* R-TYPE-0045 (L14-S7): an item of an iterator holds what the iterator holds, never the
   iterator, so items outlive the exclusive borrows of the iterator that produce them. */
struct cursor {
    const i32[] items;
    usize at;
};

impl core::Iterator for cursor {
    type Item = const i32*;
    o<const i32*> next(cursor* this) {
        if (this->at >= len(this->items)) { return o::none; }
        const i32* item = &this->items[this->at];
        this->at += 1usize;
        return o::some(item);
    }
};

@generic<I: core::Iterator>
o<I::Item> last_of(I inner) {
    o<I::Item> result = o::none;
    while (true) {
        o<I::Item> item = inner.next();
        switch (move item) {
        case variant o::some(move value):
            result = o::some(move value);
            break;
        case variant o::none:
            return move result;
        }
    }
    return move result;
}

@generic<I: core::Iterator>
o<I::Item> last_via(I* source) {
    o<I::Item> result = o::none;
    while (true) {
        o<I::Item> item = source->next();
        switch (move item) {
        case variant o::some(move value):
            result = o::some(move value);
            break;
        case variant o::none:
            return move result;
        }
    }
    return move result;
}

i32 read(o<const i32*> item) {
    switch (move item) {
    case variant o::some(move value):
        return *value;
    case variant o::none:
        return -1;
    }
    return -2;
}

o<const i32*> second_item(const i32[] data) {
    cursor c = cursor {.items = data, .at = 0usize};
    o<const i32*> first = c.next();
    first as void;
    return c.next();
}

/* R-BORROW-0021: an element of a shared slice held in storage keeps the slice's region, so the
   slice may be replaced while the element lives. */
i32 advance(cursor* c) {
    const i32* head = &c->items[0usize];
    c->items = c->items[1usize..];
    return *head;
}

/* R-STMT-0010 (L14-S9): a payload read through its binding holds what the payload holds. */
struct Holder {
    const i32* item;
};

enum Choice { Empty, Value(Holder), };

Holder pick(Choice choice, Holder fallback) {
    switch (choice) {
    case variant Choice::Value(value): return *value;
    case variant Choice::Empty: return fallback;
    }
}

i32 main() {
    i32[4] data = {3, 5, 7, 11};
    const i32[] view = &data;
    try {
        try {
            cursor c = cursor {.items = view, .at = 0usize};
            o<const i32*> a = c.next();
            o<const i32*> b = c.next();
            if (read(move a) + read(move b) != 8) { throw TestAssertionFailed {.code = 1}; }

            array<const i32*> kept = std.array::create::<const i32*>();
            cursor d = cursor {.items = view, .at = 0usize};
            for (const i32* x in &d) { kept.push(x); }
            i32 total = 0;
            for (usize i = 0usize; i < len(kept); i += 1usize) { total += *kept[i]; }
            if (total != 26) { throw TestAssertionFailed {.code = 2}; }

            cursor e = cursor {.items = view, .at = 1usize};
            if (read(last_of::<cursor>(move e)) != 11) { throw TestAssertionFailed {.code = 3}; }

            cursor f = cursor {.items = view, .at = 2usize};
            o<const i32*> via = last_via::<cursor>(&f);
            if (read(move via) != 11) { throw TestAssertionFailed {.code = 4}; }
            if (read(f.next()) != -1) { throw TestAssertionFailed {.code = 5}; }

            if (read(second_item(view)) != 5) { throw TestAssertionFailed {.code = 6}; }

            cursor g = cursor {.items = view, .at = 0usize};
            i32 first = advance(&g);
            i32 second = advance(&g);
            if (first != 3 || second != 5) { throw TestAssertionFailed {.code = 7}; }
            if (len(g.items) != 2usize) { throw TestAssertionFailed {.code = 8}; }

            fn i32 add(i32 acc, const i32* x) { return acc + *x; }
            auto numbers = std.iter::of_slice(view);
            if (std.iter::fold(move numbers, 0, &add) != 26) { throw TestAssertionFailed {.code = 9}; }
            auto more = std.iter::of_slice(view);
            if (read(std.iter::last(move more)) != 11) { throw TestAssertionFailed {.code = 10}; }

            i32 x = 42;
            Holder h = pick(Choice::Value(Holder {.item = &x}), Holder {.item = &data[0usize]});
            if (*h.item != 42) { throw TestAssertionFailed {.code = 11}; }
        } catch (std.array::push_error<const i32*> failure) {
            failure as void;
            throw TestAssertionFailed {.code = 20};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
