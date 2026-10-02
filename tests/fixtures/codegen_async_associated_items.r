module test.codegen.async_associated_items;

import std.iter;

/* R-TYPE-0045 (L14-S7): an async frame keeps items of an iterator while it borrows the
   iterator again; an item holds what the iterator holds, never the iterator. Views end before
   each await (R-BORROW-0024), so the frame iterates after its awaits. */
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

i32 read(o<const i32*> item) {
    switch (move item) {
    case variant o::some(move value):
        return *value;
    case variant o::none:
        return -1;
    }
    return -2;
}

/* R-BORROW-0021: an element of a shared slice held in storage keeps the slice's region. */
i32 advance(cursor* c) {
    const i32* head = &c->items[0usize];
    c->items = c->items[1usize..];
    return *head;
}

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async i32 work() throws std.async::start_error {
    i32 one = await tick(0);
    i32 two = await tick(one);
    if (two != 2) { return 6; }
    i32[4] data = {3, 5, 7, 11};
    const i32[] view = &data;
    cursor c = cursor {.items = view, .at = 0usize};
    o<const i32*> a = c.next();
    o<const i32*> b = c.next();
    i32 pair = read(move a) + read(move b);
    if (pair != 8) { return 1; }

    i32 total = 0;
    cursor d = cursor {.items = view, .at = 0usize};
    for (const i32* x in &d) {
        total += *x;
    }
    if (total != 26) { return 2; }

    cursor e = cursor {.items = view, .at = 1usize};
    o<const i32*> last = last_of::<cursor>(move e);
    i32 last_value = read(move last);
    if (last_value != 11) { return 3; }

    cursor g = cursor {.items = view, .at = 0usize};
    i32 first = advance(&g);
    i32 second = advance(&g);
    i32 advanced = first * 10 + second;
    if (advanced != 35 || len(g.items) != 2usize) { return 4; }

    fn i32 add(i32 acc, const i32* x) { return acc + *x; }
    auto numbers = std.iter::of_slice(view);
    i32 folded = std.iter::fold(move numbers, 0, &add);
    if (folded != 26) { return 5; }
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
