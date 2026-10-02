module test.codegen.optional_borrows;

/* R-TYPE-0012, R-BORROW-0018, R-BORROW-0009: `o<const T*>` and `o<T*>` as parameters and
   results. An optional input carries the origin of the borrow it holds; a moved borrow
   parameter still designates storage outside the function. */

struct Counter { i32[4] slots; i32 total; };

o<const i32*> find(const i32[] values, i32 wanted) {
    for (usize index = 0usize; index < len(values); index += 1usize) {
        if (values[index] == wanted) { return o::some(&values[index]); }
    }
    return o::none;
}
o<i32*> Counter::find_mut(Counter* this, i32 wanted) {
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        if (this->slots[index] == wanted) { return o::some(&this->slots[index]); }
    }
    return o::none;
}
i32 read_or(o<const i32*> value, i32 fallback) {
    switch (value) {
        case variant o::some(item): return **item;
        case variant o::none: return fallback;
    }
}
void write_if(o<i32*> target, i32 value) {
    switch (move target) {
        case variant o::some(item): **item = value; break;
        case variant o::none: break;
    }
}
void write_moved(i32* target, i32 value) {
    i32* moved = move target;
    *moved = value;
}
@generic<T: copy> T value_or(o<const T*> value, T fallback) {
    switch (value) {
        case variant o::some(item): return **item;
        case variant o::none: return move fallback;
    }
}
i32 Counter::add_optional(Counter* this, o<const i32*> extra) {
    switch (extra) {
        case variant o::some(item): this->total += **item; break;
        case variant o::none: break;
    }
    return this->total;
}
@generic<F: fn(o<const i32*>) -> i32> i32 apply(F f, o<const i32*> value) { return f(value); }
i32 twice(o<const i32*> value) { return value_or::<i32>(value, 0) * 2; }

i32 main() {
    Counter c = {.slots = {1, 2, 3, 4}, .total = 0};
    const i32[] view = c.slots[0usize..4usize];
    if (read_or(find(view, 3), 0) != 3 || read_or(find(view, 9), -1) != -1) { return 1; }
    write_if(c.find_mut(2), 20);
    o<i32*> slot = c.find_mut(4);
    write_if(move slot, 40);
    write_if(o::none, 50);
    write_moved(&c.slots[0], 10);
    if (c.slots[0] != 10 || c.slots[1] != 20 || c.slots[3] != 40) { return 2; }
    i32 bonus = 10;
    if (c.add_optional(o::some(&bonus)) != 10 || c.add_optional(o::none) != 10) { return 3; }
    i32 local = 21;
    if (value_or::<i32>(o::some(&local), 0) != 21 || value_or::<i32>(o::none, 7) != 7) {
        return 4;
    }
    o<const i32*> shared = o::some(&local);
    if (apply(twice, shared) != 42) { return 5; }
    return 0;
}
